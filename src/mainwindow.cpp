// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include "base/i18n.h"
#include "ui/about_dialog.h"
#include "ui/application.h"
#include "ui/button.h"
#include "ui/color_id.h"
#include "ui/color_provider.h"
#include "ui/dialog_window.h"
#include "ui/drag.h"
#include "ui/header_button.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/menu.h"
#include "ui/painter.h"
#include "ui/progress_bar.h"
#include "ui/theme.h"
#include "ui/window_header.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unistd.h>

using namespace Lingmo::ui;

namespace PackageInstaller {
namespace {
constexpr double kHeaderHeight = 52;
constexpr double kContentTop = 64;
constexpr double kPackageIconSize = 104;
constexpr double kIconTileSize = kPackageIconSize * 1.5;
constexpr double kFolderIconSize = kPackageIconSize;

std::string tr(const char* text) { return Lingmo::I18n::tr(text); }

std::vector<std::string> lines(std::string text)
{
    std::vector<std::string> result;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            result.push_back(std::move(line));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}

std::string statusForPackageManager(const std::string& stage)
{
    if (stage == "Getting required packages") return tr("Getting required packages");
    if (stage == "Checking dependencies") return tr("Checking dependencies");
    if (stage == "Checking package") return tr("Checking package");
    if (stage == "Unpacking package") return tr("Unpacking package");
    if (stage == "Configuring package") return tr("Configuring package");
    if (stage == "Installing package") return tr("Installing package");
    if (stage == "Finishing installation") return tr("Finishing installation");
    if (stage == "Waiting for administrator authorization")
        return tr("Waiting for administrator authorization");
    return tr("Working with the system package service");
}

std::string friendlyInstallError(const std::string& details)
{
    std::string text = details;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (text.find("not authorized") != std::string::npos
        || text.find("authentication") != std::string::npos
        || text.find("cancelled by user") != std::string::npos)
        return tr("Administrator authorization was not completed.");
    if (text.find("no space") != std::string::npos
        || text.find("disk space") != std::string::npos)
        return tr("There is not enough free disk space to install this package.");
    if (text.find("conflict") != std::string::npos)
        return tr("This package conflicts with software already on the system.");
    if (text.find("depend") != std::string::npos)
        return tr("Some required software could not be installed.");
    if (text.find("download") != std::string::npos
        || text.find("network") != std::string::npos
        || text.find("repository") != std::string::npos)
        return tr("The required software could not be downloaded from the configured repositories.");
    if (text.find("already installed") != std::string::npos)
        return tr("This package is already installed.");
    if (text.find("not trusted") != std::string::npos
        || text.find("untrusted") != std::string::npos
        || text.find("signature") != std::string::npos)
        return tr("The system package service could not verify this package.");
    return tr("The package could not be installed. No success was reported by the system package service.");
}

class HeaderContent final : public View
{
public:
    explicit HeaderContent(std::function<void(View*)> showMenu)
        : m_title(addChild<Label>(tr("Lingmo Package Installer"))),
          m_menu(addChild<HeaderButton>("menu", 32))
    {
        m_title->setName("windowTitle");
        m_title->setFontSize(15);
        m_title->setTextAlignment(Label::TextAlignment::Center);
        m_title->setVerticallyCentered(true);
        m_menu->setToolTip(tr("Application menu"));
        m_menu->onClicked = [this, showMenu = std::move(showMenu)] { showMenu(m_menu); };
    }

    void setMenuVisible(bool visible)
    {
        m_menu->setVisible(visible);
        requestLayout();
    }

protected:
    void layout() override
    {
        const double windowWidth = window() ? window()->size().width : localRect().width;
        const double titleWidth = std::max(0.0,
            std::min(localRect().width - 120, windowWidth - 120));
        m_title->setGeometry({(windowWidth - titleWidth) / 2, 4, titleWidth, 44});
        m_title->setColor(window() && !window()->isActive()
                              ? Theme::current().disabledText : Theme::current().text);
        m_menu->setGeometry({std::max(0.0, localRect().width - 44), 10, 32, 32});
    }

private:
    Label* m_title;
    HeaderButton* m_menu;
};

class PackageIcon final : public View
{
public:
    explicit PackageIcon(MainWindow& owner)
        : m_owner(owner), m_icon(addChild<ThemeIcon>("application-x-deb", kPackageIconSize))
    {
        setName("packageIconDragSource");
        setCursorShape(CursorShape::Grab);
    }

