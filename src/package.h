// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PackageInstaller {

struct PackageInfo
{
    std::string package;
    std::string version;
    std::string architecture;
    std::string description;
    std::string maintainer;
    std::string source;
    std::string depends;
    std::string preDepends;
    std::string conflicts;
    uint64_t installedSizeBytes = 0;
};

struct ParseResult
{
    std::optional<PackageInfo> package;
    std::string error;
};

ParseResult parseControlFields(std::string_view control);
bool architectureCompatible(std::string_view packageArchitecture,
                            std::string_view nativeArchitecture,
                            const std::vector<std::string>& foreignArchitectures);
std::string formatSize(uint64_t bytes);

} // namespace PackageInstaller
