#include "core/SettingsStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSettings>
#include <QStringList>

SettingsStore::SettingsStore(const QString& filePath)
    : m_filePath(filePath)
{
}

QString SettingsStore::createSecret()
{
    QByteArray bytes(24, Qt::Uninitialized);
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(0, 256));
    return QString::fromLatin1(bytes.toHex());
}

bool SettingsStore::isValidPort(int port)
{
    return port > 0 && port <= 65535;
}

cq::AppSettings SettingsStore::load()
{
    cq::AppSettings settings;
    QSettings ini(m_filePath, QSettings::IniFormat);

    settings.mixedPort = ini.value(QStringLiteral("network/mixedPort"), settings.mixedPort).toInt();
    settings.controllerPort =
        ini.value(QStringLiteral("network/controllerPort"), settings.controllerPort).toInt();
    settings.secret = ini.value(QStringLiteral("network/secret"), QString()).toString();
    settings.allowLan = ini.value(QStringLiteral("network/allowLan"), settings.allowLan).toBool();
    settings.ipv6 = ini.value(QStringLiteral("network/ipv6"), settings.ipv6).toBool();
    settings.mode = ini.value(QStringLiteral("network/mode"), settings.mode).toString().toLower();
    settings.tunEnabled = ini.value(QStringLiteral("network/tun"), settings.tunEnabled).toBool();

    settings.logLevel =
        ini.value(QStringLiteral("core/logLevel"), settings.logLevel).toString().toLower();
    settings.corePath = ini.value(QStringLiteral("core/path"), QString()).toString();
    settings.delayTestUrl =
        ini.value(QStringLiteral("core/delayTestUrl"), settings.delayTestUrl).toString();

    settings.autoLaunch =
        ini.value(QStringLiteral("behavior/autoLaunch"), settings.autoLaunch).toBool();
    settings.startMinimized =
        ini.value(QStringLiteral("behavior/startMinimized"), settings.startMinimized).toBool();
    settings.closeToTray =
        ini.value(QStringLiteral("behavior/closeToTray"), settings.closeToTray).toBool();
    settings.subscriptionIntervalHours =
        ini.value(QStringLiteral("behavior/subscriptionIntervalHours"),
                  settings.subscriptionIntervalHours)
            .toInt();

    settings.theme = ini.value(QStringLiteral("appearance/theme"), settings.theme).toString();
    if (settings.theme != QStringLiteral("light") && settings.theme != QStringLiteral("system"))
        settings.theme = QStringLiteral("dark");

    if (!isValidPort(settings.mixedPort))
        settings.mixedPort = 7890;
    if (!isValidPort(settings.controllerPort))
        settings.controllerPort = 9090;
    if (settings.controllerPort == settings.mixedPort)
        settings.controllerPort = settings.mixedPort == 9090 ? 9091 : 9090;

    if (settings.secret.trimmed().isEmpty())
        settings.secret = createSecret();

    if (settings.mode != QStringLiteral("global") && settings.mode != QStringLiteral("direct"))
        settings.mode = QStringLiteral("rule");

    const QStringList levels = {QStringLiteral("trace"), QStringLiteral("debug"),
                                QStringLiteral("info"), QStringLiteral("warning"),
                                QStringLiteral("error")};
    if (!levels.contains(settings.logLevel))
        settings.logLevel = QStringLiteral("info");

    return settings;
}

void SettingsStore::save(const cq::AppSettings& settings)
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSettings ini(m_filePath, QSettings::IniFormat);

    ini.setValue(QStringLiteral("network/mixedPort"), settings.mixedPort);
    ini.setValue(QStringLiteral("network/controllerPort"), settings.controllerPort);
    ini.setValue(QStringLiteral("network/secret"), settings.secret);
    ini.setValue(QStringLiteral("network/allowLan"), settings.allowLan);
    ini.setValue(QStringLiteral("network/ipv6"), settings.ipv6);
    ini.setValue(QStringLiteral("network/mode"), settings.mode);
    ini.setValue(QStringLiteral("network/tun"), settings.tunEnabled);

    ini.setValue(QStringLiteral("core/logLevel"), settings.logLevel);
    ini.setValue(QStringLiteral("core/path"), settings.corePath);
    ini.setValue(QStringLiteral("core/delayTestUrl"), settings.delayTestUrl);

    ini.setValue(QStringLiteral("behavior/autoLaunch"), settings.autoLaunch);
    ini.setValue(QStringLiteral("behavior/startMinimized"), settings.startMinimized);
    ini.setValue(QStringLiteral("behavior/closeToTray"), settings.closeToTray);
    ini.setValue(QStringLiteral("behavior/subscriptionIntervalHours"),
                 settings.subscriptionIntervalHours);

    ini.setValue(QStringLiteral("appearance/theme"), settings.theme);
    ini.sync();
}

bool SettingsStore::applyLaunchAtLogin(bool enabled, QString* error)
{
#ifdef Q_OS_WIN
    QSettings runKey(QStringLiteral(
                         "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    const QString appName = QStringLiteral("Clash-Q");
    if (enabled) {
        const QString exe = QDir::toNativeSeparators(
            QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath());
        if (exe.isEmpty()) {
            if (error)
                *error = QStringLiteral("无法定位程序路径");
            return false;
        }
        runKey.setValue(appName, QStringLiteral("\"%1\" --autostart").arg(exe));
    } else {
        runKey.remove(appName);
    }
    runKey.sync();
    if (runKey.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("无法写入开机启动项");
        return false;
    }
    return true;
#else
    Q_UNUSED(enabled)
    if (error)
        *error = QStringLiteral("当前平台暂不支持开机启动");
    return false;
#endif
}
