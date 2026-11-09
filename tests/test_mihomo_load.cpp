#include "scripts/ConfigComposer.h"
#include "yaml_sample.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
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

int freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
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

class MihomoLoadTest : public QObject {
    Q_OBJECT

private slots:
    void generatedConfigLoadsIntoMihomo();
};

void MihomoLoadTest::generatedConfigLoadsIntoMihomo()
{
    if (corePath().isEmpty())
        QSKIP("Mihomo core not available");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.filePath(QStringLiteral("runtime")));
    QDir().mkpath(dir.filePath(QStringLiteral("data")));

    const int mixedPort = freePort();
    int controllerPort = freePort();
    if (controllerPort == mixedPort)
        controllerPort = freePort();
    QVERIFY(mixedPort > 0);
    QVERIFY(controllerPort > 0 && controllerPort != mixedPort);

    cq::AppSettings settings;
    settings.mixedPort = mixedPort;
    settings.controllerPort = controllerPort;
    settings.secret = QStringLiteral("load-test-secret");
    settings.mode = QStringLiteral("rule");
    settings.logLevel = QStringLiteral("info");
    settings.ipv6 = false;

    ConfigComposer::Request request;
    request.requestId = QStringLiteral("load-test");
    request.profileId = QStringLiteral("profile");
    request.profileName = QStringLiteral("加载测试");
    request.rawConfig = yamlSampleConfigWithoutProxyProvider().toUtf8();
    request.settings = settings;
    request.scriptSettings.timeoutMs = 1000;
    request.scriptSettings.memoryLimitBytes = 64LL * 1024 * 1024;
    request.corePath.clear();
    request.dataDirectory = dir.filePath(QStringLiteral("data"));
    request.candidatePath = dir.filePath(QStringLiteral("runtime/candidate.yaml"));
    request.outputPath = dir.filePath(QStringLiteral("runtime/config.yaml"));
    request.validate = false;
    request.stopOnError = true;
    ScriptStep step;
    step.scriptId = QStringLiteral("script");
    step.name = QStringLiteral("加载脚本");
    step.source = QStringLiteral(
        "function main(config) {\n"
        "  config.rules.unshift('DOMAIN-SUFFIX,load-test.invalid,DIRECT');\n"
        "  return config;\n"
        "}\n");
    request.steps.append(step);

    ConfigComposer composer;
    QSignalSpy spy(&composer, &ConfigComposer::finished);
    QVERIFY(composer.build(request));
    const ConfigComposer::Result result = waitFor(composer, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(QFileInfo::exists(result.outputPath));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(corePath());
    process.setArguments({QStringLiteral("-d"), dir.filePath(QStringLiteral("data")),
                          QStringLiteral("-f"), result.outputPath,
                          QStringLiteral("-ext-ctl"),
                          QStringLiteral("127.0.0.1:%1").arg(controllerPort)});
    process.start();
    QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString()));

    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        output += process.readAll();
        if (output.contains("Start initial configuration complete") ||
            output.contains("RESTful API listening"))
            break;
        QThread::msleep(100);
        QCoreApplication::processEvents();
    }
    QVERIFY2(process.state() == QProcess::Running, "Mihomo exited while loading the config");
    QVERIFY2(output.contains("Start initial configuration complete") ||
                 output.contains("RESTful API listening"),
             qPrintable(QString::fromUtf8(output)));
    process.kill();
    process.waitForFinished(5000);
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        process.waitForFinished(3000);
    }
}

QTEST_GUILESS_MAIN(MihomoLoadTest)

#include "test_mihomo_load.moc"
