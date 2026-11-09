#pragma once

#include "scripts/ScriptBindingManager.h"
#include "scripts/ScriptDownloader.h"
#include "scripts/ScriptLogManager.h"
#include "scripts/ScriptRepository.h"
#include "scripts/ScriptTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <optional>

namespace nq::scripts {

// Lifecycle and metadata owner for JavaScript override scripts. UI and the
// Mihomo pipeline talk to this object; they never touch QuickJS directly.
class ScriptManager : public QObject {
    Q_OBJECT

public:
    explicit ScriptManager(QObject* parent = nullptr);
    ~ScriptManager() override;

    bool initialize(const QString& scriptsDirectory, QString* error = nullptr);
    void shutdown();

    const QVector<ScriptMeta>& scripts() const;
    std::optional<ScriptMeta> find(const QString& id) const;
    const ScriptSettings& settings() const { return m_repository.settings(); }
    bool updateSettings(const ScriptSettings& settings, QString* error = nullptr);

    QString createLocalScript(const QString& name, const QString& description,
                              const QString& source, QString* error = nullptr);
    QString importLocalFile(const QString& path, const QString& name = {},
                            QString* error = nullptr);
    bool importRemote(const QString& name, const QUrl& url, bool trusted,
                      QString* error = nullptr);

    QByteArray content(const QString& id, QString* error = nullptr) const;
    QByteArray versionContent(const QString& id, const QString& versionId,
                              QString* error = nullptr) const;
    bool saveContent(const QString& id, const QByteArray& source, QString* error = nullptr);
    bool renameScript(const QString& id, const QString& name, QString* error = nullptr);
    bool removeScript(const QString& id, QString* error = nullptr);
    bool setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    bool setOrder(const QString& id, int order, QString* error = nullptr);
    bool moveScript(const QString& id, int delta, QString* error = nullptr);
    bool setAutoUpdate(const QString& id, bool enabled, QString* error = nullptr);
    bool setUpdateInterval(const QString& id, int hours, QString* error = nullptr);
    bool setExpectedSha256(const QString& id, const QString& hash, QString* error = nullptr);
    bool setTrusted(const QString& id, bool trusted, QString* error = nullptr);

    bool rollback(const QString& id, const QString& versionId, QString* error = nullptr);
    bool rollbackToPrevious(const QString& id, QString* error = nullptr);

    const ScriptBindingManager& bindings() const { return m_bindings; }
    bool setGlobalBinding(const QString& id, bool enabled, QString* error = nullptr);
    bool setProfileBinding(const QString& id, const QString& profileId, bool enabled,
                           QString* error = nullptr);
    void retainProfiles(const QStringList& profileIds, QString* error = nullptr);
    QVector<ScriptStep> resolveSteps(const QString& profileId) const;
    bool isScriptUsedByProfile(const QString& id, const QString& profileId) const;

    void checkForUpdate(const QString& id, bool manual);
    void checkAll(bool manual);
    void checkUpdatesForProfile(const QString& profileId, bool manual);
    void setActiveProfileId(const QString& profileId);
    bool isUpdating(const QString& id) const { return m_updateInFlight.contains(id); }
    bool isBusy() const { return !m_updateInFlight.isEmpty(); }

    ScriptLogManager& logs() { return m_logs; }
    const ScriptLogManager& logs() const { return m_logs; }
    void clearLogs();
    void recordPipelineResult(const ScriptPipelineResult& result, const QString& profileName);

    // Version verification: after an update, the controller confirms the
    // generated config or rolls the failing script back automatically.
    void confirmPendingVersions();
    void handlePipelineFailure(const ScriptPipelineResult& result);

signals:
    void changed();
    void scriptUpdated(const QString& id);
    void updateStarted(const QString& id, const QString& name);
    void updateFinished(const nq::scripts::ScriptUpdateResult& result);
    void operationSucceeded(const QString& message);
    void operationFailed(const QString& message);
    void busyChanged(bool busy);
    void logAdded(const nq::scripts::ScriptLogEntry& entry);

private:
    struct DownloadContext {
        QString scriptId;
        QString scriptName;
        QString previousVersionId;
        QByteArray previousContent;
        bool manual = false;
        bool import = false;
        QString importName;
        QUrl importUrl;
    };

    void handleDownloadFinished(const ScriptDownloadResult& result);
    void finishUpdate(const ScriptUpdateResult& result);
    QString addScriptWithBinding(ScriptMeta meta, const QByteArray& content, QString* error);
    void setBusy();
    void markError(const QString& id, const QString& message);
    bool saveCatalog(QString* error);
    bool syncBindings(QString* error);
    bool dueForCheck(const ScriptMeta& script) const;
    void maybeAutoCheck();
    void rollbackPending(const QString& scriptId);
    QString uniqueName(const QString& desired) const;
    void emitLogTail();

    ScriptRepository m_repository;
    ScriptBindingManager m_bindings;
    ScriptDownloader m_downloader;
    ScriptLogManager m_logs;
    QHash<QString, DownloadContext> m_downloads;
    QSet<QString> m_updateInFlight;
    QHash<QString, QString> m_pendingVerification;
    QTimer m_autoTimer;
    QString m_activeProfileId;
    bool m_initialized = false;
    bool m_shuttingDown = false;
    bool m_busy = false;
};

}  // namespace nq::scripts

Q_DECLARE_METATYPE(nq::scripts::ScriptManager*)
