#include "core/MihomoProcess.h"
#include "service/ServiceClient.h"
#include "http_test_server.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>

// A real pipe server on another event loop: the production client is blocking.
class HelperFixture : public QThread {
public:
    std::atomic<bool> ready{false};
    std::atomic<bool> drop{false};
    std::atomic<bool> hang{false};
    std::atomic<int> dropNext{0};
    std::atomic<int> delayMs{0};
    std::atomic<bool> fragment{false};
    std::atomic<bool> running{true};
    std::atomic<bool> requireRotatedToken{false};
    std::atomic<qint64> pid{0};
    std::atomic<int> requests{0};
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    ~HelperFixture() override { quit(); wait(); }

    void run() override
    {
        QLocalServer server;
        const QString name = QStringLiteral("nyanq-service-recovery-tests");
        if (!server.listen(name))
            return;
        connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
                    QByteArray buffer = socket->property("request").toByteArray();
                    buffer += socket->readAll();
                    socket->setProperty("request", buffer);
                    if (!buffer.contains('\n') || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    const auto object = QJsonDocument::fromJson(buffer).object();
                    const QString command = object.value("command").toString();
                    ++requests;
                    if (command == "start") ++starts;
                    if (command == "stop") ++stops;
                    if (hang)
                        return;
                    if (drop || dropNext.load() > 0) {
                        if (dropNext.load() > 0) --dropNext;
                        socket->abort();
                        return;
                    }
                    QJsonObject response{{"ok", true}, {"protocol", 1},
                                         {"version", NYANQ_VERSION},
                                         {"running", running.load()},
                                         {"pid", static_cast<double>(pid.load())},
                                         {"next", 0}, {"logs", QJsonArray{}}};
                    if (requireRotatedToken && object.value("token").toString() != QString(64, 'b'))
                        response = {{"ok", false}, {"error", QStringLiteral("未授权")}};
                    const QByteArray wire = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
                    QTimer::singleShot(delayMs.load(), socket, [this, socket, wire] {
                        if (fragment) {
                            socket->write(wire.left(5));
                            socket->flush();
                            QTimer::singleShot(350, socket, [socket, wire] {
                                socket->write(wire.mid(5));
                                socket->disconnectFromServer();
                            });
                        } else {
                            socket->write(wire);
                            socket->disconnectFromServer();
                        }
                    });
                });
            }
        });
        ready = true;
        exec();
    }
};

class ServiceRecoveryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        m_previousDir = qgetenv("NYANQ_SERVICE_DIR");
        qputenv("NYANQ_SERVICE_DIR", m_data.path().toUtf8());
        QFile token(nq::service::tokenPath());
        QVERIFY2(token.open(QIODevice::WriteOnly), qPrintable(token.fileName() + ": " + token.errorString()));
        token.write(QByteArray(64, 'a'));
        QFile config(m_data.filePath("config.yaml"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("mixed-port: 0\n");
    }
    void cleanupTestCase()
    {
        if (m_previousDir.isNull()) qunsetenv("NYANQ_SERVICE_DIR");
        else qputenv("NYANQ_SERVICE_DIR", m_previousDir);
        nq::service::ServiceClient::invalidateToken();
    }
    void init() { nq::service::ServiceClient::invalidateToken(); }

    void diagnosticsIdentifyCommandStageAndSocketWithoutSecrets()
    {
        QString error;
        QVERIFY(!nq::service::ServiceClient::start("config", "data", 9090, "secret-do-not-log", &error));
        QVERIFY2(error.contains("command=start"), qPrintable(error));
        QVERIFY(error.contains("stage=connect"));
        QVERIFY(error.contains("socketError="));
        QVERIFY(error.contains("elapsedMs="));
        QVERIFY(!error.contains("secret-do-not-log"));
        QVERIFY(!error.contains(QString(64, 'a')));
    }
    void acceptsResponseDelayedBeyondOneReadSlice()
    {
        HelperFixture helper; helper.delayMs = 400; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QString error;
        QVERIFY2(!nq::service::ServiceClient::status(0, &error).isEmpty(), qPrintable(error));
    }
    void acceptsFragmentedResponse()
    {
        HelperFixture helper; helper.fragment = true; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QString error;
        QVERIFY2(!nq::service::ServiceClient::status(0, &error).isEmpty(), qPrintable(error));
    }
    void reconnectsReadOnlyRequest()
    {
        HelperFixture helper; helper.dropNext = 1; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QString error = "stale error";
        QVERIFY2(!nq::service::ServiceClient::status(0, &error).isEmpty(), qPrintable(error));
        QCOMPARE(helper.requests.load(), 2);
        QVERIFY(error.isEmpty());
    }
    void statusDeadlineIncludesReconnects()
    {
        HelperFixture helper; helper.hang = true; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QElapsedTimer elapsed; elapsed.start();
        QString error;
        QVERIFY(nq::service::ServiceClient::status(0, &error).isEmpty());
        QVERIFY2(elapsed.elapsed() < 1300, qPrintable(QString::number(elapsed.elapsed())));
        QVERIFY(error.contains("stage=read"));
    }
    void doesNotReplayStartOrStop()
    {
        HelperFixture helper; helper.drop = true; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QString error;
        QVERIFY(!nq::service::ServiceClient::start("config", "data", 9090, "secret", &error));
        QVERIFY(!nq::service::ServiceClient::stop(&error));
        QCOMPARE(helper.starts.load(), 1);
        QCOMPARE(helper.stops.load(), 1);
    }
    void refreshesCachedTokenAfterHelperUpdate()
    {
        QVERIFY(!nq::service::ServiceClient::readToken().isEmpty());
        QFile token(nq::service::tokenPath());
        QVERIFY(token.open(QIODevice::WriteOnly));
        token.write(QByteArray(64, 'b')); token.close();
        HelperFixture helper; helper.requireRotatedToken = true; helper.start();
        QTRY_VERIFY(helper.ready.load());
        QString error;
        QVERIFY2(nq::service::ServiceClient::ping(&error), qPrintable(error));
        QCOMPARE(helper.requests.load(), 2);
    }
    void lostHelperKeepsLiveControllerAndReconnects()
    {
        HelperFixture helper; helper.start(); QTRY_VERIFY(helper.ready.load());
        TestHttpServer http; QVERIFY(http.start());
        TestHttpServer::Route route; route.body = "{\"version\":\"test\"}";
        http.setRoute("/version", route);
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy failed(&process, &MihomoProcess::failed);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        QSignalSpy logs(&process, &MihomoProcess::logLine);
        QString trace;
        connect(&process, &MihomoProcess::logLine, &process, [&trace](const QString& line) { trace += line + '\n'; });
        process.start(m_data.filePath("config.yaml"), http.url("/version").port(), "controller-secret");
        QVERIFY(process.isRunning());
        helper.drop = true;
        QTRY_VERIFY2_WITH_TIMEOUT(http.requestCount("/version") > 0, qPrintable(trace), 6000);
        QTest::qWait(4500);
        QVERIFY(process.isRunning()); QCOMPARE(failed.size(), 0); QCOMPARE(finished.size(), 0);
        QCOMPARE(http.lastRequestHeaders("/version").value("authorization"), QByteArray("Bearer controller-secret"));
        helper.drop = false;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto& row : logs) if (row[0].toString().contains(QStringLiteral("Helper 通信已恢复"))) return true;
            return false;
        }(), 7000);
        QCOMPARE(helper.starts.load(), 1);
        QCOMPARE(helper.stops.load(), 0);
        QVERIFY(process.isRunning());
    }
    void unknownKernelStateDoesNotMeanExited()
    {
        HelperFixture helper; helper.start(); QTRY_VERIFY(helper.ready.load());
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), 1, "secret");
        helper.drop = true;
        QTest::qWait(5000);
        QVERIFY(process.isRunning()); QCOMPARE(finished.size(), 0);
    }
    void explicitStoppedStatusIsConfirmed()
    {
        HelperFixture helper; helper.running = false; helper.start(); QTRY_VERIFY(helper.ready.load());
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), 1, "secret");
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!process.isRunning());
    }
    void stoppedHelperCannotOverrideLiveController()
    {
        HelperFixture helper; helper.running = false; helper.start(); QTRY_VERIFY(helper.ready.load());
        TestHttpServer http; QVERIFY(http.start());
        TestHttpServer::Route route; route.body = "{\"version\":\"test\"}";
        http.setRoute("/version", route);
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), http.url("/version").port(), "secret");
        QTRY_VERIFY_WITH_TIMEOUT(http.requestCount("/version") >= 2, 5000);
        QVERIFY(process.isRunning()); QCOMPARE(finished.size(), 0);
    }
    void stoppingCancelsPendingVerification()
    {
        HelperFixture helper; helper.start(); QTRY_VERIFY(helper.ready.load());
        TestHttpServer http; QVERIFY(http.start());
        TestHttpServer::Route route; route.body = "{}"; route.delayMs = 700;
        http.setRoute("/version", route);
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy failed(&process, &MihomoProcess::failed);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), http.url("/version").port(), "secret");
        helper.drop = true;
        QTRY_VERIFY_WITH_TIMEOUT(http.requestCount("/version") > 0, 5000);
        helper.drop = false;
        process.stop();
        QVERIFY(!process.isRunning()); QCOMPARE(finished.size(), 1);
        process.setServiceMode(false);
        QTest::qWait(1800);
        QCOMPARE(finished.size(), 1); QCOMPARE(failed.size(), 0);
    }
    void failedStopKeepsMonitoring()
    {
        HelperFixture helper; helper.start(); QTRY_VERIFY(helper.ready.load());
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), 1, "secret");
        helper.drop = true;
        process.stop();
        QVERIFY(process.isRunning()); QCOMPARE(finished.size(), 0);
        QVERIFY(!process.stopAndWait());
        QVERIFY(process.isRunning());
    }
#ifdef Q_OS_WIN
    void verifiesExitedProcessDuringHelperLoss()
    {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--kernel-child"});
        QVERIFY(child.waitForStarted());
        HelperFixture helper; helper.pid = child.processId(); helper.start(); QTRY_VERIFY(helper.ready.load());
        MihomoProcess process; process.setDataDirectory(m_data.path()); process.setServiceMode(true);
        QSignalSpy finished(&process, &MihomoProcess::finished);
        process.start(m_data.filePath("config.yaml"), 1, "secret");
        QTRY_COMPARE_WITH_TIMEOUT(process.processId(), child.processId(), 4000);
        helper.drop = true;
        QTest::qWait(4000);
        QVERIFY(process.isRunning()); QCOMPARE(finished.size(), 0);
        child.kill(); QVERIFY(child.waitForFinished());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 7000);
        QVERIFY(!process.isRunning());
    }
#endif
private:
    QTemporaryDir m_data{QCoreApplication::applicationDirPath() + "/service-recovery-XXXXXX"};
    QByteArray m_previousDir;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--kernel-child")) {
        QTimer::singleShot(30000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    ServiceRecoveryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_service_recovery.moc"
