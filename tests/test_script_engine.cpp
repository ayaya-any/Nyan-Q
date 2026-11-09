#include "scripts/ScriptEngine.h"

#include "scripts/YamlDocument.h"
#include "yaml_sample.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace nq::scripts;

namespace {
nq::yaml::Node parseOrEmpty(const QString& text)
{
    nq::yaml::Document document;
    if (!document.parse(text.toUtf8()))
        return {};
    return document.root;
}
}  // namespace

class ScriptEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void modifiesMihomoConfig();
    void preservesCustomAndExtensionFields();
    void collectsConsoleLogs();
    void reportsSyntaxErrorWithLocation();
    void reportsRuntimeErrorWithStack();
    void interruptsInfiniteLoop();
    void cancelsRunningScript();
    void enforcesMemoryLimit();
    void acceptsSyncAndAsyncMain();
    void reportsRejectedPromise();
    void reportsNeverSettlingPromise();
    void rejectsInvalidReturnValues();
    void doesNotExposeHostApis();
    void rejectsModuleLoading();
    void validatesSyntaxOnly();
    void rejectsMissingMain();

private:
    ScriptExecutionRequest makeRequest(const QString& source, int timeoutMs = 1000,
                                       qint64 memoryLimit = 64LL * 1024 * 1024,
                                       bool fullConfig = false) const;
    nq::yaml::Node m_simple;
    nq::yaml::Node m_full;
};

void ScriptEngineTest::initTestCase()
{
    m_simple = parseOrEmpty(yamlSampleConfigWithoutProxyProvider());
    m_full = parseOrEmpty(yamlSampleConfig());
    QVERIFY(m_simple.isMapping());
    QVERIFY(m_full.isMapping());
}

ScriptExecutionRequest ScriptEngineTest::makeRequest(const QString& source, int timeoutMs,
                                                     qint64 memoryLimit,
                                                     bool fullConfig) const
{
    ScriptExecutionRequest request;
    request.scriptId = QStringLiteral("script-1");
    request.scriptName = QStringLiteral("测试脚本");
    request.profileName = QStringLiteral("测试订阅");
    request.source = source;
    request.config = fullConfig ? m_full : m_simple;
    request.limits.timeoutMs = timeoutMs;
    request.limits.memoryLimitBytes = memoryLimit;
    request.limits.maxLogEntries = 100;
    request.limits.maxLogBytes = 64 * 1024;
    return request;
}

void ScriptEngineTest::modifiesMihomoConfig()
{
    const QString script = QStringLiteral(R"JS(function main(config, profileName) {
    config["mixed-port"] = 17890;
    config.proxies.push({
        name: "Node B",
        type: "trojan",
        server: "b.example.com",
        port: 443,
        password: "secret"
    });
    config["proxy-groups"][0].proxies.push("Node B");
    config.dns.nameserver = ["1.1.1.1", "8.8.8.8"];
    config["rule-providers"] = {
        ads: { type: "http", behavior: "domain", url: "https://example.com/ads.yaml",
               path: "./ads.yaml", interval: 86400 }
    };
    config.rules.unshift("RULE-SET,ads,REJECT");
    config["custom-extension"] = { script: profileName, ok: true, count: 3, ratio: 1.5 };
    return config;
})JS");

    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script));
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.config.find(QStringLiteral("mixed-port"))->value, QStringLiteral("17890"));
    QCOMPARE(result.config.find(QStringLiteral("proxies"))->sequence.size(), 2);
    QCOMPARE(result.config.find(QStringLiteral("proxies"))
                 ->sequence.last()
                 .find(QStringLiteral("name"))
                 ->value,
             QStringLiteral("Node B"));
    QCOMPARE(result.config.find(QStringLiteral("dns"))
                 ->find(QStringLiteral("nameserver"))
                 ->sequence.first()
                 .value,
             QStringLiteral("1.1.1.1"));
    QCOMPARE(result.config.find(QStringLiteral("rules"))->sequence.first().value,
             QStringLiteral("RULE-SET,ads,REJECT"));
    QCOMPARE(result.config.find(QStringLiteral("custom-extension"))
                 ->find(QStringLiteral("script"))
                 ->value,
             QStringLiteral("测试订阅"));
    QVERIFY(result.changed);
    QVERIFY(result.durationMs >= 0);
}

