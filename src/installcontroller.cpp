// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "installcontroller.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>

namespace PackageInstaller {
namespace {

constexpr uint32_t kExitSuccess = 1;
constexpr uint32_t kExitCancelled = 3;
constexpr size_t kMaximumErrorBytes = 32 * 1024;
constexpr auto kAuthorizationTimeout = std::chrono::minutes(5);

using Lingmo::base::dbus::Message;
using Lingmo::base::dbus::Proxy;
using Lingmo::base::dbus::Reply;
using Lingmo::base::dbus::VariantMap;

std::string statusForPackageKit(uint32_t status)
{
    switch (status) {
    case 7:  // refresh-cache
    case 8:  // download
    case 20: // download-repository
    case 21: // download-packagelist
    case 22: // download-filelist
    case 23: // download-changelog
    case 24: // download-updateinfo
        return "Getting required packages";
    case 9:  // install
    case 10: // update
    case 16: // commit
    case 29: // copy-files
        return "Installing package";
    case 11: // cleanup
    case 18: // finished
        return "Finishing installation";
    case 13: // dep-resolve
    case 15: // test-commit
        return "Checking dependencies";
    case 14: // sig-check
        return "Checking package";
    case 28: // waiting-for-auth
        return "Waiting for administrator authorization";
    case 30: // run-hook
        return "Configuring package";
    default:
        return {};
    }
}

std::string errorDescription(const Reply& reply)
{
    const auto& error = reply.error();
    if (!error.name.empty() && !error.message.empty())
        return error.name + ": " + error.message;
    if (!error.message.empty())
        return error.message;
    if (!error.name.empty())
        return error.name;
    return "PackageKit returned an unspecified D-Bus error.";
}

void appendBounded(std::string& destination, std::string_view text)
{
    if (text.size() >= kMaximumErrorBytes) {
        destination.assign(text.substr(text.size() - kMaximumErrorBytes));
        return;
    }
    if (destination.size() + text.size() > kMaximumErrorBytes)
        destination.erase(0, destination.size() + text.size() - kMaximumErrorBytes);
    destination.append(text);
}

void appendError(std::string& destination, std::string_view text)
{
    if (text.empty())
        return;
    if (!destination.empty() && destination.back() != '\n')
        appendBounded(destination, "\n");
    appendBounded(destination, text);
}

} // namespace

InstallController::InstallController(Lingmo::base::EventLoop& loop) : m_loop(loop) {}

InstallController::~InstallController()
{
    if (m_running && m_transaction)
        m_transaction->send("Cancel");
}

bool InstallController::isRunning() const
{
    return m_running;
}

bool InstallController::start(const std::filesystem::path& packageFile)
{
    if (m_running)
        return false;

    std::error_code fileError;
    const std::filesystem::path canonicalFile = std::filesystem::canonical(packageFile, fileError);
    if (fileError || !std::filesystem::is_regular_file(canonicalFile, fileError) || fileError)
        return false;

    m_connection = Lingmo::base::dbus::Connection::system(&m_loop);
    if (!m_connection)
        return false;

    m_pendingCalls.clear();
    m_matches.clear();
    m_serviceWatch = {};
    m_transaction.reset();
    m_errorDetails.clear();
    m_stage.clear();
    m_percentage = -1;
    m_installStarted = false;
    m_cancelRequested = false;
    m_serviceHasOwner = false;
    m_packageFile = canonicalFile;
    m_running = true;

    m_serviceWatch = m_connection->watchName("org.freedesktop.PackageKit", [this](const std::string& owner) {
        if (!owner.empty()) {
            m_serviceHasOwner = true;
        } else if (m_serviceHasOwner && m_running) {
            finish(false, false, "The PackageKit service disconnected during installation.");
        }
    });

    Proxy packageKit(m_connection, "org.freedesktop.PackageKit", "/org/freedesktop/PackageKit",
                     "org.freedesktop.PackageKit");
    packageKit.setTimeout(kAuthorizationTimeout);
    m_pendingCalls.push_back(packageKit.callAsync("CreateTransaction", [this](const Reply& reply) {
        if (!m_running)
            return;
        if (reply.isError()) {
            finish(false, false, errorDescription(reply));
            return;
        }

        const auto transactionPath = reply.value<Lingmo::base::dbus::ObjectPath>();
        if (!transactionPath || transactionPath->path.empty()) {
            finish(false, false, "PackageKit did not return a transaction object path.");
            return;
        }
        configureTransaction(transactionPath->path);
    }));
    return true;
}

void InstallController::configureTransaction(const std::string& path)
{
    if (!m_running)
        return;

    m_transaction = std::make_unique<Proxy>(
        m_connection, "org.freedesktop.PackageKit", path, "org.freedesktop.PackageKit.Transaction");
    m_transaction->setTimeout(kAuthorizationTimeout);
    m_transaction->setInteractiveAuthorizationAllowed(true);

    m_matches.push_back(m_transaction->onPropertiesChanged(
        [this](const VariantMap& changed, const std::vector<std::string>&) {
            if (!m_running)
                return;

            const auto status = changed.find("Status");
            if (status != changed.end()) {
                if (const auto value = status->second.get<uint32_t>())
                    m_stage = statusForPackageKit(*value);
            }

            const auto percentage = changed.find("Percentage");
            if (percentage != changed.end()) {
                if (const auto value = percentage->second.get<uint32_t>())
                    m_percentage = *value <= 100 ? static_cast<int>(*value) : -1;
            }

            if (onProgress)
                onProgress(m_stage, m_percentage);
        }));
    m_matches.push_back(m_transaction->onSignal("ErrorCode", [this](const Message& message) {
        if (!m_running)
            return;

        uint32_t code = 0;
        std::string details;
        if (!message.read(code, details)) {
            appendError(m_errorDetails, "PackageKit returned an invalid error signal.");
            return;
        }
        appendError(m_errorDetails, "PackageKit error " + std::to_string(code) + ": " + details);
    }));
    m_matches.push_back(m_transaction->onSignal("Finished", [this](const Message& message) {
        if (!m_running)
            return;

        uint32_t exitCode = 0;
        uint32_t runtime = 0;
        if (!message.read(exitCode, runtime)) {
            finish(false, false, "PackageKit returned an invalid transaction completion signal.");
            return;
        }
        (void)runtime;

        const bool success = exitCode == kExitSuccess;
        const bool cancelled = exitCode == kExitCancelled;
        std::string details = std::exchange(m_errorDetails, {});
        if (!success && details.empty())
            details = "PackageKit transaction finished with exit status " + std::to_string(exitCode) + ".";
        finish(success, cancelled, std::move(details));
    }));
    m_matches.push_back(m_transaction->onSignal("Destroy", [this](const Message&) {
        if (m_running)
            finish(false, false, "PackageKit destroyed the transaction before reporting completion.");
    }));

    for (const auto& match : m_matches) {
        if (match.isNull()) {
            finish(false, false, "Could not subscribe to PackageKit transaction updates.");
            return;
        }
    }

    m_pendingCalls.push_back(m_transaction->callAsync(
        "SetHints", [this](const Reply& reply) {
            if (!m_running)
                return;
            if (reply.isError()) {
                finish(false, false, errorDescription(reply));
                return;
            }
            installFiles();
        },
        std::vector<std::string>{"interactive=true"}));
}

void InstallController::installFiles()
{
    if (!m_running || !m_transaction || m_cancelRequested)
        return;

    m_installStarted = true;
    m_pendingCalls.push_back(m_transaction->callAsync(
        "InstallFiles", [this](const Reply& reply) {
            if (!m_running)
                return;
            if (reply.isError())
                finish(false, false, errorDescription(reply));
        },
        uint64_t(0), std::vector<std::string>{m_packageFile.string()}));
}

void InstallController::cancel()
{
    if (!m_running || m_cancelRequested)
        return;

    m_cancelRequested = true;
    if (!m_transaction || !m_installStarted) {
        if (m_transaction)
            m_transaction->send("Cancel");
        finish(false, true, {});
        return;
    }

    m_pendingCalls.push_back(m_transaction->callAsync("Cancel", [this](const Reply& reply) {
        if (m_running && reply.isError())
            appendError(m_errorDetails, "Could not cancel the transaction: " + errorDescription(reply));
    }));
}

void InstallController::finish(bool success, bool cancelled, std::string details)
{
    if (!m_running)
        return;

    m_running = false;
    if (onFinished)
        onFinished(success, cancelled, std::move(details));
}

} // namespace PackageInstaller
