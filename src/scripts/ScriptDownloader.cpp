#include "scripts/ScriptDownloader.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace nq::scripts {
namespace {

constexpr qint64 kMaxDownloadBytes = 8LL * 1024 * 1024;

bool isLoopbackHost(const QString& host)
{
    const QString lower = host.trimmed().toLower();
    return lower == QStringLiteral("localhost") || lower == QStringLiteral("127.0.0.1") ||
           lower == QStringLiteral("::1") || lower == QStringLiteral("[::1]");
}

QString headerText(QNetworkReply* reply, const QByteArray& name, const QString& fallback)
{
    const QByteArray value = reply->rawHeader(name);
    return value.isEmpty() ? fallback : QString::fromUtf8(value);
}

}  // namespace

ScriptDownloader::ScriptDownloader(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ScriptDownloadResult>("nq::scripts::ScriptDownloadResult");
    qRegisterMetaType<ScriptDownloadRequest>("nq::scripts::ScriptDownloadRequest");
}

ScriptDownloader::~ScriptDownloader()
{
    const QList<Pending*> pending = m_pending.values();
    m_pending.clear();
    m_byScript.clear();
    for (Pending* item : pending) {
        if (!item)
            continue;
        if (item->reply) {
            item->reply->disconnect(this);
            item->reply->abort();
            item->reply->deleteLater();
        }
        delete item->timer;
        delete item;
    }
}

bool ScriptDownloader::isAllowedUrl(const QUrl& url, QString* error)
{
    if (error)
        error->clear();
    if (!url.isValid() || url.host().isEmpty()) {
        if (error)
            *error = QStringLiteral("脚本地址不是有效的 URL");
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("https"))
        return true;
    if (scheme == QStringLiteral("http") && isLoopbackHost(url.host()))
        return true;
    if (error)
        *error = QStringLiteral("远程脚本必须使用 HTTPS 地址（仅允许回环地址使用 HTTP 进行本地测试）");
    return false;
}

bool ScriptDownloader::isBusy(const QString& scriptId) const
{
    return m_byScript.contains(scriptId);
}

void ScriptDownloader::download(const ScriptDownloadRequest& request)
{
    ScriptDownloadResult rejected;
    rejected.requestId = request.requestId;
    rejected.scriptId = request.scriptId;
    rejected.manual = request.manual;
    rejected.error.clear();

    QString urlError;
    if (!isAllowedUrl(request.url, &urlError)) {
        rejected.error = urlError;
        emit finished(rejected);
        return;
    }
    if (request.scriptId.isEmpty()) {
        rejected.error = QStringLiteral("下载请求缺少脚本 ID");
        emit finished(rejected);
        return;
    }
    if (m_byScript.contains(request.scriptId)) {
        rejected.error = QStringLiteral("该脚本正在更新，请稍后重试");
        emit finished(rejected);
        return;
    }

    QNetworkRequest networkRequest(request.url);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader,
                             QStringLiteral("Nyan-Q/%1")
                                 .arg(QCoreApplication::applicationVersion()));
    networkRequest.setRawHeader("Accept",
                                "application/javascript, text/javascript, text/plain, */*");
    networkRequest.setRawHeader("Accept-Encoding", "identity");
    networkRequest.setRawHeader("Cache-Control", "no-cache");
    if (!request.etag.trimmed().isEmpty())
        networkRequest.setRawHeader("If-None-Match", request.etag.trimmed().toUtf8());
    if (!request.lastModified.trimmed().isEmpty())
        networkRequest.setRawHeader("If-Modified-Since",
                                    request.lastModified.trimmed().toUtf8());
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                               QNetworkRequest::NoLessSafeRedirectPolicy);
    networkRequest.setMaximumRedirectsAllowed(5);
    networkRequest.setTransferTimeout(
        qBound(1000, (request.timeoutMs > 0 ? request.timeoutMs : 30000) + 5000, 120000));

    auto* pending = new Pending;
    pending->request = request;
    pending->request.maxBytes = qBound<qint64>(1024, request.maxBytes, kMaxDownloadBytes);
    pending->request.timeoutMs = qBound(1000, request.timeoutMs, 120000);

    pending->reply = m_network.get(networkRequest);
    m_pending.insert(pending->reply, pending);
    m_byScript.insert(request.scriptId, pending->reply);

    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    connect(pending->timer, &QTimer::timeout, this, [this, pending] {
        if (!m_pending.contains(pending->reply))
            return;
        pending->timedOut = true;
        if (pending->reply)
            pending->reply->abort();
    });
    pending->timer->start(pending->request.timeoutMs);

    QNetworkReply* reply = pending->reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, pending] {
        if (!m_pending.contains(reply) || !reply->isOpen())
            return;
        pending->data += reply->readAll();
        if (pending->data.size() > pending->request.maxBytes) {
            pending->tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        Pending* item = m_pending.value(reply, nullptr);
        if (item)
            finish(item);
    });
}