void ScriptEngineTest::preservesCustomAndExtensionFields()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    config.rules = config.rules.filter((rule) => !rule.startsWith("GEOIP"));
    return config;
})JS");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script, 1000, 64 * 1024 * 1024, true));
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QVERIFY(result.config.find(QStringLiteral("custom-extension")));
    QCOMPARE(result.config.find(QStringLiteral("custom-extension"))
                 ->find(QStringLiteral("keep"))
                 ->value,
             QStringLiteral("yes"));
    QCOMPARE(result.config.find(QStringLiteral("unified-delay"))->scalarType,
             nq::yaml::ScalarType::Bool);
    QVERIFY(!result.config.find(QStringLiteral("rules"))
                 ->sequence.last()
                 .value.contains(QStringLiteral("GEOIP")));
}

void ScriptEngineTest::collectsConsoleLogs()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    console.log("hello", 42, { a: 1 });
    console.warn("careful");
    console.error("bad");
    console.debug("dbg");
    return config;
})JS");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script));
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.logs.size(), 4);
    QCOMPARE(result.logs.at(0).level, ScriptLogLevel::Info);
    QVERIFY(result.logs.at(0).message.contains(QStringLiteral("hello 42")));
    QCOMPARE(result.logs.at(1).level, ScriptLogLevel::Warning);
    QCOMPARE(result.logs.at(2).level, ScriptLogLevel::Error);
    QCOMPARE(result.logs.at(3).level, ScriptLogLevel::Debug);
}

void ScriptEngineTest::reportsSyntaxErrorWithLocation()
{
    const ScriptExecutionResult result =
        ScriptEngine::execute(makeRequest(QStringLiteral("function main( {\n")));
    QVERIFY(!result.ok);
    QCOMPARE(result.status, ScriptRunStatus::Failed);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY2(result.line >= 1, qPrintable(result.errorMessage));
}

void ScriptEngineTest::reportsRuntimeErrorWithStack()
{
    const QString script = QStringLiteral("function main(config) {\n  throw new Error('boom');\n}\n");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script));
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("boom")));
    QVERIFY2(result.stack.contains(QStringLiteral("main")), qPrintable(result.stack));
    QVERIFY2(result.line >= 2, qPrintable(result.stack));
}

void ScriptEngineTest::interruptsInfiniteLoop()
{
    const QString script = QStringLiteral("function main(config) { while (true) {} }\n");
    QElapsedTimer timer;
    timer.start();
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script, 150));
    QVERIFY(!result.ok);
    QCOMPARE(result.status, ScriptRunStatus::TimedOut);
    QVERIFY(result.errorMessage.contains(QStringLiteral("150")));
    QVERIFY2(timer.elapsed() < 5000, "timeout must interrupt the loop, not merely wait");
}

void ScriptEngineTest::cancelsRunningScript()
{
    ScriptEngine::Control control;
    QThread* canceller = QThread::create([&control] {
        QThread::msleep(80);
        control.cancelRequested.store(true);
    });
    canceller->start();
    QElapsedTimer timer;
    timer.start();
    const QString script = QStringLiteral("function main(config) { for (;;) {} }\n");
    const ScriptExecutionResult result =
        ScriptEngine::execute(makeRequest(script, 5000), &control);
    canceller->wait();
    delete canceller;
    QVERIFY(!result.ok);
    QCOMPARE(result.status, ScriptRunStatus::Canceled);
    QVERIFY(timer.elapsed() < 3000);
}

void ScriptEngineTest::enforcesMemoryLimit()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    const chunks = [];
    for (let i = 0; i < 64; ++i)
        chunks.push("x".repeat(1024 * 1024));
    return config;
})JS");
    const ScriptExecutionResult result =
        ScriptEngine::execute(makeRequest(script, 5000, 24LL * 1024 * 1024));
    QVERIFY(!result.ok);
    QCOMPARE(result.status, ScriptRunStatus::MemoryExceeded);
    QVERIFY(result.errorMessage.contains(QStringLiteral("memory"), Qt::CaseInsensitive) ||
            result.errorMessage.contains(QStringLiteral("内存")));
}

void ScriptEngineTest::acceptsSyncAndAsyncMain()
{
    const QString asyncScript = QStringLiteral(R"JS(async function main(config) {
    await Promise.resolve();
    config.mode = "global";
    return config;
})JS");
    const ScriptExecutionResult asyncResult =
        ScriptEngine::execute(makeRequest(asyncScript));
    QVERIFY2(asyncResult.ok, qPrintable(asyncResult.errorMessage));
    QCOMPARE(asyncResult.config.find(QStringLiteral("mode"))->value,
             QStringLiteral("global"));

    const QString arrowScript = QStringLiteral(
        "const main = (config) => { config.mode = 'direct'; return config; };\n");
    const ScriptExecutionResult arrowResult =
        ScriptEngine::execute(makeRequest(arrowScript));
    QVERIFY2(arrowResult.ok, qPrintable(arrowResult.errorMessage));
    QCOMPARE(arrowResult.config.find(QStringLiteral("mode"))->value,
             QStringLiteral("direct"));
}

