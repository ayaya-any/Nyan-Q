#include "scripts/ScriptManager.h"
#include "scripts/ScriptPipeline.h"
#include "http_test_server.h"
#include "yaml_sample.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace nq::scripts;

namespace {
ScriptUpdateResult waitForUpdate(ScriptManager& manager, QSignalSpy& spy)
{
    if (spy.isEmpty())
        spy.wait(10000);
    if (spy.isEmpty())
        return {};
    return qvariant_cast<ScriptUpdateResult>(spy.takeFirst().first());
}

ScriptStep stepFor(const QString& source)
{
    ScriptStep step;
    step.scriptId = QStringLiteral("script");
    step.name = QStringLiteral("test");
    step.source = source;
    return step;
}
}  // namespace

class ScriptManagerTest : public QObject {
    Q_OBJECT

private slots:
    void initializesAndPersistsLocalScript();
    void resolvesGlobalAndProfileBindings();
    void importsRemoteScriptWithConditionalUpdate();
    void skipsUnchangedRemoteContent();
    void rejectsExpectedHashMismatch();
    void keepsOldScriptWhenDownloadFails();
    void serializesConcurrentUpdates();
    void autoUpdateIsDisabledByDefault();
    void rollsBackWhenUpdatedScriptFails();
    void rollsBackAfterGeneratedConfigFailure();
    void shutdownDuringDownloadKeepsOldScript();
    void rejectsSyntaxErrorsOnManualSave();
    void refusesUnconfirmedRemoteImport();
};

void ScriptManagerTest::initializesAndPersistsLocalScript()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY2(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error),
             qPrintable(error));
    const QString id = manager.createLocalScript(
        QStringLiteral("本地脚本"), QStringLiteral("description"),
        QStringLiteral("function main(config) { config.mode = 'global'; return config; }\n"),
        &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(manager.scripts().size(), 1);
    QVERIFY(QFileInfo::exists(QDir(dir.filePath(QStringLiteral("scripts")))
                                  .filePath(QStringLiteral("scripts.json"))));

    const QVector<ScriptStep> steps = manager.resolveSteps(QStringLiteral("profile-1"));
    QCOMPARE(steps.size(), 1);
    QCOMPARE(steps.first().scriptId, id);

    ScriptManager reloaded;
    QVERIFY2(reloaded.initialize(dir.filePath(QStringLiteral("scripts")), &error),
             qPrintable(error));
    QCOMPARE(reloaded.scripts().size(), 1);
    QCOMPARE(reloaded.scripts().first().id, id);
    QCOMPARE(reloaded.resolveSteps(QStringLiteral("profile-1")).size(), 1);
}

void ScriptManagerTest::resolvesGlobalAndProfileBindings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    const QString global = manager.createLocalScript(
        QStringLiteral("全局"), {}, QStringLiteral("function main(c){return c;}\n"), &error);
    const QString specific = manager.createLocalScript(
        QStringLiteral("专属"), {}, QStringLiteral("function main(c){return c;}\n"), &error);
    QVERIFY(!global.isEmpty());
    QVERIFY(!specific.isEmpty());

    QVERIFY(manager.setGlobalBinding(specific, false, &error));
    QVERIFY(manager.setProfileBinding(specific, QStringLiteral("p1"), true, &error));
    QVERIFY(manager.setProfileBinding(specific, QStringLiteral("p2"), true, &error));
    QVERIFY(manager.setProfileBinding(global, QStringLiteral("p2"), false, &error));

    const QVector<ScriptStep> p1 = manager.resolveSteps(QStringLiteral("p1"));
    QCOMPARE(p1.size(), 2);
    QCOMPARE(p1.at(0).scriptId, global);
    QCOMPARE(p1.at(1).scriptId, specific);
    QCOMPARE(p1.at(1).scope, ScriptScope::Profile);

    const QVector<ScriptStep> p2 = manager.resolveSteps(QStringLiteral("p2"));
    QCOMPARE(p2.size(), 1);
    QCOMPARE(p2.first().scriptId, specific);

    manager.setProfileBinding(global, QStringLiteral("p2"), false, &error);
    QCOMPARE(manager.resolveSteps(QStringLiteral("p2")).size(), 1);
}

