// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "globalmenu.h"
#include "installcontroller.h"
#include "package.h"

#include "base/process.h"
#include "base/signal.h"
#include "ui/window.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Lingmo::ui {
class AboutDialog;
class Application;
class ConfirmDialog;
class DialogWindow;
class Menu;
class View;
}

namespace PackageInstaller {

class MainWindow
{
public:
    explicit MainWindow(Lingmo::ui::Application& application,
                        std::vector<std::filesystem::path> packageFiles);
    ~MainWindow();

    void show();
    void packageDropRequested();
    void cancelInstall();
    void showDetails();

private:
    class Root;
    void openNextFile();
    void openDroppedPackages(std::vector<std::filesystem::path> packageFiles);
    void inspectCurrentFile();
    void showCurrentPackage();
    void queryNativeArchitecture();
    void queryForeignArchitectures();
    void queryInstalledPackage();
    void compareInstalledVersion();
    void startInstall();
    void setError(std::string friendly, std::string details = {});
    void showMenu(Lingmo::ui::View* anchor);
    void showAbout();
    std::vector<GlobalMenu::Entry> menuEntries();

    Lingmo::ui::Application& m_application;
    Lingmo::ui::Window m_window;
    Root* m_root = nullptr;
    std::vector<std::filesystem::path> m_packageFiles;
    size_t m_currentFile = 0;
    std::optional<PackageInfo> m_package;
    std::unique_ptr<Lingmo::base::Process> m_process;
    std::unique_ptr<InstallController> m_installer;
    std::unique_ptr<Lingmo::ui::ConfirmDialog> m_confirmation;
    std::unique_ptr<Lingmo::ui::DialogWindow> m_detailsDialog;
    std::unique_ptr<Lingmo::ui::Menu> m_localMenu;
    std::unique_ptr<Lingmo::ui::AboutDialog> m_about;
    std::unique_ptr<GlobalMenu> m_globalMenu;
    Lingmo::base::Subscription m_menuAvailability;
    std::string m_nativeArchitecture;
    std::vector<std::string> m_foreignArchitectures;
    std::string m_currentInstalledVersion;
    std::string m_friendlyError;
    std::string m_errorDetails;
    bool m_operationPending = false;
    bool m_autoInstallDroppedPackages = false;
};

} // namespace PackageInstaller
