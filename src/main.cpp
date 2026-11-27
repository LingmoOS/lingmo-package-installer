// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include "base/i18n.h"
#include "ui/application.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::filesystem::path> packages;
    bool positionalOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (!positionalOnly && std::strcmp(argv[i], "--") == 0) {
            positionalOnly = true;
            continue;
        }
        if (!positionalOnly && (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)) {
            std::puts("lingmo-package-installer 1.0.13");
            return 0;
        }
        if (!positionalOnly && (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)) {
            std::printf("Usage: %s [--version] [--help] [package.deb ...]\n", argv[0]);
            return 0;
        }
        packages.emplace_back(argv[i]);
    }

    Lingmo::ui::Application application(argc, argv);
    application.setApplicationId("lingmo-package-installer");
    Lingmo::I18n::setDomain("lingmo-package-installer");
    PackageInstaller::MainWindow window(application, std::move(packages));
    window.show();
    return application.run();
}