void ScriptManagerTest::importsRemoteScriptWithConditionalUpdate()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { config.version = 1; return config; }\n";
    route.etag = "\"v1\"";
    route.lastModified = "Wed, 21 Oct 2015 07:28:00 GMT";
    server.setRoute(QStringLiteral("/script.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("远程脚本"),
                                 server.url(QStringLiteral("/script.js")), true, &error));
    const ScriptUpdateResult imported = waitForUpdate(manager, updates);
    QVERIFY2(imported.ok, qPrintable(imported.error));
    QCOMPARE(manager.scripts().size(), 1);
    const ScriptMeta script = manager.scripts().first();
    QVERIFY(script.remote());
    QVERIFY(script.trusted);
    QCOMPARE(manager.content(script.id),
             route.body);
    QCOMPARE(server.requestCount(QStringLiteral("/script.js")), 1);

    TestHttpServer::Route updated;
    updated.body = "function main(config) { config.version = 2; return config; }\n";
    updated.etag = "\"v2\"";
    updated.lastModified = "Thu, 22 Oct 2015 07:28:00 GMT";
    server.setRoute(QStringLiteral("/script.js"), updated);
    manager.checkForUpdate(script.id, true);
    const ScriptUpdateResult second = waitForUpdate(manager, updates);
    QVERIFY2(second.ok, qPrintable(second.error));
    QVERIFY(!second.notModified);
    QCOMPARE(manager.content(script.id), updated.body);
    QCOMPARE(manager.find(script.id)->versions.size(), 2);

    // Conditional request carries the previous validator; the next check must
    // carry the newly stored one.
    const auto headers = server.lastRequestHeaders(QStringLiteral("/script.js"));
    QCOMPARE(headers.value("if-none-match"), QByteArray("\"v1\""));

    // A 304 keeps the current content and refreshes the validator.
    TestHttpServer::Route notModified;
    notModified.status = 304;
    notModified.etag = "\"v2\"";
    server.setRoute(QStringLiteral("/script.js"), notModified);
    manager.checkForUpdate(script.id, true);
    const ScriptUpdateResult third = waitForUpdate(manager, updates);
    QVERIFY2(third.ok, qPrintable(third.error));
    QVERIFY(third.notModified);
    QCOMPARE(manager.content(script.id), updated.body);
    QCOMPARE(server.lastRequestHeaders(QStringLiteral("/script.js")).value("if-none-match"),
             QByteArray("\"v2\""));
}

void ScriptManagerTest::skipsUnchangedRemoteContent()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; }\n";
    route.etag = "\"a\"";
    server.setRoute(QStringLiteral("/same.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("相同"), server.url(QStringLiteral("/same.js")),
                                 true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;

    route.etag = "\"b\"";
    server.setRoute(QStringLiteral("/same.js"), route);
    manager.checkForUpdate(id, true);
    const ScriptUpdateResult result = waitForUpdate(manager, updates);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.skipped);
    QCOMPARE(manager.find(id)->versions.size(), 1);
}

void ScriptManagerTest::rejectsExpectedHashMismatch()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // one\n";
    server.setRoute(QStringLiteral("/pinned.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("固定"), server.url(QStringLiteral("/pinned.js")),
                                 true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const QByteArray original = manager.content(id);

    QVERIFY(manager.setExpectedSha256(id, QString(64, QLatin1Char('0')), &error));
    route.body = "function main(config) { return config; } // two\n";
    server.setRoute(QStringLiteral("/pinned.js"), route);
    manager.checkForUpdate(id, true);
    const ScriptUpdateResult result = waitForUpdate(manager, updates);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("SHA-256")));
    QCOMPARE(manager.content(id), original);
}

