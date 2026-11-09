#include "scripts/ScriptPipeline.h"

#include "core/RuntimeConfig.h"
#include "scripts/YamlDocument.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QStringList>
#include <QThread>

#include <utility>

namespace nq::scripts {
namespace {

QString profileError(const ScriptStepResult& step)
{
    QString message = QStringLiteral("脚本「%1」执行失败：%2")
                          .arg(step.name.isEmpty() ? step.scriptId : step.name,
                               step.errorMessage.isEmpty() ? QStringLiteral("未知错误")
                                                           : step.errorMessage);
    if (step.line > 0)
        message += QStringLiteral("（第 %1 行）").arg(step.line);
    return message;
}

bool validateTopLevel(const nq::yaml::Node& root, QString* error)
{
    if (!root.isMapping()) {
        *error = QStringLiteral("最终配置的顶层必须是 YAML 映射");
        return false;
    }

    const QStringList sequences = {
        QStringLiteral("proxies"),
        QStringLiteral("proxy-groups"),
        QStringLiteral("rules"),
    };
    for (const QString& key : sequences) {
        const nq::yaml::Node* value = root.find(key);
        if (value && !value->isSequence()) {
            *error = QStringLiteral("最终配置的 %1 必须是列表").arg(key);
            return false;
        }
    }

    const QStringList mappings = {
        QStringLiteral("dns"),
        QStringLiteral("rule-providers"),
        QStringLiteral("proxy-providers"),
        QStringLiteral("hosts"),
        QStringLiteral("tun"),
        QStringLiteral("profile"),
    };
    for (const QString& key : mappings) {
        const nq::yaml::Node* value = root.find(key);
        if (value && !value->isMapping()) {
            *error = QStringLiteral("最终配置的 %1 必须是映射").arg(key);
            return false;
        }
    }

    if (const nq::yaml::Node* rules = root.find(QStringLiteral("rules"))) {
        for (int i = 0; i < rules->sequence.size(); ++i) {
            if (!rules->sequence.at(i).isScalar()) {
                *error = QStringLiteral("最终配置的 rules[%1] 必须是字符串规则").arg(i);
                return false;
            }
        }
    }
    const QStringList objectLists = {QStringLiteral("proxies"),
                                     QStringLiteral("proxy-groups")};
    for (const QString& key : objectLists) {
        const nq::yaml::Node* list = root.find(key);
        if (!list)
            continue;
        for (int i = 0; i < list->sequence.size(); ++i) {
            if (!list->sequence.at(i).isMapping()) {
                *error = QStringLiteral("最终配置的 %1[%2] 必须是映射").arg(key).arg(i);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

ScriptPipeline::ScriptPipeline(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ScriptPipelineResult>("nq::scripts::ScriptPipelineResult");
    qRegisterMetaType<ScriptStepResult>("nq::scripts::ScriptStepResult");
}

ScriptPipeline::~ScriptPipeline()
{
    shutdown();
}

bool ScriptPipeline::run(const Request& request)
{
    if (m_busy)
        return false;

    m_busy = true;
    m_currentRequestId = request.requestId;
    m_control.cancelRequested.store(false, std::memory_order_relaxed);
    const quint64 generation = ++m_generation;
    emit busyChanged(true);
    emit started(request.requestId);

    Request copy = request;
    QThread* thread = QThread::create([this, copy = std::move(copy), generation] {
        ScriptPipelineResult result = process(copy, &m_control);
        QMetaObject::invokeMethod(
            this, [this, generation, result = std::move(result)] {
                handleResult(generation, result);
            },
            Qt::QueuedConnection);
    });
    thread->setStackSize(8 * 1024 * 1024);
    m_thread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_thread == thread)
            m_thread = nullptr;
        thread->deleteLater();
    });
    thread->start();
    return true;
}

void ScriptPipeline::handleResult(quint64 generation, const ScriptPipelineResult& result)
{
    if (generation != m_generation)
        return;
    m_busy = false;
    emit busyChanged(false);
    emit finished(result);
}

void ScriptPipeline::cancel()
{
    m_control.cancelRequested.store(true, std::memory_order_relaxed);
}

void ScriptPipeline::shutdown()
{
    m_control.cancelRequested.store(true, std::memory_order_relaxed);
    QThread* thread = m_thread;
    m_thread = nullptr;
    if (thread) {
        disconnect(thread, nullptr, this, nullptr);
        if (thread->isRunning()) {
            thread->wait(5000);
            if (thread->isRunning()) {
                thread->terminate();
                thread->wait(2000);
            }
        }
        delete thread;
    }
    m_busy = false;
}

ScriptPipelineResult ScriptPipeline::process(const Request& request,
                                             ScriptEngine::Control* control)
{
    QElapsedTimer timer;
    timer.start();

    ScriptPipelineResult result;
    result.profileId = request.profileId;
    result.profileName = request.profileName;
    result.originalYaml = request.rawYaml;
    result.usedScripts = !request.steps.isEmpty();

    auto finish = [&]() {
        result.durationMs = timer.elapsed();
        return result;
    };

    if (control && control->cancelRequested.load(std::memory_order_relaxed)) {
        result.canceled = true;
        result.error = QStringLiteral("脚本执行已取消");
        return finish();
    }

    QString baseline = request.rawYaml;
    if (request.applyClientSettings)
        baseline = RuntimeConfig::build(request.rawYaml, request.settings);
    result.baselineYaml = baseline;

    nq::yaml::Document document;
    if (!document.parse(baseline.toUtf8())) {
        result.error = QStringLiteral("无法解析订阅配置：%1").arg(document.error.toString());
        return finish();
    }
    nq::yaml::Node current = document.root;

    if (!current.isMapping()) {
        result.error = QStringLiteral("订阅配置的顶层不是 YAML 映射");
        return finish();
    }

    ScriptExecutionLimits limits;
    limits.timeoutMs = qBound(100, request.scriptSettings.timeoutMs, 120000);
    limits.memoryLimitBytes =
        qBound<qint64>(8LL * 1024 * 1024, request.scriptSettings.memoryLimitBytes,
                       2048LL * 1024 * 1024);
    limits.maxLogEntries = qBound(100, request.scriptSettings.logRetention, 10000);
    limits.maxLogBytes = 1024 * 1024;

    for (const ScriptStep& step : request.steps) {
        if (control && control->cancelRequested.load(std::memory_order_relaxed)) {
            result.canceled = true;
            result.error = QStringLiteral("脚本执行已取消");
            return finish();
        }

        ScriptStepResult stepResult;
        stepResult.scriptId = step.scriptId;
        stepResult.name = step.name;
        stepResult.scope = step.scope;

        if (!step.integrityError.isEmpty()) {
            stepResult.ok = false;
            stepResult.status = ScriptRunStatus::Failed;
            stepResult.errorName = QStringLiteral("IntegrityError");
            stepResult.errorMessage = step.integrityError;
            stepResult.skipped = !request.stopOnError;
            if (request.stopOnError) {
                result.ok = false;
                result.failingScriptId = step.scriptId;
                result.failingScriptName = step.name;
                result.failingVersionSha256 = step.versionSha256;
                result.error = profileError(stepResult);
                result.steps.append(stepResult);
                return finish();
            }
            result.partial = true;
            result.steps.append(stepResult);
            continue;
        }

        if (step.source.trimmed().isEmpty()) {
            stepResult.ok = false;
            stepResult.status = ScriptRunStatus::Failed;
            stepResult.errorName = QStringLiteral("SyntaxError");
            stepResult.errorMessage = QStringLiteral("脚本内容为空或文件缺失");
            stepResult.skipped = !request.stopOnError;
            if (request.stopOnError) {
                result.ok = false;
                result.failingScriptId = step.scriptId;
                result.failingScriptName = step.name;
                result.failingVersionSha256 = step.versionSha256;
                result.error = profileError(stepResult);
                result.steps.append(stepResult);
                return finish();
            }
            result.partial = true;
            result.steps.append(stepResult);
            continue;
        }

        ScriptExecutionRequest execution;
        execution.scriptId = step.scriptId;
        execution.scriptName = step.name;
        execution.source = step.source;
        execution.profileName = request.profileName;
        execution.config = current;
        execution.limits = limits;

        const ScriptExecutionResult executed = ScriptEngine::execute(execution, control);
        stepResult.durationMs = executed.durationMs;
        stepResult.logs = executed.logs;
        stepResult.errorName = executed.errorName;
        stepResult.errorMessage = executed.errorMessage;
        stepResult.stack = executed.stack;
        stepResult.line = executed.line;
        stepResult.column = executed.column;
        stepResult.status = executed.status;

        if (executed.status == ScriptRunStatus::Canceled) {
            result.canceled = true;
            result.error = QStringLiteral("脚本执行已取消");
            result.steps.append(stepResult);
            return finish();
        }

        if (executed.ok) {
            stepResult.ok = true;
            stepResult.diff = nq::yaml::diff(current, executed.config);
            stepResult.configChanged = !stepResult.diff.entries.isEmpty();
            current = executed.config;
        } else {
            stepResult.ok = false;
            stepResult.errorMessage = executed.errorMessage;
            if (request.stopOnError) {
                result.ok = false;
                result.failingScriptId = step.scriptId;
                result.failingScriptName = step.name;
                result.failingVersionSha256 = step.versionSha256;
                result.error = profileError(stepResult);
                result.steps.append(stepResult);
                return finish();
            }
            stepResult.skipped = true;
            result.partial = true;
        }
        result.steps.append(stepResult);
    }

    nq::yaml::Document output;
    output.root = current;
    const QByteArray serialized = output.dump();
    if (serialized.isEmpty()) {
        result.ok = false;
        result.error = QStringLiteral("无法把脚本结果序列化为 YAML");
        return finish();
    }

    QString finalYaml = QString::fromUtf8(serialized);
    if (request.applyClientSettings)
        finalYaml = RuntimeConfig::build(finalYaml, request.settings);
    if (finalYaml.toUtf8().size() > 64LL * 1024 * 1024) {
        result.ok = false;
        result.error = QStringLiteral("最终配置超过 64 MB 限制");
        return finish();
    }

    nq::yaml::Document finalDocument;
    if (!finalDocument.parse(finalYaml.toUtf8())) {
        result.ok = false;
        result.error = QStringLiteral("脚本生成的配置不是有效 YAML：%1")
                           .arg(finalDocument.error.toString());
        return finish();
    }
    QString structureError;
    if (!validateTopLevel(finalDocument.root, &structureError)) {
        result.ok = false;
        result.error = structureError;
        return finish();
    }

    result.ok = true;
    result.finalYaml = finalYaml;
    // The authoritative diff for previews is baseline -> final. The step
    // diffs already describe each individual script's writes.
    result.diff = nq::yaml::diff(document.root, finalDocument.root);
    return finish();
}

}  // namespace nq::scripts
