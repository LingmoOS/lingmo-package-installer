// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base/dbus.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Lingmo::base { class EventLoop; }

namespace PackageInstaller {

class InstallController
{
public:
    explicit InstallController(Lingmo::base::EventLoop& loop);
    ~InstallController();

    InstallController(const InstallController&) = delete;
    InstallController& operator=(const InstallController&) = delete;

    bool isRunning() const;
    bool start(const std::filesystem::path& packageFile);
    void cancel();

    std::function<void(const std::string& stage, int percentage)> onProgress;
    std::function<void(bool success, bool cancelled, std::string detail)> onFinished;

private:
    void configureTransaction(const std::string& path);
    void installFiles();
    void finish(bool success, bool cancelled, std::string details);

    Lingmo::base::EventLoop& m_loop;
    Lingmo::base::dbus::Connection* m_connection = nullptr;
    Lingmo::base::dbus::NameWatch m_serviceWatch;
    std::unique_ptr<Lingmo::base::dbus::Proxy> m_transaction;
    std::vector<Lingmo::base::dbus::Match> m_matches;
    std::vector<Lingmo::base::dbus::PendingCall> m_pendingCalls;
    std::filesystem::path m_packageFile;
    std::string m_errorDetails;
    std::string m_stage;
    int m_percentage = -1;
    bool m_running = false;
    bool m_installStarted = false;
    bool m_cancelRequested = false;
    bool m_serviceHasOwner = false;
};

} // namespace PackageInstaller