void ScriptManagerTest::keepsOldScriptWhenDownloadFails()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // stable\n";
    server.setRoute(QStringLiteral("/stable.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("稳定"), server.url(QStringLiteral("/stable.js")),
                                 true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const QByteArray original = manager.content(id);

    TestHttpServer::Route failure;
    failure.status = 500;
    failure.body = "server error";
    server.setRoute(QStringLiteral("/stable.js"), failure);
    manager.checkForUpdate(id, true);
    const ScriptUpdateResult result = waitForUpdate(manager, updates);
    QVERIFY(!result.ok);
    QCOMPARE(manager.content(id), original);
    QVERIFY(!manager.find(id)->lastError.isEmpty());
}

void ScriptManagerTest::serializesConcurrentUpdates()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // initial\n";
    server.setRoute(QStringLiteral("/concurrent.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("并发"),
                                 server.url(QStringLiteral("/concurrent.js")), true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;

    route.delayMs = 300;
    route.body = "function main(config) { return config; } // delayed\n";
    server.setRoute(QStringLiteral("/concurrent.js"), route);
    manager.checkForUpdate(id, true);
    QVERIFY(manager.isUpdating(id));
    manager.checkForUpdate(id, true);
    const ScriptUpdateResult result = waitForUpdate(manager, updates);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(manager.find(id)->versions.size(), 2);
}

void ScriptManagerTest::autoUpdateIsDisabledByDefault()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; }\n";
    server.setRoute(QStringLiteral("/auto.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("自动"), server.url(QStringLiteral("/auto.js")),
                                 true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const int count = server.requestCount(QStringLiteral("/auto.js"));
    manager.checkForUpdate(id, false);
    QTest::qWait(100);
    QCOMPARE(server.requestCount(QStringLiteral("/auto.js")), count);
}

void ScriptManagerTest::rollsBackWhenUpdatedScriptFails()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // v1\n";
    server.setRoute(QStringLiteral("/rollback.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("回滚"),
                                 server.url(QStringLiteral("/rollback.js")), true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const QByteArray versionOne = manager.content(id);

    route.body = "function main(config) { return config; } // v2\n";
    server.setRoute(QStringLiteral("/rollback.js"), route);
    manager.checkForUpdate(id, true);
    QVERIFY(waitForUpdate(manager, updates).ok);
    QVERIFY(manager.content(id) != versionOne);

    ScriptPipelineResult failure;
    failure.ok = false;
    failure.failingScriptId = id;
    failure.failingScriptName = QStringLiteral("回滚");
    failure.error = QStringLiteral("synthetic failure");
    manager.handlePipelineFailure(failure);
    QCOMPARE(manager.content(id), versionOne);
    QVERIFY(!manager.find(id)->versions.isEmpty());
}

void ScriptManagerTest::rollsBackAfterGeneratedConfigFailure()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // v1\n";
    server.setRoute(QStringLiteral("/runtime-fail.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("执行回滚"),
                                 server.url(QStringLiteral("/runtime-fail.js")), true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const QByteArray versionOne = manager.content(id);

    route.body =
        "function main(config) {\n  throw new Error('bad update');\n}\n";
    server.setRoute(QStringLiteral("/runtime-fail.js"), route);
    manager.checkForUpdate(id, true);
    QVERIFY(waitForUpdate(manager, updates).ok);
    QVERIFY(manager.content(id) != versionOne);

    ScriptPipeline::Request request;
    request.requestId = QStringLiteral("rollback-pipeline");
    request.profileId = QStringLiteral("p1");
    request.profileName = QStringLiteral("回滚测试");
    request.rawYaml = yamlSampleConfigWithoutProxyProvider();
    request.settings.mixedPort = 17890;
    request.settings.controllerPort = 19090;
    request.settings.secret = QStringLiteral("test");
    request.scriptSettings.timeoutMs = 1000;
    request.steps = manager.resolveSteps(QStringLiteral("p1"));
    request.stopOnError = true;
    const ScriptPipelineResult pipeline = ScriptPipeline::process(request, nullptr);
    QVERIFY(!pipeline.ok);
    QCOMPARE(pipeline.failingScriptId, id);
    manager.handlePipelineFailure(pipeline);
    QCOMPARE(manager.content(id), versionOne);
}

void ScriptManagerTest::shutdownDuringDownloadKeepsOldScript(){
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; } // keep\n";
    server.setRoute(QStringLiteral("/shutdown.js"), route);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QSignalSpy updates(&manager, &ScriptManager::updateFinished);
    QVERIFY(manager.importRemote(QStringLiteral("关闭"),
                                 server.url(QStringLiteral("/shutdown.js")), true, &error));
    QVERIFY(waitForUpdate(manager, updates).ok);
    const QString id = manager.scripts().first().id;
    const QByteArray original = manager.content(id);

    route.hang = true;
    server.setRoute(QStringLiteral("/shutdown.js"), route);
    manager.checkForUpdate(id, true);
    manager.shutdown();
    QCOMPARE(manager.content(id), original);
}

void ScriptManagerTest::rejectsSyntaxErrorsOnManualSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    const QString id = manager.createLocalScript(
        QStringLiteral("保存"), {}, QStringLiteral("function main(c){return c;}\n"), &error);
    QVERIFY(!id.isEmpty());
    const QByteArray original = manager.content(id);
    QVERIFY(!manager.saveContent(id, "function main( {\n", &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(manager.content(id), original);
}

void ScriptManagerTest::refusesUnconfirmedRemoteImport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ScriptManager manager;
    QString error;
    QVERIFY(manager.initialize(dir.filePath(QStringLiteral("scripts")), &error));
    QVERIFY(!manager.importRemote(QStringLiteral("未确认"),
                                  QUrl(QStringLiteral("https://example.com/script.js")), false,
                                  &error));
    QVERIFY(error.contains(QStringLiteral("确认")));
    QCOMPARE(manager.scripts().size(), 0);
}

QTEST_GUILESS_MAIN(ScriptManagerTest)

#include "test_script_manager.moc"