    void setPackageIcon(std::string icon)
    {
        if (icon.empty() || themeIconPath(icon, 128).empty())
            icon = "application-x-deb";
        m_icon->setIconName(std::move(icon));
        requestPaint();
    }

    void setCanDrag(bool canDrag) { m_canDrag = canDrag; }
    bool acceptsPointer() const override { return m_canDrag; }

protected:
    void layout() override
    {
        m_icon->setGeometry({(localRect().width - kPackageIconSize) / 2,
                             (localRect().height - kPackageIconSize) / 2,
                             kPackageIconSize, kPackageIconSize});
    }

    void pointerEnter() override
    {
        if (m_canDrag)
            setOpacity(0.9);
    }

    void pointerLeave() override
    {
        if (!m_dragging)
            setOpacity(1);
        m_pressed = false;
    }

    void pointerPress(const PointerEvent& event) override
    {
        if (event.button == MouseButton::Left && m_canDrag) {
            m_pressed = true;
            m_pressPosition = event.position;
        }
    }

    void pointerMove(const PointerEvent& event) override
    {
        if (!m_pressed || m_dragging || std::hypot(event.position.x - m_pressPosition.x,
                                                   event.position.y - m_pressPosition.y) < 8)
            return;

        DragSource source;
        source.mimeTypes = {"application/x-lingmo-package-installer"};
        source.data = [](const std::string&) { return "install"; };
        source.actions = CopyAction;
        const double scale = window() ? window()->scale() : 1;
        source.icon = grab(scale);
        source.iconScale = scale / 1.08;
        source.hotSpot = {localRect().width / 2, localRect().height / 2};
        source.context = &m_owner;
        source.onEnded = [this](DropAction, bool) {
            m_dragging = false;
            m_pressed = false;
            setOpacity(1);
        };
        m_dragging = startDrag(std::move(source));
        m_pressed = false;
        if (m_dragging)
            setOpacity(0.45);
    }

    void pointerRelease(const PointerEvent& event) override
    {
        if (event.button == MouseButton::Left)
            m_pressed = false;
    }

private:
    MainWindow& m_owner;
    ThemeIcon* m_icon;
    PointF m_pressPosition;
    bool m_canDrag = false;
    bool m_pressed = false;
    bool m_dragging = false;
};

class InstallTarget final : public View
{
public:
    explicit InstallTarget(MainWindow& owner)
        : m_owner(owner), m_icon(addChild<ThemeIcon>("folder", kFolderIconSize)),
          m_title(addChild<Label>(tr("Lingmo OS"))),
          m_message(addChild<Label>(tr("Drop the app here to install"))),
          m_stage(addChild<Label>()), m_progress(addChild<ProgressBar>()),
          m_action(addChild<Button>()), m_details(addChild<Button>(tr("View details")))
    {
        m_title->setFontSize(22);
        m_title->setWeight(600);
        m_title->setTextAlignment(Label::TextAlignment::Center);
        m_message->setFontSize(14);
        m_message->setColor(ColorRecipe(ColorId::DisabledText));
        m_message->setTextAlignment(Label::TextAlignment::Center);
        m_message->setWrap(true);
        m_stage->setFontSize(13);
        m_stage->setTextAlignment(Label::TextAlignment::Center);
        m_stage->setWrap(true);
        m_progress->setIndeterminate(true);
        m_action->onClicked = [this] { if (m_actionHandler) m_actionHandler(); };
        m_details->onClicked = [&owner] { owner.showDetails(); };
        m_details->setVisible(false);
        m_action->setVisible(false);
        setName("virtualLingmoInstallTarget");
    }

    void setAction(std::string label, std::function<void()> handler)
    {
        m_action->setText(std::move(label));
        m_actionHandler = std::move(handler);
        m_action->setVisible(bool(m_actionHandler));
        requestLayout();
    }

    void showReady()
    {
        m_mode = Mode::Ready;
        m_icon->setIconName("folder");
        m_title->setText(tr("Lingmo OS"));
        m_message->setText(tr("Drop the app here to install"));
        m_stage->setText(tr("The package manager will check dependencies and request authorization."));
        m_progress->setVisible(false);
        m_action->setVisible(false);
        m_details->setVisible(false);
        requestLayout();
        requestPaint();
    }

