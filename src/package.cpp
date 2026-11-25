// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace PackageInstaller {
namespace {

std::string trim(std::string_view text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool validPackageName(std::string_view name)
{
    if (name.empty() || (!std::islower(static_cast<unsigned char>(name.front()))
                         && !std::isdigit(static_cast<unsigned char>(name.front()))))
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '+' || ch == '-' || ch == '.';
    });
}

std::string field(const std::map<std::string, std::string>& fields, const char* name)
{
    const auto it = fields.find(name);
    return it == fields.end() ? std::string() : it->second;
}

} // namespace

ParseResult parseControlFields(std::string_view control)
{
    std::map<std::string, std::string> fields;
    size_t offset = 0;
    std::string current;
    while (offset <= control.size()) {
        const size_t end = control.find('\n', offset);
        const std::string_view line = control.substr(offset,
            end == std::string_view::npos ? std::string_view::npos : end - offset);
        if (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            if (current.empty())
                return {{}, "The package metadata contains an invalid continuation line."};
            fields[current] += "\n" + trim(line);
        } else if (!line.empty()) {
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0)
                return {{}, "The package metadata contains an invalid field."};
            current = lower(trim(line.substr(0, colon)));
            if (current.empty() || fields.contains(current))
                return {{}, "The package metadata contains a duplicate or empty field."};
            fields[current] = trim(line.substr(colon + 1));
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1;
    }

    PackageInfo package;
    package.package = field(fields, "package");
    package.version = field(fields, "version");
    package.architecture = field(fields, "architecture");
    if (!validPackageName(package.package) || package.version.empty() || package.architecture.empty())
        return {{}, "The package is missing a valid name, version, or architecture."};

    package.description = field(fields, "description");
    package.maintainer = field(fields, "maintainer");
    package.source = field(fields, "origin");
    if (package.source.empty())
        package.source = field(fields, "source");
    package.depends = field(fields, "depends");
    package.preDepends = field(fields, "pre-depends");
    package.conflicts = field(fields, "conflicts");

    const std::string installedSize = field(fields, "installed-size");
    uint64_t kibibytes = 0;
    if (!installedSize.empty()) {
        const auto result = std::from_chars(installedSize.data(),
                                            installedSize.data() + installedSize.size(), kibibytes);
        if (result.ec != std::errc() || result.ptr != installedSize.data() + installedSize.size())
            return {{}, "The package reports an invalid installed size."};
        if (kibibytes > UINT64_MAX / 1024)
            return {{}, "The package reports an installed size that is too large."};
        package.installedSizeBytes = kibibytes * 1024;
    }
    return {std::move(package), {}};
}

bool architectureCompatible(std::string_view packageArchitecture,
                            std::string_view nativeArchitecture,
                            const std::vector<std::string>& foreignArchitectures)
{
    return packageArchitecture == "all" || packageArchitecture == nativeArchitecture
        || std::find(foreignArchitectures.begin(), foreignArchitectures.end(), packageArchitecture)
            != foreignArchitectures.end();
}

std::string formatSize(uint64_t bytes)
{
    static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    size_t unit = 0;
    while (size >= 1024.0 && unit + 1 < std::size(units)) {
        size /= 1024.0;
        ++unit;
    }
    std::ostringstream result;
    result << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << size << ' ' << units[unit];
    return result.str();
}

} // namespace PackageInstaller
