#include "service/ServiceProtocol.h"
#include "service/ServiceRuntime.h"
#include "service/ServiceServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringList>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#include <winsvc.h>

#include <cstdio>

namespace {

constexpr wchar_t kServiceName[] = L"NyanQHelperService";
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
QEventLoop* g_loop = nullptr;

QString serviceTokenPath()
{
    return nq::service::tokenPath();
}

QString serviceExecutablePath()
{
    return nq::service::serviceExePath();
}

bool copyFile(const QString& source, const QString& target, QString* error)
{
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(target).absoluteFilePath())
        return true;
    if (QFile::exists(target))
        QFile::remove(target);
    if (!QFile::copy(source, target)) {
        if (error)
            *error = QStringLiteral("无法复制 %1 到 %2").arg(source, target);
        return false;
    }
    return true;
}

bool setTokenAcl(const QString& path, const QString& ownerSid, QString* error)
{
    if (!ownerSid.startsWith(QStringLiteral("S-1-"))) {
        if (error)
            *error = QStringLiteral("用户 SID 无效");
        return false;
    }
    const QString sddl = QStringLiteral("D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;%1)")
                             .arg(ownerSid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) {
        if (error)
            *error = QStringLiteral("无法构建 Helper 令牌权限");
        return false;
    }
    const BOOL ok = SetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                     DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    if (!ok) {
        if (error)
            *error = QStringLiteral("无法保护 Helper 令牌文件");
        return false;
    }
    return true;
}