void ScriptDownloader::finish(Pending* pending)
{
    if (!pending || !pending->reply)
        return;
    QNetworkReply* reply = pending->reply;
    if (!m_pending.contains(reply))
        return;

    if (reply->isOpen())
        pending->data += reply->readAll();
    if (pending->timer)
        pending->timer->stop();
    ScriptDownloadResult result;
    result.requestId = pending->request.requestId;
    result.scriptId = pending->request.scriptId;
    result.manual = pending->request.manual;
    result.finalUrl = reply->url().toString();
    result.etag = headerText(reply, "ETag", pending->request.etag);
    result.lastModified = headerText(reply, "Last-Modified", pending->request.lastModified);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.httpStatus = status;
    result.bytes = pending->data.size();

    if (pending->tooLarge || pending->data.size() > pending->request.maxBytes) {
        result.error = QStringLiteral("远程脚本超过 %1 KB 大小限制")
                           .arg(pending->request.maxBytes / 1024);
        result.tooLarge = true;
    } else if (pending->canceled) {
        result.canceled = true;
        result.error = QStringLiteral("下载已取消");
    } else if (pending->timedOut) {
        result.timedOut = true;
        result.error = QStringLiteral("下载脚本超时（%1 ms）").arg(pending->request.timeoutMs);
    } else if (reply->error() != QNetworkReply::NoError && status != 304) {
        if (status >= 400) {
            result.httpError = true;
            result.error = QStringLiteral("脚本服务器返回 HTTP %1").arg(status);
        } else {
            result.error = QStringLiteral("下载脚本失败：%1").arg(reply->errorString());
        }
    } else if (status >= 400) {
        result.httpError = true;
        result.error = QStringLiteral("脚本服务器返回 HTTP %1").arg(status);
    } else if (status == 304) {
        result.ok = true;
        result.notModified = true;
    } else if (pending->data.isEmpty()) {
        result.error = QStringLiteral("远程脚本内容为空");
    } else if (pending->data.trimmed().startsWith('<')) {
        result.error = QStringLiteral("服务器返回了网页而不是 JavaScript 脚本");
    } else if (pending->data.contains('\0')) {
        result.error = QStringLiteral("远程脚本不是有效的 UTF-8 文本");
    } else {
        result.ok = true;
        result.data = pending->data;
    }

    m_pending.remove(reply);
    if (m_byScript.value(pending->request.scriptId) == reply)
        m_byScript.remove(pending->request.scriptId);

    reply->deleteLater();
    if (pending->timer) {
        pending->timer->stop();
        pending->timer->deleteLater();
    }
    delete pending;

    emit finished(result);
}

void ScriptDownloader::cancel(Pending* pending)
{
    if (!pending || !pending->reply)
        return;
    pending->canceled = true;
    pending->reply->abort();
}

void ScriptDownloader::abortAll()
{
    const QList<Pending*> items = m_pending.values();
    for (Pending* item : items)
        cancel(item);
}

}  // namespace nq::scripts
