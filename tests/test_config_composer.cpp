#include "scripts/ConfigComposer.h"
#include "yaml_sample.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace nq::scripts;

namespace {
QString corePath()
{
    const QString bundled = QStringLiteral("third_party/mihomo/mihomo.exe");
    return QFileInfo::exists(bundled) ? QFileInfo(bundled).absoluteFilePath() : QString();
}

cq::AppSettings testSettings()
{
    cq::AppSettings settings;
    settings.mixedPort = 17890;
    settings.controllerPort = 19090;
    settings.secret = QStringLiteral("test-secret");
    settings.mode = QStringLiteral("rule");
    settings.logLevel = QStringLiteral("info");
    return settings;
}

ConfigComposer::Request requestFor(const QTemporaryDir& dir, const QString& content)
{
    ConfigComposer::Request request;
    request.requestId = QStringLiteral("build-1");
    request.profileId = QStringLiteral("profile-1");
    request.profileName = QStringLiteral("测试订阅");
    request.rawConfig = yamlSampleConfigWithoutProxyProvider().toUtf8();
    request.settings = testSettings();
    request.scriptSettings.timeoutMs = 1000;
    request.scriptSettings.memoryLimitBytes = 64LL * 1024 * 1024;
    request.corePath = corePath();
    request.dataDirectory = dir.path();
    request.candidatePath = dir.filePath(QStringLiteral("runtime/candidate.yaml"));
    request.outputPath = dir.filePath(QStringLiteral("runtime/config.yaml"));
    request.validationTimeoutMs = 20000;
    ScriptStep step;
    step.scriptId = QStringLiteral("script-1");
    step.name = QStringLiteral("测试脚本");
    step.source = content;
    request.steps.append(step);
    return request;
}

ConfigComposer::Result waitFor(ConfigComposer& composer, QSignalSpy& spy)
{
    if (spy.isEmpty())
        spy.wait(30000);
    if (spy.isEmpty())
        return {};
    return qvariant_cast<ConfigComposer::Result>(spy.takeFirst().first());
}
}  // namespace

class ConfigComposerTest : public QObject {
    Q_OBJECT

private slots:
    void commitsValidatedConfig();
    void keepsOldOutputWhenScriptFails();
    void keepsOldOutputWhenMihomoRejects();
    void canSkipValidationForTests();
    void coalescesConcurrentRequests();
};

void ConfigComposerTest::commitsValidatedConfig()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    const QString script = QStringLiteral(R"JS(function main(config) {
    config.rules.unshift("DOMAIN-SUFFIX,composed.example,DIRECT");
    return config;
})JS");
    QVERIFY(composer.build(requestFor(dir, script)));
    const ConfigComposer::Result result = waitFor(composer, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.validated);
    QVERIFY(QFileInfo::exists(result.outputPath));
    QFile file(result.outputPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    QVERIFY(written.contains("composed.example"));
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("runtime/candidate.yaml"))));
}

void ConfigComposerTest::keepsOldOutputWhenScriptFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));
    const QString outputPath = dir.filePath(QStringLiteral("runtime/config.yaml"));
    const QByteArray previous = "previous: true\n";
    QFile file(outputPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(previous);
    file.close();

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    const QString bad = QStringLiteral("function main(config) { throw new Error('compose boom'); }\n");
    QVERIFY(composer.build(requestFor(dir, bad)));
    const ConfigComposer::Result result = waitFor(composer, spy);
    QVERIFY(!result.ok);
    QVERIFY(!result.pipeline.ok);
    QCOMPARE(result.pipeline.failingScriptName, QStringLiteral("测试脚本"));
    QFile check(outputPath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), previous);
}

void ConfigComposerTest::keepsOldOutputWhenMihomoRejects()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));
    const QString outputPath = dir.filePath(QStringLiteral("runtime/config.yaml"));
    const QByteArray previous = "previous: true\n";
    QFile file(outputPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(previous);
    file.close();

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    const QString script = QStringLiteral(R"JS(function main(config) {
    config.proxies.push({ name: "bad", type: "definitely-not-a-protocol",
                          server: "example.com", port: 443 });
    return config;
})JS");
    QVERIFY(composer.build(requestFor(dir, script)));
    const ConfigComposer::Result result = waitFor(composer, spy);
    QVERIFY(!result.ok);
    QVERIFY(result.pipeline.ok);
    QVERIFY2(result.error.contains(QStringLiteral("Mihomo")) ||
                 result.error.contains(QStringLiteral("失败")),
             qPrintable(result.error));
    QFile check(outputPath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), previous);
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("runtime/candidate.yaml"))));
}

void ConfigComposerTest::canSkipValidationForTests()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));
    ConfigComposer::Request request = requestFor(
        dir, QStringLiteral("function main(config) { config.mode = 'global'; return config; }\n"));
    request.validate = false;
    request.corePath.clear();

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    QVERIFY(composer.build(request));
    const ConfigComposer::Result result = waitFor(composer, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.validated);
    QVERIFY(QFileInfo::exists(result.outputPath));
}

void ConfigComposerTest::coalescesConcurrentRequests()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));
    ConfigComposer::Request first = requestFor(
        dir, QStringLiteral("function main(config) { config.first = true; return config; }\n"));
    first.validate = false;
    first.corePath.clear();
    ConfigComposer::Request second = first;
    second.requestId = QStringLiteral("build-2");
    second.steps.first().source =
        QStringLiteral("function main(config) { config.second = true; return config; }\n");

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    QVERIFY(composer.build(first));
    QVERIFY(composer.build(second));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 10000);
    const auto firstResult = qvariant_cast<ConfigComposer::Result>(spy.at(0).first());
    const auto secondResult = qvariant_cast<ConfigComposer::Result>(spy.at(1).first());
    QVERIFY(firstResult.ok);
    QVERIFY(secondResult.ok);
    QCOMPARE(secondResult.requestId, QStringLiteral("build-2"));
    QFile file(second.outputPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("second: true"));
}

QTEST_GUILESS_MAIN(ConfigComposerTest)

#include "test_config_composer.moc"
