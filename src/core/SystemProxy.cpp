#include "core/SystemProxy.h"

#ifdef Q_OS_WIN
#include <QSettings>
#include <windows.h>
#include <wininet.h>
#endif

namespace {
#ifdef Q_OS_WIN
QString internetSettingsPath()
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings");
}
#endif
}  // namespace

SystemProxy::SystemProxy(QObject* parent)
    : QObject(parent)
{
}

SystemProxy::~SystemProxy()
{
    restoreOriginal();
}

bool SystemProxy::supported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void SystemProxy::captureOriginal()
{
#ifdef Q_OS_WIN
    if (m_captured)
        return;
    QSettings settings(internetSettingsPath(), QSettings::NativeFormat);
    m_hadEnable = settings.contains(QStringLiteral("ProxyEnable"));
    m_hadServer = settings.contains(QStringLiteral("ProxyServer"));
    m_hadOverride = settings.contains(QStringLiteral("ProxyOverride"));
    m_originalEnable = settings.value(QStringLiteral("ProxyEnable"));
    m_originalServer = settings.value(QStringLiteral("ProxyServer"));
    m_originalOverride = settings.value(QStringLiteral("ProxyOverride"));
    m_captured = true;
#endif
}

void SystemProxy::notifySystem()
{
#ifdef Q_OS_WIN
    InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
#endif
}

bool SystemProxy::applyRegistry(bool enabled, const QString& server, const QString& bypass,
                                QString* error)
{
#ifdef Q_OS_WIN
    QSettings settings(internetSettingsPath(), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("ProxyEnable"), enabled ? 1 : 0);
    if (enabled) {
        settings.setValue(QStringLiteral("ProxyServer"), server);
        settings.setValue(QStringLiteral("ProxyOverride"), bypass);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("无法写入 Windows 代理设置（可能被组策略限制）");
        return false;
    }
    notifySystem();
    return true;
#else
    Q_UNUSED(enabled)
    Q_UNUSED(server)
    Q_UNUSED(bypass)
    if (error)
        *error = QStringLiteral("当前平台不支持自动设置系统代理");
    return false;
#endif
}

bool SystemProxy::enable(int port, QString* error)
{
    if (port <= 0 || port > 65535) {
        if (error)
            *error = QStringLiteral("混合端口无效");
        return false;
    }
    captureOriginal();
    const QString server = QStringLiteral("127.0.0.1:%1").arg(port);
    const QString bypass = QStringLiteral("<local>;localhost;127.*;10.*;172.16.*;172.17.*;"
                                          "172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;"
                                          "172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;"
                                          "172.28.*;172.29.*;172.30.*;172.31.*;192.168.*");
    if (!applyRegistry(true, server, bypass, error))
        return false;
    m_applied = true;
    m_port = port;
    emit stateChanged(true);
    return true;
}

bool SystemProxy::disable(QString* error)
{
    if (!m_captured) {
        if (error)
            *error = QStringLiteral("没有可恢复的系统代理状态");
        return false;
    }
#ifdef Q_OS_WIN
    QSettings settings(internetSettingsPath(), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("ProxyEnable"),
                      m_hadEnable ? m_originalEnable : QVariant(0));
    if (m_hadServer)
        settings.setValue(QStringLiteral("ProxyServer"), m_originalServer);
    else
        settings.remove(QStringLiteral("ProxyServer"));
    if (m_hadOverride)
        settings.setValue(QStringLiteral("ProxyOverride"), m_originalOverride);
    else
        settings.remove(QStringLiteral("ProxyOverride"));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("无法恢复 Windows 代理设置");
        return false;
    }
    notifySystem();
#endif
    const bool wasApplied = m_applied;
    m_applied = false;
    m_port = 0;
    if (wasApplied)
        emit stateChanged(false);
    return true;
}

bool SystemProxy::isEnabled() const
{
#ifdef Q_OS_WIN
    QSettings settings(internetSettingsPath(), QSettings::NativeFormat);
    const bool enabled = settings.value(QStringLiteral("ProxyEnable"), 0).toInt() != 0;
    if (!enabled)
        return false;
    const QString server = settings.value(QStringLiteral("ProxyServer")).toString();
    return server.contains(QStringLiteral("127.0.0.1")) ||
           server.contains(QStringLiteral("localhost"));
#else
    return false;
#endif
}

void SystemProxy::restoreOriginal()
{
    if (!m_applied)
        return;
    disable();
}