    void showWelcome()
    {
        showReady();
        m_message->setText(tr("Open a Debian package to get started"));
        m_stage->setText(tr("Double-click a .deb file in the file manager, or launch this app with a package path."));
    }

    void showLoading(const std::string& text)
    {
        m_mode = Mode::Loading;
        m_icon->setIconName("application-x-deb");
        m_title->setText(tr("Checking package"));
        m_message->setText(text);
        m_stage->setText({});
        m_progress->setVisible(true);
        m_progress->setIndeterminate(true);
        m_action->setVisible(false);
        m_details->setVisible(false);
        requestLayout();
    }

    void showProgress(const std::string& stage, int percentage)
    {
        m_mode = Mode::Busy;
        m_icon->setIconName("system-software-install");
        m_title->setText(tr("Installing"));
        if (!stage.empty())
            m_message->setText(statusForPackageManager(stage));
        m_progress->setVisible(true);
        m_progress->setIndeterminate(percentage < 0);
        if (percentage >= 0)
            m_progress->setValue(percentage / 100.0);
        m_action->setText(tr("Cancel"));
        m_action->setVisible(true);
        m_details->setVisible(false);
        requestLayout();
        requestPaint();
    }

    void showError(const std::string& message, bool hasDetails)
    {
        m_mode = Mode::Error;
        m_icon->setIconName("dialog-error");
        m_title->setText(tr("Could not continue"));
        m_message->setText(message);
        m_stage->setText({});
        m_progress->setVisible(false);
        m_action->setVisible(false);
        m_details->setVisible(hasDetails);
        requestLayout();
        requestPaint();
    }

    void showSuccess()
    {
        m_mode = Mode::Success;
        m_icon->setIconName("emblem-ok");
        m_title->setText(tr("Installation complete"));
        m_message->setText(tr("The package was installed successfully."));
        m_stage->setText({});
        m_progress->setVisible(false);
        m_action->setText(tr("Done"));
        m_action->setVisible(true);
        m_details->setVisible(false);
        requestLayout();
        requestPaint();
    }

    void setCanDrop(bool canDrop) { m_canDrop = canDrop; }

    bool canDrop(const DropOffer& offer) const override
    {
        const DragSource* source = offer.localSource();
        return m_canDrop && source && source->context == &m_owner
            && offer.hasMimeType("application/x-lingmo-package-installer");
    }

    void dragEnter(const DragEvent&) override
    {
        m_dragHover = true;
        m_icon->setIconName("folder-open");
        requestPaint();
    }

    uint32_t dragUpdate(const DragEvent&) override
    {
        return m_canDrop ? CopyAction : NoAction;
    }

    void dragLeave() override
    {
        m_dragHover = false;
        m_icon->setIconName(m_mode == Mode::Ready ? "folder" : "system-software-install");
        requestPaint();
    }

    uint32_t drop(const DragEvent&) override
    {
        m_dragHover = false;
        m_icon->setIconName("folder");
        requestPaint();
        m_owner.packageDropRequested();
        return CopyAction;
    }

protected:
    void layout() override
    {
        const RectF r = localRect();
        const double centre = r.width / 2;
        const double tileTop = 14;
        m_icon->setGeometry({centre - kFolderIconSize / 2,
                             tileTop + (kIconTileSize - kFolderIconSize) / 2,
                             kFolderIconSize, kFolderIconSize});
        m_title->setGeometry({24, tileTop + kIconTileSize + 12,
                              std::max(0.0, r.width - 48), 28});
        m_message->setGeometry({30, tileTop + kIconTileSize + 45,
                                std::max(0.0, r.width - 60), 42});
        m_stage->setGeometry({32, tileTop + kIconTileSize + 90,
                              std::max(0.0, r.width - 64), 34});
        m_progress->setGeometry({54, tileTop + kIconTileSize + 130,
                                 std::max(0.0, r.width - 108), 8});
        const double actionWidth = std::min(190.0, std::max(120.0, r.width - 100));
        const bool showBothActions = m_action->isVisible() && m_details->isVisible();
        const double actionTop = showBothActions ? r.height - 82 : r.height - 52;
        const double detailsTop = showBothActions ? r.height - 36 : r.height - 30;
        m_action->setGeometry({centre - actionWidth / 2, actionTop, actionWidth, 36});
        m_details->setGeometry({centre - 80, detailsTop, 160, 28});
    }

