#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace nq::service {

inline constexpr int kProtocolVersion = 1;
inline constexpr char kPipeName[] = "nyanq-service";
inline constexpr char kServiceName[] = "NyanQHelperService";

inline QString programDataDir()
{
    const QString overrideDir = qEnvironmentVariable("NYANQ_SERVICE_DIR");
    if (!overrideDir.isEmpty())
        return QDir::cleanPath(overrideDir);
    QString base = qEnvironmentVariable("ProgramData");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("Nyan-Q"));
}

inline QString installDir()
{
    return programDataDir();
}

inline QString serviceExePath()
{
    return QDir(installDir()).filePath(QStringLiteral("nyanq-service.exe"));
}

inline QString coreDirectory()
{
    return QDir(installDir()).filePath(QStringLiteral("core"));
}

inline QString corePath()
{
    return QDir(coreDirectory()).filePath(QStringLiteral("mihomo.exe"));
}

inline QString tokenPath()
{
    return QDir(installDir()).filePath(QStringLiteral("service.token"));
}

inline QString serviceLogPath()
{
    return QDir(installDir()).filePath(QStringLiteral("service.log"));
}

}  // namespace nq::service
