#include "core/Types.h"
#include "scripts/ScriptPipeline.h"
#include "yaml_sample.h"

#include <QSignalSpy>
#include <QTest>

using namespace nq::scripts;

namespace {
cq::AppSettings testSettings()
{
    cq::AppSettings settings;
    settings.mixedPort = 17890;
    settings.controllerPort = 19090;
    settings.secret = QStringLiteral("test-secret");
    settings.mode = QStringLiteral("rule");
    settings.logLevel = QStringLiteral("info");
    settings.ipv6 = false;
    return settings;
}

ScriptStep step(const QString& id, const QString& name, const QString& source,
                ScriptScope scope = ScriptScope::Global)
{
    ScriptStep s;
    s.scriptId = id;
    s.name = name;
    s.source = source;
    s.scope = scope;
    return s;
}

ScriptPipeline::Request requestFor(const QVector<ScriptStep>& steps, bool stopOnError = true)
{
    ScriptPipeline::Request request;
    request.requestId = QStringLiteral("req-1");
    request.profileId = QStringLiteral("profile-1");
    request.profileName = QStringLiteral("测试订阅");
    request.rawYaml = yamlSampleConfigWithoutProxyProvider();
    request.settings = testSettings();
    request.steps = steps;
    request.scriptSettings.timeoutMs = 1000;
    request.scriptSettings.memoryLimitBytes = 64LL * 1024 * 1024;
    request.scriptSettings.stopOnError = stopOnError;
    request.stopOnError = stopOnError;
    return request;
}
}  // namespace

class ScriptPipelineTest : public QObject {
    Q_OBJECT

private slots:
    void executesScriptsInOrder();
    void reappliesClientSettingsAfterScripts();
    void stopsAtFailingScript();
    void canSkipFailingScriptWhenConfigured();
    void returnsStepLogsAndDiff();
    void runsAsynchronously();
    void finalStructureIsValidated();
    void reportsIntegrityErrorBeforeExecution();
};

void ScriptPipelineTest::executesScriptsInOrder()
{
    const QString first = QStringLiteral(R"JS(function main(config) {
    config.trace = ["first"];
    config.rules.push("DOMAIN-SUFFIX,first.example,DIRECT");
    return config;
})JS");
    const QString second = QStringLiteral(R"JS(function main(config) {
    config.trace.push("second");
    config.rules.push("DOMAIN-SUFFIX,second.example,DIRECT");
    return config;
})JS");
    const ScriptPipelineResult result = ScriptPipeline::process(
        requestFor({step(QStringLiteral("s1"), QStringLiteral("第一"), first),
                    step(QStringLiteral("s2"), QStringLiteral("第二"), second)}),
        nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.steps.size(), 2);
    QVERIFY(result.steps.at(0).ok);
    QVERIFY(result.steps.at(1).ok);
    QVERIFY(result.finalYaml.contains(QStringLiteral("first.example")));
    QVERIFY(result.finalYaml.contains(QStringLiteral("second.example")));
    QVERIFY(result.finalYaml.indexOf(QStringLiteral("first.example")) <
            result.finalYaml.indexOf(QStringLiteral("second.example")));
    QVERIFY(!result.diff.entries.isEmpty());
}

void ScriptPipelineTest::reappliesClientSettingsAfterScripts()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    config["mixed-port"] = 1234;
    config.mode = "global";
    config.secret = "hijacked";
    return config;
})JS");
    const ScriptPipelineResult result =
        ScriptPipeline::process(requestFor({step(QStringLiteral("s1"), QStringLiteral("设置"), script)}),
                                nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.finalYaml.contains(QStringLiteral("mixed-port: 17890")));
    QVERIFY(!result.finalYaml.contains(QStringLiteral("mixed-port: 1234")));
    QVERIFY(result.finalYaml.contains(QStringLiteral("mode: rule")));
    QVERIFY(result.finalYaml.contains(QStringLiteral("secret: 'test-secret'")));
    QVERIFY(!result.finalYaml.contains(QStringLiteral("hijacked")));
}