    void paint(Painter& painter) override
    {
        const Theme& theme = Theme::current();
        const double tileTop = 14;
        const RectF tile{(localRect().width - kIconTileSize) / 2, tileTop,
                         kIconTileSize, kIconTileSize};
        const Color fill = m_dragHover
            ? theme.highlight.withAlpha(theme.darkMode ? 0.18 : 0.10)
            : theme.secondBackground.withAlpha(0.48);
        painter.drawRoundedRect(tile, {18, 0, themeCorners()}, fill,
                                painter.borderWidth(m_dragHover ? 2 : 1),
                                (m_dragHover ? theme.highlight : theme.text).withAlpha(m_dragHover ? 0.58 : 0.1));
    }

private:
    enum class Mode { Ready, Loading, Busy, Error, Success };
    MainWindow& m_owner;
    ThemeIcon* m_icon;
    Label* m_title;
    Label* m_message;
    Label* m_stage;
    ProgressBar* m_progress;
    Button* m_action;
    Button* m_details;
    std::function<void()> m_actionHandler;
    Mode m_mode = Mode::Ready;
    bool m_canDrop = false;
    bool m_dragHover = false;
};

} // namespace

class MainWindow::Root final : public View
{
public:
    explicit Root(MainWindow& owner)
        : m_owner(owner), m_header(addChild<WindowHeader>(kHeaderHeight)),
          m_headerContent(m_header->setContent<HeaderContent>(
              [&owner](View* anchor) { owner.showMenu(anchor); })),
          m_appIcon(addChild<PackageIcon>(owner)),
          m_appName(addChild<Label>(tr("No package selected"))),
          m_appVersion(addChild<Label>()),
          m_appDescription(addChild<Label>()),
          m_packageSource(addChild<Label>()),
          m_detailsButton(addChild<Button>(tr("Package details"))),
          m_target(addChild<InstallTarget>(owner))
    {
        m_appName->setFontSize(21);
        m_appName->setWeight(650);
        m_appName->setTextAlignment(Label::TextAlignment::Center);
        m_appName->setElide(Label::Elide::Middle);
        m_appVersion->setFontSize(14);
        m_appVersion->setColor(ColorRecipe(ColorId::DisabledText));
        m_appVersion->setTextAlignment(Label::TextAlignment::Center);
        m_appDescription->setFontSize(13);
        m_appDescription->setColor(ColorRecipe(ColorId::DisabledText));
        m_appDescription->setTextAlignment(Label::TextAlignment::Center);
        m_appDescription->setWrap(true);
        m_appDescription->setMaximumLineCount(3);
        m_packageSource->setFontSize(12);
        m_packageSource->setColor(ColorRecipe(ColorId::DisabledText));
        m_packageSource->setTextAlignment(Label::TextAlignment::Center);
        m_packageSource->setWrap(true);
        m_packageSource->setMaximumLineCount(3);
        m_detailsButton->onClicked = [&owner] { owner.showDetails(); };
        m_detailsButton->setVisible(false);
        m_target->setName("installDropTarget");
        m_target->setAction(tr("Cancel"), [&owner] { owner.cancelInstall(); });
        m_target->showWelcome();
    }

    void setLocalMenuVisible(bool visible) { m_headerContent->setMenuVisible(visible); }

    void setPackage(const PackageInfo& package, const std::filesystem::path& path,
                    size_t index, size_t count)
    {
        m_packageFile = path;
        m_appIcon->setPackageIcon(package.package);
        m_appIcon->setCanDrag(true);
        m_appName->setText(package.package);
        m_appVersion->setText(tr("Version ") + package.version + " · " + package.architecture);
        const std::string description = package.description.empty() ? tr("No description is provided.")
                                                                      : package.description;
        m_appDescription->setText(description);
        std::string source = package.source.empty() ? tr("Local package · source not verified")
                                                    : tr("Source: ") + package.source;
        std::error_code fileError;
        const uint64_t fileSize = std::filesystem::file_size(path, fileError);
        source += "\n" + tr("File size: ") + (fileError ? tr("Unavailable") : formatSize(fileSize));
        if (count > 1)
            source += "\n" + Lingmo::I18n::format(tr("Package {current} of {total}"),
                {{"current", std::to_string(index + 1)}, {"total", std::to_string(count)}});
        m_packageSource->setText(source);
        m_target->setCanDrop(true);
        m_target->showReady();
        m_detailsButton->setVisible(true);
        if (window())
            window()->requestLayout();
    }

