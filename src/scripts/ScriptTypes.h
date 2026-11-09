#pragma once

#include "scripts/YamlDocument.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace nq::scripts {

enum class ScriptSourceKind { Local, Remote };
enum class ScriptTrustState { Local, Untrusted, Trusted, Pinned, PinMismatch };
enum class ScriptRunStatus { Success, Skipped, Failed, TimedOut, MemoryExceeded, Canceled };
enum class ScriptLogLevel { Debug, Info, Warning, Error };
enum class ScriptScope { Global, Profile };

struct ScriptVersion {
    QString id;
    QString sha256;
    qint64 size = 0;
    QDateTime savedAt;
    QString sourceUrl;
    QString note;
    bool current = false;
};

struct ScriptMeta {
    QString id;
    QString name;
    QString description;
    ScriptSourceKind kind = ScriptSourceKind::Local;
    QString sourcePath;
    QString url;
    QString fileName;
    bool enabled = true;
    int order = 0;
    bool autoUpdate = false;
    int updateIntervalHours = 24;
    QString etag;
    QString lastModified;
    QString clientSha256;
    QString expectedSha256;
    bool trusted = false;
    QString lastError;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime lastCheckedAt;
    QVector<ScriptVersion> versions;

    bool remote() const { return kind == ScriptSourceKind::Remote; }
};

struct ScriptBinding {
    QString scriptId;
    QString profileId;  // "*" identifies the global (all profiles) binding.
    bool enabled = true;
};

struct ScriptSettings {
    int timeoutMs = 10000;
    qint64 memoryLimitBytes = 256LL * 1024 * 1024;
    bool stopOnError = true;
    bool autoUpdateEnabled = false;
    int logRetention = 1000;

    bool isValid() const
    {
        return timeoutMs >= 100 && timeoutMs <= 120000 && memoryLimitBytes >= 8LL * 1024 * 1024 &&
               memoryLimitBytes <= 2048LL * 1024 * 1024 && logRetention >= 100 &&
               logRetention <= 10000;
    }
};

struct ScriptLogEntry {
    QDateTime time;
    ScriptLogLevel level = ScriptLogLevel::Info;
    QString scriptId;
    QString scriptName;
    QString message;
};

struct ScriptLogLine {
    ScriptLogLevel level = ScriptLogLevel::Info;
    QString message;
};

struct ScriptExecutionLimits {
    int timeoutMs = 10000;
    qint64 memoryLimitBytes = 256LL * 1024 * 1024;
    int maxLogEntries = 1000;
    qint64 maxLogBytes = 1024LL * 1024;
    qint64 maxConfigBytes = 64LL * 1024 * 1024;
};

struct ScriptExecutionRequest {
    QString scriptId;
    QString scriptName;
    QString source;
    QString profileName;
    nq::yaml::Node config;
    ScriptExecutionLimits limits;
};

struct ScriptExecutionResult {
    bool ok = false;
    ScriptRunStatus status = ScriptRunStatus::Failed;
    QString errorName;
    QString errorMessage;
    QString stack;
    int line = 0;
    int column = 0;
    QVector<ScriptLogLine> logs;
    nq::yaml::Node config;
    qint64 durationMs = 0;
    bool changed = false;

    bool timedOut() const { return status == ScriptRunStatus::TimedOut; }
    bool memoryExceeded() const { return status == ScriptRunStatus::MemoryExceeded; }
    bool canceled() const { return status == ScriptRunStatus::Canceled; }
};

struct ScriptStep {
    QString scriptId;
    QString name;
    QString source;
    ScriptScope scope = ScriptScope::Global;
    int order = 0;
    QString versionSha256;
    QString sourceUrl;
    QString integrityError;
};

struct ScriptStepResult {
    QString scriptId;
    QString name;
    ScriptScope scope = ScriptScope::Global;
    bool ok = false;
    bool skipped = false;
    ScriptRunStatus status = ScriptRunStatus::Failed;
    QString errorName;
    QString errorMessage;
    QString stack;
    int line = 0;
    int column = 0;
    qint64 durationMs = 0;
    QVector<ScriptLogLine> logs;
    bool configChanged = false;
    nq::yaml::DiffResult diff;
};

struct ScriptPipelineResult {
    bool ok = false;
    bool canceled = false;
    bool usedScripts = false;
    bool partial = false;
    QString error;
    QString profileId;
    QString profileName;
    QString originalYaml;
    QString baselineYaml;
    QString finalYaml;
    QVector<ScriptStepResult> steps;
    nq::yaml::DiffResult diff;
    qint64 durationMs = 0;
    QString failingScriptId;
    QString failingScriptName;
    QString failingVersionSha256;
};

struct ScriptUpdateResult {
    QString scriptId;
    QString scriptName;
    bool ok = false;
    bool notModified = false;
    bool skipped = false;
    bool httpError = false;
    bool manual = false;
    int httpStatus = 0;
    QString error;
    QString oldSha256;
    QString newSha256;
    QByteArray oldContent;
    QByteArray newContent;
    qint64 bytes = 0;
    QDateTime checkedAt;
};

QString sourceKindLabel(ScriptSourceKind kind);
QString trustStateLabel(ScriptTrustState state);
QString runStatusLabel(ScriptRunStatus status);
QString logLevelLabel(ScriptLogLevel level);

ScriptTrustState trustState(const ScriptMeta& script);
QString maskSecretUrl(const QString& url);

QByteArray sha256Hex(const QByteArray& data);
bool writeFileAtomic(const QString& path, const QByteArray& data, QString* error);
bool copyFileAtomic(const QString& sourcePath, const QString& destinationPath, QString* error);

}  // namespace nq::scripts

Q_DECLARE_METATYPE(nq::scripts::ScriptSourceKind)
Q_DECLARE_METATYPE(nq::scripts::ScriptTrustState)
Q_DECLARE_METATYPE(nq::scripts::ScriptRunStatus)
Q_DECLARE_METATYPE(nq::scripts::ScriptLogLevel)
Q_DECLARE_METATYPE(nq::scripts::ScriptScope)
Q_DECLARE_METATYPE(nq::scripts::ScriptVersion)
Q_DECLARE_METATYPE(nq::scripts::ScriptMeta)
Q_DECLARE_METATYPE(nq::scripts::ScriptBinding)
Q_DECLARE_METATYPE(nq::scripts::ScriptSettings)
Q_DECLARE_METATYPE(nq::scripts::ScriptLogEntry)
Q_DECLARE_METATYPE(nq::scripts::ScriptLogLine)
Q_DECLARE_METATYPE(nq::scripts::ScriptExecutionResult)
Q_DECLARE_METATYPE(nq::scripts::ScriptStep)
Q_DECLARE_METATYPE(nq::scripts::ScriptStepResult)
Q_DECLARE_METATYPE(nq::scripts::ScriptPipelineResult)
Q_DECLARE_METATYPE(nq::scripts::ScriptUpdateResult)
