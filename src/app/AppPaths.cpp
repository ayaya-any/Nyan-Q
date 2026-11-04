#include "app/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString g_dataDirOverride;

QString defaultDataDir()
{
    if (!g_dataDirOverride.isEmpty())
        return QDir::cleanPath(g_dataDirOverride);

    QString fromEnv = qEnvironmentVariable("NYANQ_DATA_DIR");
    if (fromEnv.isEmpty())
        fromEnv = qEnvironmentVariable("CLASHQ_DATA_DIR");
    if (!fromEnv.isEmpty())
        return QDir::cleanPath(fromEnv);

    const QDir exeDir(QCoreApplication::applicationDirPath());
    if (QFile::exists(exeDir.filePath(QStringLiteral("portable.flag"))))
        return QDir::cleanPath(exeDir.filePath(QStringLiteral("data")));

    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString dataDir = QDir::cleanPath(base + QStringLiteral("/Nyan-Q"));
    const QString legacyDir = QDir::cleanPath(base + QStringLiteral("/Clash-Q"));
    if (!QFileInfo::exists(dataDir) && QFileInfo::exists(legacyDir)) {
        // Preserve existing profiles, subscriptions and settings across the
        // Clash-Q -> Nyan-Q rename. If the move fails, keep using the legacy
        // directory instead of silently starting with an empty profile store.
        if (!QDir().rename(legacyDir, dataDir))
            return legacyDir;
    }
    return dataDir;
}

QString resolvedDataDir()
{
    static const QString dir = defaultDataDir();
    return dir;
}
}  // namespace

namespace AppPaths {

QString dataDir()
{
    return resolvedDataDir();
}

QString profilesDir()
{
    return dataDir() + QStringLiteral("/profiles");
}

QString runtimeDir()
{
    return dataDir() + QStringLiteral("/runtime");
}

QString settingsFile()
{
    return dataDir() + QStringLiteral("/settings.ini");
}

QString logFile()
{
    return dataDir() + QStringLiteral("/nyan-q.log");
}

QString runtimeConfigFile()
{
    return runtimeDir() + QStringLiteral("/config.yaml");
}

QString coreLogFile()
{
    return runtimeDir() + QStringLiteral("/core.log");
}

void ensureDirectories()
{
    QDir().mkpath(profilesDir());
    QDir().mkpath(runtimeDir());
}

QString bundledCorePath()
{
    const QDir exeDir(QCoreApplication::applicationDirPath());
    const QString besideExe = exeDir.filePath(QStringLiteral("mihomo.exe"));
    if (QFileInfo::exists(besideExe))
        return besideExe;

    return {};
}

QString resolveCorePath(const QString& preferred)
{
    if (!preferred.trimmed().isEmpty()) {
        const QFileInfo info(preferred);
        if (info.exists() && info.isFile())
            return info.absoluteFilePath();
        return {};
    }

    const QString bundled = bundledCorePath();
    if (!bundled.isEmpty())
        return bundled;

    return QStandardPaths::findExecutable(QStringLiteral("mihomo"));
}

QString appVersion()
{
    return QStringLiteral(NYANQ_VERSION);
}

void setDataDirOverride(const QString& path)
{
    g_dataDirOverride = QDir::cleanPath(path);
}

bool isPortable()
{
    const QDir exeDir(QCoreApplication::applicationDirPath());
    return QFile::exists(exeDir.filePath(QStringLiteral("portable.flag")));
}

}  // namespace AppPaths