    void setWelcome()
    {
        m_packageFile.clear();
        m_appIcon->setPackageIcon("application-x-deb");
        m_appIcon->setCanDrag(false);
        m_appName->setText(tr("No package selected"));
        m_appVersion->setText({});
        m_appDescription->setText({});
        m_packageSource->setText({});
        m_detailsButton->setVisible(false);
        m_target->setCanDrop(false);
        m_target->showWelcome();
        requestLayout();
    }

    void showLoading(const std::string& text)
    {
        m_appIcon->setCanDrag(false);
        m_target->setCanDrop(false);
        m_target->showLoading(text);
    }

    void showError(const std::string& message, bool details)
    {
        m_appIcon->setCanDrag(false);
        m_target->setCanDrop(false);
        m_target->showError(message, details);
    }

    void showProgress(const std::string& stage, int percentage)
    {
        m_appIcon->setCanDrag(false);
        m_target->setCanDrop(false);
        m_target->showProgress(stage, percentage);
    }

    void showSuccess()
    {
        m_appIcon->setCanDrag(false);
        m_target->setCanDrop(false);
        m_target->showSuccess();
    }

    void setTargetAction(std::string label, std::function<void()> handler)
    {
        m_target->setAction(std::move(label), std::move(handler));
    }

protected:
    void layout() override
    {
        const RectF r = localRect();
        m_header->setGeometry({0, 0, r.width, kHeaderHeight});
        const double packagePaneWidth = r.width / 2;
        const double packageContentWidth = std::max(0.0, packagePaneWidth - 92);
        const double packageContentLeft = (packagePaneWidth - packageContentWidth) / 2;
        const double packageTileTop = kContentTop + 8;
        m_appIcon->setGeometry({(packagePaneWidth - 116) / 2,
                                packageTileTop + (kIconTileSize - 116) / 2, 116, 116});
        const double packageTextTop = packageTileTop + kIconTileSize + 14;
        m_appName->setGeometry({packageContentLeft, packageTextTop, packageContentWidth, 30});
        m_appVersion->setGeometry({packageContentLeft, packageTextTop + 32, packageContentWidth, 24});
        m_appDescription->setGeometry({packageContentLeft, packageTextTop + 60, packageContentWidth, 48});
        m_packageSource->setGeometry({packageContentLeft, packageTextTop + 112, packageContentWidth, 48});
        const double detailsWidth = std::min(190.0, packageContentWidth);
        m_detailsButton->setGeometry({(packagePaneWidth - detailsWidth) / 2,
                                      packageTextTop + 168, detailsWidth, 36});
        const double targetLeft = packagePaneWidth + 12;
        m_target->setGeometry({targetLeft, kContentTop,
                               std::max(0.0, r.width - targetLeft - 12),
                               std::max(0.0, r.height - kContentTop - 20)});
        if (Window* ownerWindow = window()) {
            ownerWindow->setBackgroundColor(Theme::current().background);
            ownerWindow->setBackgroundGlass(std::nullopt);
        }
    }

