#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QTimer;

namespace nq::scripts {

struct ScriptDownloadRequest {
    QString requestId;
    QString scriptId;
    QUrl url;
    QString etag;
    QString lastModified;
    int timeoutMs = 30000;
    qint64 maxBytes = 1024LL * 1024;
    bool manual = false;
};

struct ScriptDownloadResult {
    QString requestId;
    QString scriptId;
    bool ok = false;
    bool notModified = false;
    bool canceled = false;
    bool timedOut = false;
    bool tooLarge = false;
    bool httpError = false;
    int httpStatus = 0;
    QByteArray data;
    QString etag;
    QString lastModified;
    QString finalUrl;
    QString error;
    qint64 bytes = 0;
    bool manual = false;
};

// Downloads remote JavaScript through QNetworkAccessManager. This class only
// transports bytes; it never executes a script and never replaces storage on
// its own. Conditional requests (ETag / Last-Modified), redirects, timeouts
// and the maximum response size are handled here.
class ScriptDownloader : public QObject {
    Q_OBJECT

public:
    explicit ScriptDownloader(QObject* parent = nullptr);
    ~ScriptDownloader() override;

    void download(const ScriptDownloadRequest& request);
    void abortAll();
    bool isBusy(const QString& scriptId) const;
    bool isIdle() const { return m_pending.isEmpty(); }

    static bool isAllowedUrl(const QUrl& url, QString* error = nullptr);

signals:
    void finished(const nq::scripts::ScriptDownloadResult& result);

private:
    struct Pending {
        ScriptDownloadRequest request;
        QNetworkReply* reply = nullptr;
        QTimer* timer = nullptr;
        QByteArray data;
        bool canceled = false;
        bool timedOut = false;
        bool tooLarge = false;
    };

    void finish(Pending* pending);
    void cancel(Pending* pending);

    QNetworkAccessManager m_network;
    QHash<QNetworkReply*, Pending*> m_pending;
    QHash<QString, QNetworkReply*> m_byScript;
};

}  // namespace nq::scripts

Q_DECLARE_METATYPE(nq::scripts::ScriptDownloadRequest)
Q_DECLARE_METATYPE(nq::scripts::ScriptDownloadResult)
