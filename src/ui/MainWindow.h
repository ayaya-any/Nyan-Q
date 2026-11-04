#pragma once

#include "core/Types.h"

#include <QMainWindow>

class ClashController;
class ConfigEditorView;
class ConnectionsPage;
class DashboardPage;
class LogsPage;
class M3Snackbar;
class ProfilesPage;
class ProxiesPage;
class RulesPage;
class SettingsPage;
class QAction;
class QActionGroup;
class QStackedWidget;
class QSystemTrayIcon;
class QMenu;
class Sidebar;
class TitleBar;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(ClashController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setCurrentPage(int index);
    void showFromTray();
    bool hasTrayIcon() const;
    void openConfigEditor(const QString& profileId);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void buildUi();
    void wire();
    void setupTray();
    void setupShortcuts();
    void applyTheme();
    void updateWindowStatus();
    void updateTrayState();
    void updateTrayTooltip();
    void toggleMaximize();
    void saveWindowGeometry();
    void restoreWindowGeometry();
    void enableResizeFrame();
    void setupResizeHandles();
    void layoutResizeHandles();
    void closeConfigEditor();
    bool ensureEditorClosable();

    ClashController* m_controller = nullptr;
    TitleBar* m_titleBar = nullptr;
    QStackedWidget* m_bodyStack = nullptr;
    ConfigEditorView* m_editorView = nullptr;
    int m_editorReturnPage = 0;
    Sidebar* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    DashboardPage* m_dashboard = nullptr;
    ProxiesPage* m_proxies = nullptr;
    ProfilesPage* m_profiles = nullptr;
    ConnectionsPage* m_connections = nullptr;
    RulesPage* m_rules = nullptr;
    LogsPage* m_logs = nullptr;
    SettingsPage* m_settings = nullptr;
    M3Snackbar* m_snackbar = nullptr;
    QWidget* m_resizeHandles[8] = {};

    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;

    QAction* m_traySystemProxy = nullptr;
    QAction* m_trayTun = nullptr;
    QActionGroup* m_trayModeGroup = nullptr;
    QAction* m_trayRuleMode = nullptr;
    QAction* m_trayGlobalMode = nullptr;
    QAction* m_trayDirectMode = nullptr;

    bool m_forceQuit = false;
    bool m_trayHintShown = false;
    bool m_centered = false;
    bool m_themeRefreshedOnShow = false;
    bool m_resizeFrameEnabled = false;
};