    void paint(Painter& painter) override
    {
        const Theme& theme = Theme::current();
        const RectF bounds = localRect();
        const double packagePaneWidth = bounds.width / 2;
        painter.fillRect(bounds, theme.background);
        const RectF packageTile{(packagePaneWidth - kIconTileSize) / 2,
                                kContentTop + 8, kIconTileSize, kIconTileSize};
        painter.drawRoundedRect(packageTile, {18, 0, themeCorners()},
                                theme.secondBackground.withAlpha(0.48),
                                painter.borderWidth(1), theme.text.withAlpha(0.1));
    }

private:
    MainWindow& m_owner;
    WindowHeader* m_header;
    HeaderContent* m_headerContent;
    PackageIcon* m_appIcon;
    Label* m_appName;
    Label* m_appVersion;
    Label* m_appDescription;
    Label* m_packageSource;
    Button* m_detailsButton;
    InstallTarget* m_target;
    std::filesystem::path m_packageFile;
};

MainWindow::MainWindow(Application& application, std::vector<std::filesystem::path> packageFiles)
    : m_application(application), m_packageFiles(std::move(packageFiles)),
      m_installer(std::make_unique<InstallController>(application.loop()))
{
    m_window.setTitle(tr("Lingmo Package Installer"));
    m_window.setMinimumSize({700, 560});
    m_window.resize({860, 560});
    m_window.setRestoreId("lingmo-package-installer");
    m_window.setTranslucentSubpixelText(false);
    m_window.addShortcut(ControlModifier, key::letter('q'), [this] { m_application.quit(); });
    m_window.addShortcut(NoModifier, key::F1, [this] { showAbout(); });
    auto root = std::make_unique<Root>(*this);
    m_root = root.get();
    m_window.setContentView(std::move(root));
    m_window.onActiveChanged = [this] { if (m_root) m_root->requestPaint(); };
    m_installer->onProgress = [this](const std::string& stage, int percentage) {
        if (m_root)
            m_root->showProgress(stage, percentage);
    };
    m_installer->onFinished = [this](bool success, bool cancelled, std::string details) {
        m_operationPending = false;
        if (success) {
            m_root->showSuccess();
            m_root->setTargetAction(tr("Done"), [this] { openNextFile(); });
            return;
        }
        m_errorDetails = std::move(details);
        m_friendlyError = cancelled ? tr("Installation was interrupted.")
                                    : friendlyInstallError(m_errorDetails);
        m_root->showError(m_friendlyError, !m_errorDetails.empty());
        m_root->setTargetAction({}, {});
    };
    m_root->setTargetAction({}, {});
    if (application.sessionBus()) {
        m_globalMenu = std::make_unique<GlobalMenu>(application.sessionBus(),
            [this] { return menuEntries(); });
        m_menuAvailability = m_globalMenu->availabilityChanged.connect([this](bool available) {
            if (m_root)
                m_root->setLocalMenuVisible(!available);
        });
    }
    if (m_packageFiles.empty())
        m_root->setWelcome();
    else
        inspectCurrentFile();
}

MainWindow::~MainWindow() = default;

void MainWindow::show()
{
    m_window.show();
}

void MainWindow::openNextFile()
{
    ++m_currentFile;
    m_package.reset();
    if (m_currentFile >= m_packageFiles.size()) {
        m_application.quit();
        return;
    }
    inspectCurrentFile();
}

void MainWindow::inspectCurrentFile()
{
    m_package.reset();
    m_friendlyError.clear();
    m_errorDetails.clear();
    if (m_currentFile >= m_packageFiles.size()) {
        m_root->setWelcome();
        return;
    }

    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::canonical(m_packageFiles[m_currentFile], error);
    if (error || !std::filesystem::is_regular_file(canonical, error) || error
        || access(canonical.c_str(), R_OK) != 0) {
        setError(tr("This file is missing or cannot be read."),
                 error ? error.message() : std::strerror(errno));
        return;
    }
    m_packageFiles[m_currentFile] = canonical;
    m_root->showLoading(tr("Reading package information"));
    m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
    m_process->setEnvironment({"LC_ALL=C"});
    m_process->onFinished = [this](int code, bool crashed) {
        if (crashed || code != 0) {
            const std::string details = m_process->standardError().empty()
                ? m_process->standardOutput() : m_process->standardError();
            setError(tr("This does not appear to be a readable Debian package."), details);
            return;
        }
        ParseResult parsed = parseControlFields(m_process->standardOutput());
        if (!parsed.package) {
            setError(tr("The package information is incomplete or damaged."), std::move(parsed.error));
            return;
        }
        m_package = std::move(parsed.package);
        queryNativeArchitecture();
    };
    if (!m_process->start("dpkg-deb", {"--field", canonical.string()}))
        setError(tr("The Debian package tools are not available."), "Could not start dpkg-deb.");
}

void MainWindow::queryNativeArchitecture()
{
    m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
    m_process->setEnvironment({"LC_ALL=C"});
    m_process->onFinished = [this](int code, bool crashed) {
        if (crashed || code != 0 || m_process->standardOutput().empty()) {
            setError(tr("The system architecture could not be checked."), m_process->standardError());
            return;
        }
        m_nativeArchitecture = m_process->standardOutput();
        while (!m_nativeArchitecture.empty()
               && std::isspace(static_cast<unsigned char>(m_nativeArchitecture.back())))
            m_nativeArchitecture.pop_back();
        queryForeignArchitectures();
    };
    if (!m_process->start("dpkg", {"--print-architecture"}))
        setError(tr("The system architecture could not be checked."), "Could not start dpkg.");
}

void MainWindow::queryForeignArchitectures()
{
    m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
    m_process->setEnvironment({"LC_ALL=C"});
    m_process->onFinished = [this](int code, bool crashed) {
        if (crashed || code != 0) {
            setError(tr("The system architecture could not be checked."), m_process->standardError());
            return;
        }
        m_foreignArchitectures = lines(m_process->standardOutput());
        if (!architectureCompatible(m_package->architecture, m_nativeArchitecture, m_foreignArchitectures)) {
            setError(tr("This package is for a different system architecture."),
                tr("Package architecture: ") + m_package->architecture
                    + "\n" + tr("System architecture: ") + m_nativeArchitecture);
            return;
        }
        queryInstalledPackage();
    };
    if (!m_process->start("dpkg", {"--print-foreign-architectures"}))
        setError(tr("The system architecture could not be checked."), "Could not start dpkg.");
}

void MainWindow::queryInstalledPackage()
{
    m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
    m_process->setEnvironment({"LC_ALL=C"});
    const std::string format = "--showformat=${db:Status-Status}|${Version}";
    m_process->onFinished = [this](int code, bool crashed) {
        if (crashed) {
            setError(tr("The current package status could not be checked."), m_process->standardError());
            return;
        }
        if (code != 0) {
            m_currentInstalledVersion.clear();
            m_root->setPackage(*m_package, m_packageFiles[m_currentFile], m_currentFile, m_packageFiles.size());
            return;
        }
        const std::string output = m_process->standardOutput();
        const size_t separator = output.find('|');
        if (separator == std::string::npos || output.substr(0, separator) != "installed") {
            m_currentInstalledVersion.clear();
            m_root->setPackage(*m_package, m_packageFiles[m_currentFile], m_currentFile, m_packageFiles.size());
            return;
        }
        m_currentInstalledVersion = output.substr(separator + 1);
        compareInstalledVersion();
    };
    if (!m_process->start("dpkg-query", {"--show", format, m_package->package}))
        setError(tr("The current package status could not be checked."), "Could not start dpkg-query.");
}

void MainWindow::compareInstalledVersion()
{
    const auto compare = [this](std::string relation, std::function<void(bool)> done) {
        m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
        m_process->setEnvironment({"LC_ALL=C"});
        m_process->onFinished = [this, done = std::move(done)](int code, bool crashed) {
            if (crashed || code > 1) {
                setError(tr("The installed package version could not be checked."),
                         m_process->standardError());
                return;
            }
            done(code == 0);
        };
        if (!m_process->start("dpkg", {"--compare-versions", m_currentInstalledVersion,
                                      std::move(relation), m_package->version}))
            setError(tr("The installed package version could not be checked."), "Could not start dpkg.");
    };

    compare("eq", [this](bool equal) {
        if (equal) {
            setError(tr("This package is already installed."),
                tr("Installed version: ") + m_currentInstalledVersion);
            return;
        }
        m_process = std::make_unique<Lingmo::base::Process>(&m_application.loop());
        m_process->setEnvironment({"LC_ALL=C"});
        m_process->onFinished = [this](int code, bool crashed) {
            if (crashed || code > 1) {
                setError(tr("The installed package version could not be checked."),
                         m_process->standardError());
                return;
            }
            if (code == 0) {
                setError(tr("A newer version of this package is already installed."),
                    tr("Installed version: ") + m_currentInstalledVersion);
                return;
            }
            m_root->setPackage(*m_package, m_packageFiles[m_currentFile], m_currentFile, m_packageFiles.size());
        };
        if (!m_process->start("dpkg", {"--compare-versions", m_currentInstalledVersion,
                                      "gt", m_package->version}))
            setError(tr("The installed package version could not be checked."), "Could not start dpkg.");
    });
}

void MainWindow::packageDropRequested()
{
    if (!m_package || m_operationPending || m_currentFile >= m_packageFiles.size())
        return;
    m_confirmation = std::make_unique<ConfirmDialog>(&m_window, tr("Install this package?"),
        Lingmo::I18n::format(tr("Install {name} version {version}? The system package service will resolve dependencies and ask before making system changes. A local package may not have a verifiable repository signature."),
            {{"name", m_package->package}, {"version", m_package->version}}));
    m_confirmation->setConfirmText(tr("Install"));
    m_confirmation->setCancelText(tr("Cancel"));
    m_confirmation->onAccepted = [this] { startInstall(); };
    m_confirmation->onRejected = [this] {
        if (m_root)
            m_root->setPackage(*m_package, m_packageFiles[m_currentFile], m_currentFile, m_packageFiles.size());
    };
    m_confirmation->show();
}

void MainWindow::startInstall()
{
    if (!m_package || m_operationPending || m_currentFile >= m_packageFiles.size())
        return;
    m_operationPending = true;
    m_root->showProgress({}, -1);
    if (!m_installer->start(m_packageFiles[m_currentFile])) {
        m_operationPending = false;
        setError(tr("The system package service could not be started."),
                 tr("Make sure PackageKit is running and accessible over the system bus."));
    }
}

void MainWindow::cancelInstall()
{
    if (m_installer && m_installer->isRunning()) {
        m_root->showProgress(tr("Cancelling installation"), -1);
        m_installer->cancel();
    }
}

void MainWindow::setError(std::string friendly, std::string details)
{
    m_friendlyError = std::move(friendly);
    m_errorDetails = std::move(details);
    if (m_root) {
        m_root->showError(m_friendlyError, !m_errorDetails.empty());
        m_root->setTargetAction(tr("Done"), [this] { openNextFile(); });
    }
}

void MainWindow::showMenu(View* anchor)
{
    m_localMenu = std::make_unique<Menu>();
    auto* app = m_localMenu->addSubmenu(tr("Package Installer"));
    app->addItem(tr("Quit"), [this] { m_application.quit(); }, "Ctrl+Q");
    auto* help = m_localMenu->addSubmenu(tr("Help"));
    help->addItem(tr("About Lingmo Package Installer"), [this] { showAbout(); }, "F1");
    m_localMenu->popupFor(anchor);
}

void MainWindow::showDetails()
{
    std::string details = m_errorDetails;
    if (m_package) {
        std::error_code error;
        const uint64_t fileSize = std::filesystem::file_size(m_packageFiles[m_currentFile], error);
        if (details.empty())
            details = tr("Package information");
        details += "\n\n" + tr("Name: ") + m_package->package
            + "\n" + tr("Version: ") + m_package->version
            + "\n" + tr("Architecture: ") + m_package->architecture
            + "\n" + tr("Maintainer: ") + (m_package->maintainer.empty() ? tr("Not provided") : m_package->maintainer)
            + "\n" + tr("Source: ") + (m_package->source.empty() ? tr("Local package; repository signature not available")
                                                                  : m_package->source)
            + "\n" + tr("Package file size: ") + (error ? tr("Unavailable") : formatSize(fileSize))
            + "\n" + tr("Installed size: ") + formatSize(m_package->installedSizeBytes);
        if (!m_package->preDepends.empty())
            details += "\n" + tr("Pre-dependencies: ") + m_package->preDepends;
        if (!m_package->depends.empty())
            details += "\n" + tr("Dependencies: ") + m_package->depends;
        if (!m_package->conflicts.empty())
            details += "\n" + tr("Conflicts: ") + m_package->conflicts;
    }
    if (details.empty())
        return;
    auto dialog = std::make_unique<DialogWindow>(&m_window, tr("Installation details"), 560);
    auto* detail = dialog->setContent<Label>(std::move(details));
    detail->setWrap(true);
    detail->setMaximumLineCount(40);
    m_detailsDialog = std::move(dialog);
    m_detailsDialog->show();
}

void MainWindow::showAbout()
{
    if (!m_about) {
        m_about = std::make_unique<AboutDialog>(&m_window);
        m_about->setName(tr("Lingmo Package Installer"));
        m_about->setDescription(tr("Install local Debian software packages with the Lingmo desktop."));
        m_about->setIconSource("lingmo-package-installer");
    }
    m_about->show();
}

std::vector<GlobalMenu::Entry> MainWindow::menuEntries()
{
    return {
        {tr("Package Installer"), {}, {
            {tr("Quit"), [this] { m_application.quit(); }, {}, "Ctrl+Q"},
        }, {}},
        {tr("Help"), {}, {
            {tr("About Lingmo Package Installer"), [this] { showAbout(); }, {}, "F1"},
        }, {}},
    };
}

} // namespace PackageInstaller
