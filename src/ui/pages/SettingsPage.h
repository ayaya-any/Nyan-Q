#pragma once

#include <QWidget>

class ClashController;
class M3Button;
class M3Pill;
class M3SegmentedControl;
class M3SettingRow;
class M3Switch;
class PageHeader;
class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();
    void apply();

private:
    void buildUi();
    void wire();
    void refreshFromSettings();
    void markDirty();
    void updateDirtyUi();
    void updateCoreInfo();
    void browseCore();
    void openDataDirectory();

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    M3Button* m_save = nullptr;
    M3Pill* m_dirtyPill = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_sections = nullptr;

    M3SegmentedControl* m_theme = nullptr;
    M3Switch* m_allowLan = nullptr;
    M3Switch* m_ipv6 = nullptr;
    M3Switch* m_tun = nullptr;
    QLineEdit* m_mixedPort = nullptr;
    QLineEdit* m_controllerPort = nullptr;
    M3Switch* m_autoLaunch = nullptr;
    M3Switch* m_startMinimized = nullptr;
    M3Switch* m_closeToTray = nullptr;
    M3SegmentedControl* m_updateInterval = nullptr;
    QLineEdit* m_delayUrl = nullptr;
    QLineEdit* m_corePath = nullptr;
    M3SegmentedControl* m_logLevel = nullptr;
    M3SettingRow* m_corePathRow = nullptr;
    M3SettingRow* m_dataRow = nullptr;
    M3Pill* m_coreStatusPill = nullptr;
    M3Pill* m_aboutKernelPill = nullptr;
    M3Button* m_restartCore = nullptr;
    bool m_dirty = false;
    bool m_syncing = false;
};
