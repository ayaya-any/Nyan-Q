#include "scripts/MihomoConfigValidator.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace nq::scripts;

namespace {
QString corePath()
{
    const QString bundled = QStringLiteral("third_party/mihomo/mihomo.exe");
    return QFileInfo::exists(bundled) ? QFileInfo(bundled).absoluteFilePath() : QString();
}
}  // namespace

class MihomoConfigValidatorTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsValidConfig();
    void rejectsInvalidConfig();
    void reportsMissingCore();
    void validatesAsynchronously();
};

void MihomoConfigValidatorTest::acceptsValidConfig()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("valid.yaml"));
    const QByteArray config =
        "mixed-port: 17890\nallow-lan: false\nmode: rule\nlog-level: info\n"
        "ipv6: false\nproxies: []\nproxy-groups:\n  - name: PROXY\n    type: select\n"
        "    proxies: [DIRECT]\nrules:\n  - MATCH,DIRECT\n";
    QFile file(configPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(config);
    file.close();

    QString message;
    QVERIFY2(MihomoConfigValidator::validateSync(corePath(), configPath, dir.path(), 20000,
                                                 &message),
             qPrintable(message));
}

void MihomoConfigValidatorTest::rejectsInvalidConfig()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("invalid.yaml"));
    const QByteArray config =
        "mixed-port: 17890\nproxies:\n  - name: broken\n    type: definitely-not-a-protocol\n"
        "    server: example.com\n    port: 443\nrules:\n  - MATCH,broken\n";
    QFile file(configPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(config);
    file.close();

    QString message;
    QVERIFY(!MihomoConfigValidator::validateSync(corePath(), configPath, dir.path(), 20000,
                                                 &message));
    QVERIFY(!message.isEmpty());
}

void MihomoConfigValidatorTest::reportsMissingCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("config.yaml"));
    QFile file(configPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("proxies: []\n");
    file.close();

    QString message;
    QVERIFY(!MihomoConfigValidator::validateSync(dir.filePath(QStringLiteral("missing.exe")),
                                                 configPath, dir.path(), 5000, &message));
    QVERIFY(message.contains(QStringLiteral("找不到 Mihomo")));
}

void MihomoConfigValidatorTest::validatesAsynchronously()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString configPath = dir.filePath(QStringLiteral("async.yaml"));
    QFile file(configPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("proxies: []\nproxy-groups: []\nrules: []\n");
    file.close();

    MihomoConfigValidator validator;
    QSignalSpy spy(&validator, &MihomoConfigValidator::finished);
    validator.validate(corePath(), configPath, dir.path(), 20000);
    QVERIFY(spy.wait(25000));
    QVERIFY(spy.first().at(0).toBool());
    QVERIFY(!validator.isBusy());
}

QTEST_GUILESS_MAIN(MihomoConfigValidatorTest)

#include "test_mihomo_config_validator.moc"
