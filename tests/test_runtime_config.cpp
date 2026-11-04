#include "core/RuntimeConfig.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class RuntimeConfigTest : public QObject {
    Q_OBJECT

private slots:
    void overridesControlledTopLevelKeys();
    void preservesExistingDnsAndAddsTun();
    void addsDnsWhenTunEnabledWithoutDns();
    void quotesControllerSecret();
    void writeFailsForMissingSource();
};

namespace {
cq::AppSettings baseSettings()
{
    cq::AppSettings settings;
    settings.mixedPort = 17890;
    settings.controllerPort = 19090;
    settings.secret = QStringLiteral("it's-a-secret");
    settings.mode = QStringLiteral("rule");
    settings.logLevel = QStringLiteral("info");
    return settings;
}

QString sampleYaml()
{
    return QStringLiteral(R"(mixed-port: 7890
port: 7891
socks-port: 7892
allow-lan: true
mode: global
log-level: debug
external-controller: 0.0.0.0:9090
secret: "old"
ipv6: true

proxies:
  - name: test
    type: ss
    server: example.com
    port: 443
    cipher: aes-128-gcm
    password: pass

proxy-groups:
  - name: PROXY
    type: select
    proxies: [test, DIRECT]

rules:
  - MATCH,PROXY
)");
}
}  // namespace

void RuntimeConfigTest::overridesControlledTopLevelKeys()
{
    const cq::AppSettings settings = baseSettings();
    const QString result = RuntimeConfig::build(sampleYaml(), settings);

    QCOMPARE(result.count(QStringLiteral("mixed-port:")), 1);
    QVERIFY(result.contains(QStringLiteral("mixed-port: 17890")));
    QVERIFY(result.contains(QStringLiteral("allow-lan: false")));
    QVERIFY(result.contains(QStringLiteral("mode: rule")));
    QVERIFY(result.contains(QStringLiteral("log-level: info")));
    QVERIFY(result.contains(QStringLiteral("ipv6: false")));
    QVERIFY(result.contains(QStringLiteral("external-controller: '127.0.0.1:19090'")));
    QVERIFY(result.contains(QStringLiteral("secret: 'it''s-a-secret'")));
    QVERIFY(!result.contains(QStringLiteral("port: 7891")));
    QVERIFY(!result.contains(QStringLiteral("socks-port: 7892")));
    QVERIFY(!result.contains(QStringLiteral("old")));
    QVERIFY(result.contains(QStringLiteral("proxies:")));
    QVERIFY(result.contains(QStringLiteral("MATCH,PROXY")));
    QCOMPARE(result.count(QStringLiteral("external-controller:")), 1);
    QCOMPARE(result.count(QStringLiteral("tun:")), 0);
}

void RuntimeConfigTest::preservesExistingDnsAndAddsTun()
{
    cq::AppSettings settings = baseSettings();
    settings.tunEnabled = true;
    const QString yaml = sampleYaml() + QStringLiteral("\ndns:\n  enable: true\n  listen: 0.0.0.0:1053\n");
    const QString result = RuntimeConfig::build(yaml, settings);

    QVERIFY(result.contains(QStringLiteral("tun:")));
    QVERIFY(result.contains(QStringLiteral("enable: true")));
    QCOMPARE(result.count(QStringLiteral("dns:")), 1);
    QVERIFY(result.contains(QStringLiteral("listen: 0.0.0.0:1053")));
    QVERIFY(result.contains(QStringLiteral("dns-hijack:")));
}

void RuntimeConfigTest::addsDnsWhenTunEnabledWithoutDns()
{
    cq::AppSettings settings = baseSettings();
    settings.tunEnabled = true;
    const QString result = RuntimeConfig::build(sampleYaml(), settings);

    QVERIFY(result.contains(QStringLiteral("dns:")));
    QVERIFY(result.contains(QStringLiteral("fake-ip-range: 198.18.0.1/16")));
    QVERIFY(result.contains(QStringLiteral("dns-hijack:")));
    QCOMPARE(result.count(QStringLiteral("dns:")), 1);
}

void RuntimeConfigTest::quotesControllerSecret()
{
    cq::AppSettings settings = baseSettings();
    settings.secret = QStringLiteral("line'break");
    const QString result = RuntimeConfig::build(sampleYaml(), settings);
    QVERIFY(result.contains(QStringLiteral("secret: 'line''break'")));
}

void RuntimeConfigTest::writeFailsForMissingSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const bool ok = RuntimeConfig::write(dir.filePath(QStringLiteral("missing.yaml")),
                                          dir.filePath(QStringLiteral("runtime.yaml")),
                                          baseSettings(), &error);
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(RuntimeConfigTest)

#include "test_runtime_config.moc"
