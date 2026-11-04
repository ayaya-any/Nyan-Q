#pragma once

#include "core/MihomoApi.h"
#include "core/MihomoProcess.h"
#include "core/NetworkProbe.h"
#include "core/ProfileStore.h"
#include "core/SettingsStore.h"
#include "core/SystemProxy.h"
#include "core/Types.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>

class ClashController final : public QObject {
    Q_OBJECT

public:
    explicit ClashController(QObject* parent = nullptr);
    ~ClashController() override;

    void initialize(bool demo, bool ephemeral = false);

    bool isDemo() const { return m_demo; }
    bool isInitialized() const { return m_initialized; }
    cq::CoreState state() const { return m_state; }
    bool isRunning() const { return m_state == cq::CoreState::Running; }
    bool isTransitioning() const
    {
        return m_state == cq::CoreState::Starting || m_state == cq::CoreState::Stopping;
    }

    const cq::AppSettings& settings() const { return m_settings; }
    cq::AppSettings& mutableSettings() { return m_settings; }
    ProfileStore* profileStore() { return &m_profiles; }
    const ProfileStore* profileStore() const { return &m_profiles; }
    NetworkProbe* networkProbe() { return &m_networkProbe; }
    const NetworkProbe* networkProbe() const { return &m_networkProbe; }
    cq::MemoryUsage memoryUsage() const { return m_memoryUsage; }

    cq::SessionStats stats() const { return m_stats; }
    const QVector<cq::ProxyGroup>& proxyGroups() const { return m_proxyGroups; }
    const QVector<cq::ConnectionInfo>& connections() const { return m_connections; }
    const QVector<cq::RuleInfo>& rules() const { return m_rules; }
    const QVector<cq::LogEntry>& logs() const { return m_logHistory; }

    QString coreVersion() const { return m_coreVersion; }
    QString mode() const { return m_mode; }
    QString lastError() const { return m_lastError; }
    QString controllerEndpoint() const;
    QString coreBinaryPath() const { return m_coreBinary; }
    QString dataDirectory() const;
    QString activeProfileName() const;
    bool systemProxyEnabled() const { return m_systemProxyEnabled; }
    bool tunEnabled() const { return m_settings.tunEnabled; }
    bool isElevated() const;
    qint64 connectedSeconds() const { return m_stats.connectedSeconds; }
    qint64 coreProcessId() const { return m_process.processId(); }

public slots:
    void start();
    void stop();
    void restart();
    void shutdown();

    bool updateSettings(const cq::AppSettings& settings, bool restartIfNeeded = true);
    void setMode(const QString& mode);
    void setTunEnabled(bool enabled);
    void setAllowLanEnabled(bool enabled);
    void setSystemProxy(bool enabled);

    void selectProxy(const QString& group, const QString& node);
    void testProxy(const QString& node);
    void testGroup(const QString& group);
    void closeConnection(const QString& id);
    void closeAllConnections();

    void importUrl(const QString& name, const QUrl& url);
    void importFile(const QString& path);
    void refreshProfile(const QString& id);
    void removeProfile(const QString& id);
    void activateProfile(const QString& id);

signals:
    void stateChanged(cq::CoreState state);
    void statsChanged(const cq::SessionStats& stats);
    void proxyGroupsChanged(const QVector<cq::ProxyGroup>& groups);
    void connectionsChanged(const QVector<cq::ConnectionInfo>& connections);
    void rulesChanged(const QVector<cq::RuleInfo>& rules);
    void modeChanged(const QString& mode);
    void coreVersionChanged(const QString& version);
    void systemProxyChanged(bool enabled);
    void tunChanged(bool enabled);
    void settingsChanged();
    void activeProfileChanged();
    void memoryUsageChanged(const cq::MemoryUsage& usage);
    void delayUpdated(const QString& node, int delayMs);
    void logAdded(const cq::LogEntry& entry);
    void notification(const QString& message, bool error);

private:
    void wire();
    void enterDemo();
    void startDemo();
    void stopDemo();
    void populateDemoData();
    void demoTick();
    void setState(cq::CoreState state);
    void log(cq::LogLevel level, const QString& message);
    void logCoreLine(const QString& raw);
    void appendLogFile(const cq::LogEntry& entry);
    void handleCoreReady(const QString& version);
    void handleProcessFinished(int exitCode, bool crashed);
    void handleProcessFailure(const QString& message);
    void handleStartupTimeout();
    void failStartup(const QString& message);
    void updateStatsTick();
    void requestStop(bool restartAfter);
    void clearTransientState();
    void syncSystemProxy(bool enabled);
    void maybeAutoUpdateSubscription();
    void updateMemoryUsage();
    void scheduleConfigReload();
    void performConfigReload();
    void handleConfigReloadFinished(bool success, const QString& error);
    bool prepareRuntimeConfig(QString* error);
    bool validateSettings(const cq::AppSettings& settings, QString* error) const;
    static cq::LogLevel parseLogLevel(const QString& raw);
    static QString cleanCoreMessage(const QString& raw);

    SettingsStore m_settingsStore;
    cq::AppSettings m_settings;
    ProfileStore m_profiles;
    MihomoProcess m_process;
    MihomoApi m_api;
    NetworkProbe m_networkProbe;
    SystemProxy m_systemProxy;

    QTimer m_probeTimer;
    QTimer m_startupTimer;
    QTimer m_statsTimer;
    QTimer m_subscriptionTimer;
    QTimer m_demoTimer;
    QTimer m_configReloadTimer;
    QTimer m_memoryTimer;

    cq::CoreState m_state = cq::CoreState::Stopped;
    cq::SessionStats m_stats;
    QVector<cq::ProxyGroup> m_proxyGroups;
    QVector<cq::ConnectionInfo> m_connections;
    QVector<cq::RuleInfo> m_rules;
    QVector<cq::LogEntry> m_logHistory;
    cq::MemoryUsage m_memoryUsage;

    QString m_mode = QStringLiteral("rule");
    QString m_coreVersion;
    QString m_lastError;
    QString m_lastProbeError;
    QString m_coreBinary;
    QString m_activeConfigPath;
    QDateTime m_connectedAt;
    QDateTime m_lastTrafficAt;
    bool m_demo = false;
    bool m_ephemeral = false;
    bool m_initialized = false;
    bool m_shuttingDown = false;
    bool m_restartPending = false;
    bool m_systemProxyEnabled = false;
    bool m_pendingSystemProxy = false;
    bool m_restoreSystemProxyAfterRestart = false;
    bool m_configReloadInFlight = false;
    bool m_configReloadQueued = false;
    bool m_configReloadWhenReady = false;
    int m_demoTick = 0;
    QElapsedTimer m_lastApiErrorAt;
    QString m_lastApiError;
};