void stopAndDeleteService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return;
    SC_HANDLE service = OpenServiceW(
        scm, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service) {
        SERVICE_STATUS status = {};
        ControlService(service, SERVICE_CONTROL_STOP, &status);
        for (int i = 0; i < 50; ++i) {
            SERVICE_STATUS current = {};
            if (!QueryServiceStatus(service, &current) ||
                current.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(100);
        }
        DeleteService(service);
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
}

bool installService(const QStringList& args, QString* error)
{
    QString tokenFile;
    QString ownerSid;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--token-file") && i + 1 < args.size())
            tokenFile = args.at(++i);
        else if (args.at(i) == QStringLiteral("--owner-sid") && i + 1 < args.size())
            ownerSid = args.at(++i);
    }

    QFile tokenSource(tokenFile);
    if (!tokenSource.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取安装令牌");
        return false;
    }
    const QByteArray token = tokenSource.readAll().trimmed();
    if (token.size() != 64) {
        if (error)
            *error = QStringLiteral("安装令牌格式无效");
        return false;
    }

    const QString sourceDir = QCoreApplication::applicationDirPath();
    const QString targetDir = nq::service::installDir();
    const QString coreDir = nq::service::coreDirectory();
    if (!QDir().mkpath(coreDir)) {
        if (error)
            *error = QStringLiteral("无法创建 Helper 安装目录");
        return false;
    }

    const QStringList requiredSources = {
        QDir(sourceDir).filePath(QStringLiteral("nyanq-service.exe")),
        QDir(sourceDir).filePath(QStringLiteral("Qt6Core.dll")),
        QDir(sourceDir).filePath(QStringLiteral("Qt6Network.dll")),
        QDir(sourceDir).filePath(QStringLiteral("vcruntime140.dll")),
        QDir(sourceDir).filePath(QStringLiteral("vcruntime140_1.dll")),
        QDir(sourceDir).filePath(QStringLiteral("msvcp140.dll")),
        QDir(sourceDir).filePath(QStringLiteral("mihomo.exe")),
        QDir(sourceDir).filePath(QStringLiteral("wintun.dll")),
    };
    for (const QString& source : requiredSources) {
        if (!QFileInfo::exists(source)) {
            if (error)
                *error = QStringLiteral("Helper 安装文件缺失：%1").arg(source);
            return false;
        }
    }

    stopAndDeleteService();

    const QStringList rootFiles = {
        QStringLiteral("nyanq-service.exe"), QStringLiteral("Qt6Core.dll"),
        QStringLiteral("Qt6Network.dll"), QStringLiteral("vcruntime140.dll"),
        QStringLiteral("vcruntime140_1.dll"), QStringLiteral("msvcp140.dll"),
    };
    for (const QString& name : rootFiles) {
        if (!copyFile(QDir(sourceDir).filePath(name), QDir(targetDir).filePath(name), error))
            return false;
    }
    for (const QString& name : {QStringLiteral("mihomo.exe"), QStringLiteral("wintun.dll")}) {
        if (!copyFile(QDir(sourceDir).filePath(name), QDir(coreDir).filePath(name), error))
            return false;
    }

    QFile tokenTarget(serviceTokenPath());
    if (!tokenTarget.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 Helper 令牌");
        return false;
    }
    tokenTarget.write(token);
    tokenTarget.close();
    if (!setTokenAcl(serviceTokenPath(), ownerSid, error))
        return false;

    const QString binary = QStringLiteral("\"%1\" --service")
                               .arg(QDir::toNativeSeparators(serviceExecutablePath()));
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        if (error)
            *error = QStringLiteral("无法打开服务管理器（错误 %1）").arg(GetLastError());
        return false;
    }
    SC_HANDLE service = CreateServiceW(
        scm, kServiceName, L"Nyan-Q Helper", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        reinterpret_cast<LPCWSTR>(binary.utf16()), nullptr, nullptr, nullptr, nullptr,
        nullptr);
    if (!service) {
        const DWORD code = GetLastError();
        CloseServiceHandle(scm);
        if (error)
            *error = QStringLiteral("无法创建 Helper 服务（错误 %1）").arg(code);
        return false;
    }
    SERVICE_DESCRIPTIONW description = {};
    description.lpDescription = const_cast<LPWSTR>(L"Nyan-Q TUN/Core privileged helper");
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
    if (!StartServiceW(service, 0, nullptr)) {
        const DWORD code = GetLastError();
        DeleteService(service);
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        if (error)
            *error = QStringLiteral("无法启动 Helper 服务（错误 %1）").arg(code);
        return false;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool uninstallService(QString* error)
{
    stopAndDeleteService();
    const QString dir = nq::service::installDir();
    if (QDir(dir).exists() && !QDir(dir).removeRecursively()) {
        if (error)
            *error = QStringLiteral("服务已删除，但部分文件仍被占用");
    }
    return true;
}

void setStatus(DWORD state, DWORD exitCode = 0)
{
    if (!g_statusHandle)
        return;
    SERVICE_STATUS status = {};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted =
        state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = exitCode;
    SetServiceStatus(g_statusHandle, &status);
}

DWORD WINAPI controlHandler(DWORD control, DWORD, LPVOID, LPVOID)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        if (g_loop)
            QMetaObject::invokeMethod(g_loop, [loop = g_loop] { loop->quit(); },
                                      Qt::QueuedConnection);
        return NO_ERROR;
    }
    if (control == SERVICE_CONTROL_INTERROGATE)
        return NO_ERROR;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI serviceMain(DWORD, LPWSTR*)
{
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, controlHandler, nullptr);
    if (!g_statusHandle)
        return;
    setStatus(SERVICE_START_PENDING);

    QFile tokenFile(serviceTokenPath());
    if (!tokenFile.open(QIODevice::ReadOnly)) {
        setStatus(SERVICE_STOPPED, ERROR_FILE_NOT_FOUND);
        return;
    }
    const QString token = QString::fromLatin1(tokenFile.readAll()).trimmed();

    nq::service::ServiceRuntime runtime;
    nq::service::ServiceServer server(token, &runtime);
    QString error;
    if (!server.start(&error)) {
        setStatus(SERVICE_STOPPED, ERROR_SERVICE_NOT_ACTIVE);
        return;
    }

    QEventLoop loop;
    g_loop = &loop;
    setStatus(SERVICE_RUNNING);
    loop.exec();
    g_loop = nullptr;
    server.stop();
    runtime.stop();
    setStatus(SERVICE_STOPPED);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QString error;

    if (args.contains(QStringLiteral("--install"))) {
        if (!installService(args, &error)) {
            std::fprintf(stderr, "%s\n", error.toUtf8().constData());
            return 1;
        }
        return 0;
    }
    if (args.contains(QStringLiteral("--uninstall"))) {
        if (!uninstallService(&error)) {
            std::fprintf(stderr, "%s\n", error.toUtf8().constData());
            return 1;
        }
        return 0;
    }
    if (args.contains(QStringLiteral("--standalone"))) {
        QFile tokenFile(serviceTokenPath());
        if (!tokenFile.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read service token\n");
            return 1;
        }
        const QString token = QString::fromLatin1(tokenFile.readAll()).trimmed();
        nq::service::ServiceRuntime runtime;
        nq::service::ServiceServer server(token, &runtime);
        if (!server.start(&error)) {
            std::fprintf(stderr, "%s\n", error.toUtf8().constData());
            return 1;
        }
        const int result = app.exec();
        server.stop();
        runtime.stop();
        return result;
    }
    if (!args.contains(QStringLiteral("--service"))) {
        std::fprintf(stderr, "nyanq-service: use --install, --uninstall, --standalone or --service\n");
        return 1;
    }

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(kServiceName), serviceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        std::fprintf(stderr, "StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
        return 1;
    }
    return 0;
}

#else

int main()
{
    return 1;
}

#endif
