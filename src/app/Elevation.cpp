#include "app/Elevation.h"

#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace {

#ifdef Q_OS_WIN
QString quoteArgument(const QString& argument)
{
    if (!argument.contains(QLatin1Char(' ')) && !argument.contains(QLatin1Char('\t')) &&
        !argument.contains(QLatin1Char('"'))) {
        return argument;
    }

    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar character : argument) {
        if (character == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            quoted += QLatin1Char('"');
            backslashes = 0;
            continue;
        }
        quoted += QString(backslashes, QLatin1Char('\\'));
        quoted += character;
        backslashes = 0;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}
#endif

}  // namespace

namespace Elevation {

RelaunchResult relaunchAsAdministrator(const QStringList& arguments, QString* error)
{
#ifdef Q_OS_WIN
    QStringList quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const QString& argument : arguments)
        quotedArguments.append(quoteArgument(argument));

    const QString executable =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workingDirectory =
        QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString parameters = quotedArguments.join(QLatin1Char(' '));

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInitialized = SUCCEEDED(comResult);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    info.lpParameters =
        parameters.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(parameters.utf16());
    info.lpDirectory = reinterpret_cast<LPCWSTR>(workingDirectory.utf16());
    info.nShow = SW_SHOWNORMAL;

    const BOOL started = ShellExecuteExW(&info);
    const DWORD errorCode = started ? ERROR_SUCCESS : GetLastError();
    if (info.hProcess)
        CloseHandle(info.hProcess);
    if (comInitialized)
        CoUninitialize();

    if (!started) {
        if (errorCode == ERROR_CANCELLED)
            return RelaunchResult::Canceled;
        if (error)
            *error = QStringLiteral("无法请求管理员权限（错误码 %1）").arg(errorCode);
        return RelaunchResult::Failed;
    }
    return RelaunchResult::Started;
#else
    Q_UNUSED(arguments)
    if (error)
        *error = QStringLiteral("当前平台不支持管理员提权重启");
    return RelaunchResult::Failed;
#endif
}

}  // namespace Elevation