void ScriptPipelineTest::stopsAtFailingScript()
{
    const QString good = QStringLiteral("function main(config) { config.ok = true; return config; }\n");
    const QString bad = QStringLiteral("function main(config) { throw new Error('boom'); }\n");
    const QString after = QStringLiteral("function main(config) { config.after = true; return config; }\n");
    const ScriptPipelineResult result = ScriptPipeline::process(
        requestFor({step(QStringLiteral("s1"), QStringLiteral("成功"), good),
                    step(QStringLiteral("s2"), QStringLiteral("失败"), bad),
                    step(QStringLiteral("s3"), QStringLiteral("后续"), after)}),
        nullptr);
    QVERIFY(!result.ok);
    QCOMPARE(result.steps.size(), 2);
    QCOMPARE(result.failingScriptId, QStringLiteral("s2"));
    QCOMPARE(result.failingScriptName, QStringLiteral("失败"));
    QVERIFY(result.error.contains(QStringLiteral("boom")));
    QVERIFY(result.finalYaml.isEmpty());
    QVERIFY(result.steps.at(0).ok);
    QVERIFY(!result.steps.at(1).ok);
}

void ScriptPipelineTest::canSkipFailingScriptWhenConfigured()
{
    const QString good = QStringLiteral("function main(config) { config.ok = true; return config; }\n");
    const QString bad = QStringLiteral("function main(config) { throw new Error('skip me'); }\n");
    ScriptPipeline::Request request = requestFor(
        {step(QStringLiteral("s1"), QStringLiteral("成功"), good),
         step(QStringLiteral("s2"), QStringLiteral("失败"), bad)},
        false);
    const ScriptPipelineResult result = ScriptPipeline::process(request, nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.partial);
    QCOMPARE(result.steps.size(), 2);
    QVERIFY(result.steps.at(0).ok);
    QVERIFY(!result.steps.at(1).ok);
    QVERIFY(result.steps.at(1).skipped);
    QVERIFY(result.finalYaml.contains(QStringLiteral("ok: true")));
}

void ScriptPipelineTest::returnsStepLogsAndDiff()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    console.log("changing rules");
    config.rules.unshift("DOMAIN-SUFFIX,log.example,DIRECT");
    return config;
})JS");
    const ScriptPipelineResult result =
        ScriptPipeline::process(requestFor({step(QStringLiteral("s1"), QStringLiteral("日志"), script)}),
                                nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.steps.at(0).logs.size(), 1);
    QCOMPARE(result.steps.at(0).logs.first().message, QStringLiteral("changing rules"));
    QVERIFY(result.steps.at(0).configChanged);
    QVERIFY(result.steps.at(0).diff.total >= 1);
    QVERIFY(result.steps.at(0).durationMs >= 0);
}

void ScriptPipelineTest::runsAsynchronously()
{
    ScriptPipeline pipeline;
    QSignalSpy finished(&pipeline, &ScriptPipeline::finished);
    QSignalSpy busy(&pipeline, &ScriptPipeline::busyChanged);
    const QString script = QStringLiteral("function main(config) { return config; }\n");
    QVERIFY(pipeline.run(requestFor({step(QStringLiteral("s1"), QStringLiteral("异步"), script)})));
    QVERIFY(pipeline.isBusy());
    QVERIFY(finished.wait(5000));
    QVERIFY(!pipeline.isBusy());
    QCOMPARE(finished.count(), 1);
    const auto result =
        qvariant_cast<ScriptPipelineResult>(finished.first().first());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(busy.count() >= 2);
}

void ScriptPipelineTest::finalStructureIsValidated()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    config.proxies = "not a list";
    return config;
})JS");
    const ScriptPipelineResult result =
        ScriptPipeline::process(requestFor({step(QStringLiteral("s1"), QStringLiteral("坏结构"), script)}),
                                nullptr);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("proxies")));
    QVERIFY(result.finalYaml.isEmpty());
}

void ScriptPipelineTest::reportsIntegrityErrorBeforeExecution()
{
    ScriptStep broken = step(QStringLiteral("s1"), QStringLiteral("固定哈希"), QStringLiteral("function main(c){return c;}\n"));
    broken.integrityError = QStringLiteral("哈希不匹配");
    const ScriptPipelineResult result = ScriptPipeline::process(requestFor({broken}), nullptr);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("哈希不匹配")));
    QCOMPARE(result.steps.size(), 1);
}

QTEST_GUILESS_MAIN(ScriptPipelineTest)

#include "test_script_pipeline.moc"
