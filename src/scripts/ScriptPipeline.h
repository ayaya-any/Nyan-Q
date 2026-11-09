#pragma once

#include "core/Types.h"
#include "scripts/ScriptEngine.h"
#include "scripts/ScriptTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

class QThread;

namespace nq::scripts {

// Runs a batch of scripts away from the UI thread. The worker is a dedicated
// QThread per run; a fresh QuickJS runtime is created for each script.
class ScriptPipeline : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString requestId;
        QString profileId;
        QString profileName;
        QString rawYaml;
        cq::AppSettings settings;
        QVector<ScriptStep> steps;
        ScriptSettings scriptSettings;
        bool applyClientSettings = true;
        bool stopOnError = true;
    };

    explicit ScriptPipeline(QObject* parent = nullptr);
    ~ScriptPipeline() override;

    bool run(const Request& request);
    void cancel();
    void shutdown();

    bool isBusy() const { return m_busy; }
    QString currentRequestId() const { return m_currentRequestId; }

    // Synchronous worker entry point. Public for deterministic tests; the
    // application always goes through run().
    static ScriptPipelineResult process(const Request& request, ScriptEngine::Control* control);

signals:
    void started(const QString& requestId);
    void finished(const nq::scripts::ScriptPipelineResult& result);
    void busyChanged(bool busy);

private:
    void handleResult(quint64 generation, const ScriptPipelineResult& result);

    QThread* m_thread = nullptr;
    quint64 m_generation = 0;
    bool m_busy = false;
    QString m_currentRequestId;
    ScriptEngine::Control m_control;
};

}  // namespace nq::scripts

Q_DECLARE_METATYPE(nq::scripts::ScriptPipeline::Request)
