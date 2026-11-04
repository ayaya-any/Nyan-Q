#pragma once

#include "core/Types.h"

#include <QString>

class SettingsStore {
public:
    explicit SettingsStore(const QString& filePath);

    cq::AppSettings load();
    void save(const cq::AppSettings& settings);

    static QString createSecret();
    static bool isValidPort(int port);
    static bool applyLaunchAtLogin(bool enabled, QString* error = nullptr);

    QString filePath() const { return m_filePath; }

private:
    QString m_filePath;
};
