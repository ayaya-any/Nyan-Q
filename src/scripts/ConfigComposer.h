#pragma once

#include "core/Types.h"
#include "scripts/MihomoConfigValidator.h"
#include "scripts/ScriptPipeline.h"
#include "scripts/ScriptTypes.h"

#include <QObject>
#include <QString>

#include <optional>

namespace nq::scripts {

// Composes the final runtime configuration:
//   raw profile -> client overrides -> JS scripts -> Mihomo -t -> atomic commit
// A failed script or a rejected config never replaces the last good runtime
// file. Only one build runs at a time; concurrent requests are coalesced.
class ConfigComposer : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString requestId;
        QString profileId;
        QString profileName;
        QByteArray rawConfig;
        cq::AppSettings settings;
        QVector<ScriptStep> steps;
        ScriptSettings scriptSettings;
        QString corePath;
        QString dataDirectory;
        QString candidatePath;
        QString outputPath;
        int validationTimeoutMs = 20000;
        bool validate = true;
        bool stopOnError = true;
    };

    struct Result {
        bool ok = false;
        bool canceled = false;
        bool usedScripts = false;
        bool validated = false;
        QString requestId;
        QString profileId;
        QString profileName;
        QString error;
        QString outputPath;
        ScriptPipelineResult pipeline;
    };

    explicit ConfigComposer(QObject* parent = nullptr);
    ~ConfigComposer() override;

    bool build(const Request& request);
    void cancel();
    void shutdown();

    bool isBusy() const { return m_busy; }
    bool hasPending() const { return m_pending.has_value(); }
    const ScriptPipelineResult& lastPipeline() const { return m_lastPipeline; }
    const Result& lastResult() const { return m_lastResult; }

signals:
    void started(const QString& requestId);
    void finished(const nq::scripts::ConfigComposer::Result& result);
    void busyChanged(bool busy);

private:
    void startNext();
    void startRequest(const Request& request);
    void handlePipelineFinished(const ScriptPipelineResult& result);
    void handleValidationFinished(bool valid, const QString& message);
    void finish(const Result& result);
    void removeCandidate();

    ScriptPipeline m_pipeline;
    MihomoConfigValidator m_validator;
    std::optional<Request> m_pending;
    Request m_current;
    Result m_lastResult;
    ScriptPipelineResult m_lastPipeline;
    QString m_candidatePath;
    bool m_busy = false;
    bool m_active = false;
    bool m_shuttingDown = false;
    quint64 m_generation = 0;
};

}  // namespace nq::scripts

Q_DECLARE_METATYPE(nq::scripts::ConfigComposer::Request)
Q_DECLARE_METATYPE(nq::scripts::ConfigComposer::Result)
