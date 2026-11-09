#include "scripts/ScriptTypes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

namespace nq::scripts {

QString sourceKindLabel(ScriptSourceKind kind)
{
    return kind == ScriptSourceKind::Remote ? QStringLiteral("远程")
                                            : QStringLiteral("本地");
}

QString trustStateLabel(ScriptTrustState state)
{
    switch (state) {
    case ScriptTrustState::Local:
        return QStringLiteral("本地脚本");
    case ScriptTrustState::Untrusted:
        return QStringLiteral("来源未确认");
    case ScriptTrustState::Trusted:
        return QStringLiteral("已确认来源");
    case ScriptTrustState::Pinned:
        return QStringLiteral("哈希已固定");
    case ScriptTrustState::PinMismatch:
        return QStringLiteral("哈希不匹配");
    }
    return {};
}

QString runStatusLabel(ScriptRunStatus status)
{
    switch (status) {
    case ScriptRunStatus::Success:
        return QStringLiteral("成功");
    case ScriptRunStatus::Skipped:
        return QStringLiteral("已跳过");
    case ScriptRunStatus::Failed:
        return QStringLiteral("失败");
    case ScriptRunStatus::TimedOut:
        return QStringLiteral("超时");
    case ScriptRunStatus::MemoryExceeded:
        return QStringLiteral("内存超限");
    case ScriptRunStatus::Canceled:
        return QStringLiteral("已取消");
    }
    return {};
}

QString logLevelLabel(ScriptLogLevel level)
{
    switch (level) {
    case ScriptLogLevel::Debug:
        return QStringLiteral("DEBUG");
    case ScriptLogLevel::Info:
        return QStringLiteral("INFO");
    case ScriptLogLevel::Warning:
        return QStringLiteral("WARN");
    case ScriptLogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return {};
}

ScriptTrustState trustState(const ScriptMeta& script)
{
    if (!script.remote())
        return ScriptTrustState::Local;
    if (!script.expectedSha256.trimmed().isEmpty()) {
        return script.expectedSha256.compare(script.clientSha256, Qt::CaseInsensitive) == 0
                   ? ScriptTrustState::Pinned
                   : ScriptTrustState::PinMismatch;
    }
    return script.trusted ? ScriptTrustState::Trusted : ScriptTrustState::Untrusted;
}

QString maskSecretUrl(const QString& raw)
{
    const QUrl url(raw);
    if (!url.isValid() || url.query().isEmpty())
        return raw;

    QUrl masked = url;
    QUrlQuery query(url);
    QUrlQuery replacement;
    const auto items = query.queryItems(QUrl::FullyEncoded);
    for (const auto& item : items) {
        QString value = item.second;
        if (!value.isEmpty() && (item.first.contains(QStringLiteral("token"), Qt::CaseInsensitive) ||
                                 item.first.contains(QStringLiteral("key"), Qt::CaseInsensitive) ||
                                 item.first.contains(QStringLiteral("secret"), Qt::CaseInsensitive) ||
                                 item.first.contains(QStringLiteral("sign"), Qt::CaseInsensitive) ||
                                 item.first.contains(QStringLiteral("password"), Qt::CaseInsensitive))) {
            value = QStringLiteral("***");
        }
        replacement.addQueryItem(item.first, value);
    }
    masked.setQuery(replacement);
    return masked.toString(QUrl::FullyEncoded);
}

QByteArray sha256Hex(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool writeFileAtomic(const QString& path, const QByteArray& data, QString* error)
{
    if (error)
        error->clear();
    const QFileInfo info(path);
    if (!info.absolutePath().isEmpty())
        QDir().mkpath(info.absolutePath());

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(QDir::toNativeSeparators(path),
                                                          file.errorString());
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("写入 %1 失败").arg(QDir::toNativeSeparators(path));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 %1 失败：%2").arg(QDir::toNativeSeparators(path),
                                                          file.errorString());
        return false;
    }
    return true;
}

bool copyFileAtomic(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(QDir::toNativeSeparators(sourcePath),
                                                          source.errorString());
        return false;
    }
    return writeFileAtomic(destinationPath, source.readAll(), error);
}

}  // namespace nq::scripts
