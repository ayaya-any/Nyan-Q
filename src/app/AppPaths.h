#pragma once

#include <QString>

namespace AppPaths {

// Persistent application data. Set NYANQ_DATA_DIR (or the legacy CLASHQ_DATA_DIR) to override; a
// "portable.flag" beside the executable switches to <exe>/data as well.
QString dataDir();
QString profilesDir();
QString runtimeDir();
QString settingsFile();
QString logFile();
QString runtimeConfigFile();
QString coreLogFile();

QString bundledCorePath();
QString resolveCorePath(const QString& preferred = {});
QString appVersion();

void ensureDirectories();
void setDataDirOverride(const QString& path);
bool isPortable();

}  // namespace AppPaths
