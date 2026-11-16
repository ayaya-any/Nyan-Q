#include "service/ServiceInstaller.h"

#include "service/ServiceClient.h"
#include "service/ServiceProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#endif

namespace nq::service {
namespace {

QString createToken()
{
    QByteArray bytes(32, 0);
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    return QString::fromLatin1(bytes.toHex());
}

QString writeTokenFile(QString* error)
{
    const QString path = QDir(QDir::tempPath()).filePath(
        QStringLiteral("nyanq-service-token-%1.txt").arg(QCoreApplication::applicationPid()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法创建 Helper 安装令牌文件");
        return {};
    }
    file.write(createToken().toLatin1());
    file.close();
    return path;
}

}  // namespace

QString ServiceInstaller::currentUserSid()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(static_cast<int>(size), 0);
    if (size == 0 || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        CloseHandle(token);
        return {};
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.constData());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) {
        CloseHandle(token);
        return {};
    }
    const QString result = QString::fromWCharArray(sid);
    LocalFree(sid);
    CloseHandle(token);
    return result;
#else
    return {};
#endif
}

bool ServiceInstaller::runElevated(const QString& program, const QString& arguments,
                                   int timeoutMs, QString* error)
{
#ifdef Q_OS_WIN
    const QString nativeProgram = QDir::toNativeSeparators(program);
    const std::wstring file = nativeProgram.toStdWString();
    const std::wstring params = arguments.toStdWString();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError();
        if (error) {
            *error = code == ERROR_CANCELLED
                         ? QStringLiteral("已取消管理员授权")
                         : QStringLiteral("无法启动 Helper 安装程序（错误 %1）").arg(code);
        }
        return false;
    }
    if (!info.hProcess) {
        if (error)
            *error = QStringLiteral("Helper 安装程序没有返回进程句柄");
        return false;
    }
    const DWORD wait = WaitForSingleObject(info.hProcess, static_cast<DWORD>(timeoutMs));
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (wait == WAIT_TIMEOUT) {
        if (error)
            *error = QStringLiteral("Helper 安装超时");
        return false;
    }
    if (exitCode != 0) {
        if (error)
            *error = QStringLiteral("Helper 安装失败（退出码 %1）").arg(exitCode);
        return false;
    }
    return true;
#else
    Q_UNUSED(program)
    Q_UNUSED(arguments)
    Q_UNUSED(timeoutMs)
    if (error)
        *error = QStringLiteral("当前平台不支持 Nyan-Q Helper");
    return false;
#endif
}

bool ServiceInstaller::install(QString* error)
{
    const QString source = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("nyanq-service.exe"));
    if (!QFile::exists(source)) {
        if (error)
            *error = QStringLiteral("找不到 nyanq-service.exe，请重新安装 Nyan-Q");
        return false;
    }

    const QString sid = currentUserSid();
    if (sid.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法获取当前用户 SID");
        return false;
    }

    const QString tokenFile = writeTokenFile(error);
    if (tokenFile.isEmpty())
        return false;

    const QString arguments =
        QStringLiteral("--install --token-file \"%1\" --owner-sid \"%2\"")
            .arg(QDir::toNativeSeparators(tokenFile), sid);
    const bool installed = runElevated(source, arguments, 120000, error);
    QFile::remove(tokenFile);
    if (!installed)
        return false;

    ServiceClient::invalidateToken();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        if (ServiceClient::ping())
            return true;
        QThread::msleep(150);
    }
    if (error)
        *error = QStringLiteral("Helper 服务安装后未能就绪");
    return false;
}

bool ServiceInstaller::uninstall(QString* error)
{
    if (!QFile::exists(serviceExePath())) {
        if (error)
            *error = QStringLiteral("Helper 服务尚未安装");
        return false;
    }
    const bool removed = runElevated(serviceExePath(), QStringLiteral("--uninstall"), 30000, error);
    ServiceClient::invalidateToken();
    return removed;
}

}  // namespace nq::service
