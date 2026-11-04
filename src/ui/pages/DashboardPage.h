#pragma once

#include "core/Types.h"

#include <QHash>
#include <QWidget>

class ClashController;
class ConnectionListModel;
class ConnectionListView;
class M3Card;
class M3IconButton;
class M3IconLabel;
class M3Pill;
class M3ProgressBar;
class M3SegmentedControl;
class M3SettingRow;
class M3Switch;
class PageHeader;
class SiteLatencyTile;
class TrafficChart;
class QLabel;
class QScrollArea;
class QShowEvent;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();

signals:
    void navigateRequested(int index);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void wire();
    void updateState();
    void updateStats();
    void updateSettings();
    void updateNetworkExit();
    void updateSiteLatencies();
    void updateMemoryUsage();
    void updateActivity();
    void updateConfigCard();
    void updateChartFromStats();

    ClashController* m_controller = nullptr;
    QScrollArea* m_scroll = nullptr;
    PageHeader* m_header = nullptr;
    M3SegmentedControl* m_modeControl = nullptr;
    QLabel* m_localIpLabel = nullptr;
    M3SettingRow* m_sysProxyRow = nullptr;
    M3Switch* m_sysProxySwitch = nullptr;
    M3SettingRow* m_tunRow = nullptr;
    M3Switch* m_tunSwitch = nullptr;
    M3SettingRow* m_allowLanRow = nullptr;
    M3Switch* m_allowLanSwitch = nullptr;
    TrafficChart* m_chart = nullptr;
    M3IconLabel* m_downloadIcon = nullptr;
    M3IconLabel* m_uploadIcon = nullptr;
    QLabel* m_downloadSpeed = nullptr;
    QLabel* m_uploadSpeed = nullptr;
    QLabel* m_sessionTotals = nullptr;
    QLabel* m_sessionTime = nullptr;
    QLabel* m_appMemory = nullptr;
    QLabel* m_kernelMemory = nullptr;
    TrafficChart* m_memoryChart = nullptr;

    M3IconButton* m_exitRefresh = nullptr;
    M3Pill* m_exitCountryPill = nullptr;
    QLabel* m_exitIp = nullptr;
    QLabel* m_exitRegion = nullptr;
    QLabel* m_exitOrg = nullptr;

    ConnectionListModel* m_connectionModel = nullptr;
    ConnectionListView* m_connectionView = nullptr;
    M3Pill* m_connectionCountPill = nullptr;

    QLabel* m_configName = nullptr;
    M3Pill* m_configTypePill = nullptr;
    QLabel* m_configSource = nullptr;
    QLabel* m_configExpiry = nullptr;
    QLabel* m_configInterval = nullptr;
    QLabel* m_configFile = nullptr;
    QLabel* m_configUpdated = nullptr;
    QLabel* m_configUsage = nullptr;
    M3ProgressBar* m_configBar = nullptr;

    M3IconButton* m_siteRefresh = nullptr;
    QHash<QString, SiteLatencyTile*> m_siteTiles;
    qint64 m_lastSampleSecond = -1;
    bool m_initialScrollReset = false;
};
