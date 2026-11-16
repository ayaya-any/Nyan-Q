#pragma once

#include <QString>

namespace nq::service {

class ServiceInstaller final {
public:
    static bool install(QString* error = nullptr);
    static bool uninstall(QString* error = nullptr);
    static QString currentUserSid();

private:
    static bool runElevated(const QString& program, const QString& arguments, int timeoutMs,
                            QString* error);
};

}  // namespace nq::service
