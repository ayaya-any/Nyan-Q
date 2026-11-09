#include "scripts/ConfigComposer.h"

#include <QFile>
#include <QFileInfo>

namespace nq::scripts {

ConfigComposer::ConfigComposer(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ConfigComposer::Result>("nq::scripts::ConfigComposer::Result");
    connect(&m_pipeline, &ScriptPipeline::finished, this,
            &ConfigComposer::handlePipelineFinished);
    connect(&m_validator, &MihomoConfigValidator::finished, this,
            &ConfigComposer::handleValidationFinished);
}

ConfigComposer::~ConfigComposer()
{
    shutdown();
}

bool ConfigComposer::build(const Request& request)
{
    if (m_shuttingDown)
        return false;
    if (m_busy) {
        m_pending = request;
        return true;
    }
    startRequest(request);
    return true;
}

void ConfigComposer::startRequest(const Request& request)
{
    m_current = request;
    m_candidatePath = request.candidatePath;
    m_active = true;
    m_busy = true;
    ++m_generation;
    emit busyChanged(true);
    emit started(request.requestId);

    ScriptPipeline::Request pipelineRequest;
    pipelineRequest.requestId = request.requestId;
    pipelineRequest.profileId = request.profileId;
    pipelineRequest.profileName = request.profileName;
    pipelineRequest.rawYaml = QString::fromUtf8(request.rawConfig);
    pipelineRequest.settings = request.settings;
    pipelineRequest.steps = request.steps;
    pipelineRequest.scriptSettings = request.scriptSettings;
    pipelineRequest.applyClientSettings = true;
    pipelineRequest.stopOnError = request.stopOnError;

    if (!m_pipeline.run(pipelineRequest)) {
        Result result;
        result.requestId = request.requestId;
        result.profileId = request.profileId;
        result.profileName = request.profileName;
        result.error = QStringLiteral("脚本流水线忙，无法开始配置生成");
        finish(result);
    }
}

void ConfigComposer::handlePipelineFinished(const ScriptPipelineResult& result)
{
    if (!m_active || m_shuttingDown)
        return;

    m_lastPipeline = result;
    Result composed;
    composed.requestId = m_current.requestId;
    composed.profileId = m_current.profileId;
    composed.profileName = m_current.profileName;
    composed.usedScripts = result.usedScripts;
    composed.pipeline = result;

    if (result.canceled) {
        composed.canceled = true;
        composed.error = QStringLiteral("配置生成已取消");
        finish(composed);
        return;
    }
    if (!result.ok) {
        composed.error = result.error;
        finish(composed);
        return;
    }
    if (m_candidatePath.isEmpty()) {
        composed.error = QStringLiteral("没有指定候选配置路径");
        finish(composed);
        return;
    }

    QString writeError;
    if (!writeFileAtomic(m_candidatePath, result.finalYaml.toUtf8(), &writeError)) {
        composed.error = QStringLiteral("无法写入候选配置：%1").arg(writeError);
        finish(composed);
        return;
    }

    if (m_current.validate) {
        if (m_current.corePath.trimmed().isEmpty()) {
            removeCandidate();
            composed.error = QStringLiteral("未找到 Mihomo 核心，无法校验最终配置");
            finish(composed);
            return;
        }
        m_validator.validate(m_current.corePath, m_candidatePath, m_current.dataDirectory,
                             m_current.validationTimeoutMs);
        return;
    }

    QFile candidate(m_candidatePath);
    if (!candidate.open(QIODevice::ReadOnly)) {
        composed.error = QStringLiteral("无法读取候选配置");
        finish(composed);
        return;
    }
    const QByteArray content = candidate.readAll();
    candidate.close();
    if (!writeFileAtomic(m_current.outputPath, content, &writeError)) {
        composed.error = QStringLiteral("无法提交运行时配置：%1").arg(writeError);
        finish(composed);
        return;
    }
    removeCandidate();
    composed.ok = true;
    composed.validated = false;
    composed.outputPath = m_current.outputPath;
    finish(composed);
}

void ConfigComposer::handleValidationFinished(bool valid, const QString& message)
{
    if (!m_active || m_shuttingDown)
        return;

    Result composed;
    composed.requestId = m_current.requestId;
    composed.profileId = m_current.profileId;
    composed.profileName = m_current.profileName;
    composed.usedScripts = m_lastPipeline.usedScripts;
    composed.pipeline = m_lastPipeline;

    if (!valid) {
        removeCandidate();
        composed.error = message.isEmpty() ? QStringLiteral("Mihomo 拒绝了最终配置") : message;
        finish(composed);
        return;
    }

    QFile candidate(m_candidatePath);
    if (!candidate.open(QIODevice::ReadOnly)) {
        composed.error = QStringLiteral("无法读取通过校验的候选配置");
        removeCandidate();
        finish(composed);
        return;
    }
    const QByteArray content = candidate.readAll();
    candidate.close();

    QString writeError;
    if (!writeFileAtomic(m_current.outputPath, content, &writeError)) {
        composed.error = QStringLiteral("无法提交运行时配置：%1").arg(writeError);
        removeCandidate();
        finish(composed);
        return;
    }
    removeCandidate();
    composed.ok = true;
    composed.validated = true;
    composed.outputPath = m_current.outputPath;
    finish(composed);
}

void ConfigComposer::finish(const Result& result)
{
    m_active = false;
    m_busy = false;
    m_lastResult = result;
    removeCandidate();

    std::optional<Request> next = std::move(m_pending);
    m_pending.reset();

    emit busyChanged(false);
    emit finished(result);

    if (next && !m_shuttingDown) {
        if (m_busy)
            m_pending = next;
        else
            startRequest(*next);
    }
}

void ConfigComposer::removeCandidate()
{
    if (m_candidatePath.isEmpty())
        return;
    QFile::remove(m_candidatePath);
}

void ConfigComposer::cancel()
{
    if (!m_busy)
        return;
    m_pending.reset();
    m_pipeline.cancel();
    m_validator.cancel();
}

void ConfigComposer::shutdown()
{
    m_shuttingDown = true;
    m_pending.reset();
    m_active = false;
    m_pipeline.shutdown();
    if (m_validator.isBusy())
        m_validator.cancel();
    removeCandidate();
    m_busy = false;
    m_candidatePath.clear();
}

}  // namespace nq::scripts
