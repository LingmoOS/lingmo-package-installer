// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Package metadata test failed: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace PackageInstaller;
    const ParseResult parsed = parseControlFields(
        "Package: sample-app\n"
        "Version: 2:1.4-3\n"
        "Architecture: amd64\n"
        "Maintainer: Example Maintainer <maintainer@example.invalid>\n"
        "Origin: Example Repository\n"
        "Depends: libsample1 (>= 1.0), libc6\n"
        "Installed-Size: 2048\n"
        "Description: A sample application\n"
        " A continuation line.\n");
    require(parsed.package.has_value(), "valid metadata should parse");
    require(parsed.package->package == "sample-app", "package name");
    require(parsed.package->version == "2:1.4-3", "epoch version retained");
    require(parsed.package->description == "A sample application\nA continuation line.",
            "description continuation");
    require(parsed.package->depends == "libsample1 (>= 1.0), libc6", "dependencies");
    require(parsed.package->source == "Example Repository", "origin");
    require(parsed.package->installedSizeBytes == 2 * 1024 * 1024, "installed size");

    require(!parseControlFields("Version: 1\nArchitecture: amd64\n").package,
            "required package field");
    require(!parseControlFields("Package: bad name\nVersion: 1\nArchitecture: amd64\n").package,
            "invalid package name");
    require(!parseControlFields("Package: sample\nVersion: 1\nArchitecture: amd64\nInstalled-Size: nope\n").package,
            "invalid installed size");
    require(!parseControlFields(" Description without a field\n").package,
            "orphan continuation");

    require(architectureCompatible("all", "amd64", {}), "architecture-independent package");
    require(architectureCompatible("amd64", "amd64", {}), "native architecture");
    require(architectureCompatible("i386", "amd64", {"i386"}), "configured foreign architecture");
    require(!architectureCompatible("arm64", "amd64", {"i386"}), "incompatible architecture");
    require(formatSize(0) == "0 B", "zero size");
    std::cout << "Package metadata checks passed\n";
}