void ScriptEngineTest::reportsRejectedPromise()
{
    const QString script = QStringLiteral(
        "async function main(config) {\n  throw new Error('rejected');\n}\n");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script));
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("rejected")));
    QVERIFY2(result.stack.contains(QStringLiteral("main")), qPrintable(result.stack));
}

void ScriptEngineTest::reportsNeverSettlingPromise()
{
    const QString script =
        QStringLiteral("function main(config) { return new Promise(() => {}); }\n");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script, 300));
    QVERIFY(!result.ok);
    QVERIFY2(result.errorMessage.contains(QStringLiteral("Promise")),
             qPrintable(result.errorMessage));
}

void ScriptEngineTest::rejectsInvalidReturnValues()
{
    const QVector<QString> scripts = {
        QStringLiteral("function main(config) { return null; }\n"),
        QStringLiteral("function main(config) { }\n"),
        QStringLiteral("function main(config) { return []; }\n"),
        QStringLiteral("function main(config) { return 'nope'; }\n"),
        QStringLiteral("function main(config) { return () => {}; }\n"),
        QStringLiteral("function main(config) { const c = { a: 1 }; c.self = c; return c; }\n"),
        QStringLiteral("function main(config) { return { value: NaN }; }\n"),
        QStringLiteral("function main(config) { return { value: Infinity }; }\n"),
        QStringLiteral("function main(config) { return { value: 10n }; }\n"),
        QStringLiteral("function main(config) { return { value: () => 1 }; }\n"),
        QStringLiteral("function main(config) { return new Date(); }\n"),
    };

    for (const QString& script : scripts) {
        const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script, 1000));
        QVERIFY2(!result.ok, qPrintable(script));
        QVERIFY2(!result.errorMessage.isEmpty(), qPrintable(script));
    }
}

void ScriptEngineTest::doesNotExposeHostApis()
{
    const QString script = QStringLiteral(R"JS(function main(config) {
    config.probe = {
        require: typeof require,
        process: typeof process,
        fetch: typeof fetch,
        std: typeof std,
        os: typeof os,
        file: typeof readFile,
        print: typeof print,
        gc: typeof gc,
        module: typeof module,
        setTimeout: typeof setTimeout,
        buffer: typeof Buffer
    };
    return config;
})JS");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script));
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    const nq::yaml::Node* probe = result.config.find(QStringLiteral("probe"));
    QVERIFY(probe);
    for (const auto& entry : probe->mapping) {
        QCOMPARE(entry.second.value, QStringLiteral("undefined"));
    }
}

void ScriptEngineTest::rejectsModuleLoading()
{
    const QString script = QStringLiteral(R"JS(async function main(config) {
    await import("https://example.com/malicious.js");
    return config;
})JS");
    const ScriptExecutionResult result = ScriptEngine::execute(makeRequest(script, 1000));
    QVERIFY(!result.ok);
    QVERIFY2(result.errorMessage.contains(QStringLiteral("模块")) ||
                 result.errorMessage.contains(QStringLiteral("import")) ||
                 result.errorMessage.contains(QStringLiteral("load")),
             qPrintable(result.errorMessage));
}

void ScriptEngineTest::validatesSyntaxOnly()
{
    QString error;
    int line = 0;
    int column = 0;
    QVERIFY(ScriptEngine::validate(QStringLiteral("function main(c) { return c; }"),
                                   QStringLiteral("ok"), &error));
    QVERIFY(error.isEmpty());

    QVERIFY(!ScriptEngine::validate(QStringLiteral("function main( {"), QStringLiteral("bad"),
                                    &error, &line, &column));
    QVERIFY(!error.isEmpty());
    QVERIFY(line >= 1);

    // Compilation must never run top-level side effects.
    error.clear();
    QVERIFY(ScriptEngine::validate(QStringLiteral("while (true) {}"),
                                   QStringLiteral("loop"), &error));
}

void ScriptEngineTest::rejectsMissingMain()
{
    const ScriptExecutionResult result =
        ScriptEngine::execute(makeRequest(QStringLiteral("const value = 1;\n")));
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("main")));
}

QTEST_GUILESS_MAIN(ScriptEngineTest)

#include "test_script_engine.moc"
