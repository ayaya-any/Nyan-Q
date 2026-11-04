#include "core/ProfileStore.h"

#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class ProfileStoreTest : public QObject {
    Q_OBJECT

private slots:
    void createsBuiltinProfile();
    void importsAndPersistsLocalProfile();
    void importsRemoteSubscription();
    void editsAndPersistsConfigContent();
    void renamesProfileAndPersists();
    void rejectsInvalidEditedContent();
    void rejectsInvalidYaml();
    void removingActiveProfileFallsBack();
};

namespace {
QByteArray validYaml(const QString& groupName = QStringLiteral("TEST"))
{
    return QByteArrayLiteral("mixed-port: 7890\n"
                             "proxies: []\n"
                             "proxy-groups:\n"
                             "  - name: ") +
           groupName.toUtf8() +
           QByteArrayLiteral("\n    type: select\n    proxies: [DIRECT]\n"
                             "rules:\n  - MATCH,DIRECT\n");
}
}  // namespace

void ProfileStoreTest::createsBuiltinProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    QCOMPARE(store.profiles().size(), 1);
    QVERIFY(store.activeProfile().has_value());
    QCOMPARE(store.activeProfile()->type, QStringLiteral("builtin"));
    QVERIFY(QFile::exists(store.configPath(*store.activeProfile())));
}

void ProfileStoreTest::importsAndPersistsLocalProfile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    const QString activeBefore = store.activeId();
    QSignalSpy changedSpy(&store, &ProfileStore::changed);
    store.importContent(QStringLiteral("Unit Test"), validYaml(), QStringLiteral("local"));
    QCOMPARE(store.profiles().size(), 2);
    QVERIFY(store.activeId() != activeBefore);
    QCOMPARE(store.activeProfile()->name, QStringLiteral("Unit Test"));
    QCOMPARE(changedSpy.count(), 1);

    const QString importedId = store.activeId();
    ProfileStore reloaded;
    reloaded.initialize(dir.path());
    QCOMPARE(reloaded.profiles().size(), 2);
    QCOMPARE(reloaded.activeId(), importedId);
    QCOMPARE(reloaded.activeProfile()->name, QStringLiteral("Unit Test"));
}

void ProfileStoreTest::importsRemoteSubscription()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                const QByteArray body = validYaml(QStringLiteral("REMOTE"));
                QByteArray response =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/yaml\r\n"
                    "profile-title: Remote Unit Test\r\n"
                    "subscription-userinfo: upload=1024; download=2048; total=1048576; expire=4102444800\r\n"
                    "Connection: close\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    ProfileStore store;
    store.initialize(dir.path());
    QSignalSpy successSpy(&store, &ProfileStore::operationSucceeded);
    QSignalSpy failureSpy(&store, &ProfileStore::operationFailed);
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/subscription")
                       .arg(server.serverPort()));
    store.importUrl(QString(), url);

    QTRY_COMPARE_WITH_TIMEOUT(successSpy.count(), 1, 5000);
    QCOMPARE(failureSpy.count(), 0);
    QCOMPARE(store.profiles().size(), 2);
    const auto active = store.activeProfile();
    QVERIFY(active.has_value());
    QCOMPARE(active->name, QStringLiteral("Remote Unit Test"));
    QCOMPARE(active->type, QStringLiteral("remote"));
    QCOMPARE(active->url, url.toString());
    QCOMPARE(active->upload, 1024LL);
    QCOMPARE(active->download, 2048LL);
    QCOMPARE(active->total, 1048576LL);
    QCOMPARE(active->expireAt, 4102444800LL);
}

void ProfileStoreTest::editsAndPersistsConfigContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    const QString id = store.activeId();
    const QByteArray original = store.configContent(id);
    QVERIFY(!original.isEmpty());
    QVERIFY(original.contains("proxies:"));

    QByteArray edited = original;
    edited += "# edited from the config editor\n";
    QSignalSpy changedSpy(&store, &ProfileStore::changed);
    QSignalSpy updatedSpy(&store, &ProfileStore::profileUpdated);
    QSignalSpy successSpy(&store, &ProfileStore::operationSucceeded);

    QString error;
    QVERIFY2(store.saveConfigContent(id, edited, &error), qPrintable(error));
    QCOMPARE(store.configContent(id), edited);
    QCOMPARE(store.find(id)->type, QStringLiteral("local"));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(updatedSpy.count(), 1);
    QCOMPARE(updatedSpy.first().first().toString(), id);
    QCOMPARE(successSpy.count(), 1);

    ProfileStore reloaded;
    reloaded.initialize(dir.path());
    QCOMPARE(reloaded.configContent(id), edited);
    QCOMPARE(reloaded.find(id)->type, QStringLiteral("local"));
}

void ProfileStoreTest::renamesProfileAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    const QString id = store.activeId();
    QString error;
    QVERIFY(store.renameProfile(id, QStringLiteral("Renamed Profile"), &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(store.activeProfile()->name, QStringLiteral("Renamed Profile"));

    ProfileStore reloaded;
    reloaded.initialize(dir.path());
    QCOMPARE(reloaded.activeProfile()->name, QStringLiteral("Renamed Profile"));

    QVERIFY(!store.renameProfile(id, QStringLiteral("   "), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.renameProfile(id, QStringLiteral("Renamed Profile"), &error));
    QVERIFY(error.isEmpty());
}

void ProfileStoreTest::rejectsInvalidEditedContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    const QString id = store.activeId();
    const QByteArray original = store.configContent(id);

    QString error;
    QVERIFY(!store.saveConfigContent(id, QByteArrayLiteral("<html>not a config</html>"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.configContent(id), original);
    QCOMPARE(store.find(id)->type, QStringLiteral("builtin"));

    QVERIFY(!store.saveConfigContent(QStringLiteral("missing"), original, &error));
    QVERIFY(!error.isEmpty());
}

void ProfileStoreTest::rejectsInvalidYaml()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    QSignalSpy failedSpy(&store, &ProfileStore::operationFailed);
    store.importContent(QStringLiteral("Broken"), QByteArrayLiteral("<html>not yaml</html>"));
    QCOMPARE(store.profiles().size(), 1);
    QCOMPARE(failedSpy.count(), 1);
}

void ProfileStoreTest::removingActiveProfileFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProfileStore store;
    store.initialize(dir.path());
    store.importContent(QStringLiteral("Temporary"), validYaml());
    QCOMPARE(store.profiles().size(), 2);
    const QString id = store.activeId();
    store.removeProfile(id);
    QCOMPARE(store.profiles().size(), 1);
    QVERIFY(store.activeProfile().has_value());
    QVERIFY(store.activeId() != id);
}

QTEST_GUILESS_MAIN(ProfileStoreTest)

#include "test_profile_store.moc"
