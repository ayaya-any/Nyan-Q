#include "scripts/ScriptDownloader.h"
#include "http_test_server.h"

#include <QSignalSpy>
#include <QTest>

using namespace nq::scripts;

namespace {
ScriptDownloadRequest requestFor(const QUrl& url, int timeoutMs = 5000, qint64 maxBytes = 4096)
{
    ScriptDownloadRequest request;
    request.requestId = QStringLiteral("req");
    request.scriptId = QStringLiteral("script");
    request.url = url;
    request.timeoutMs = timeoutMs;
    request.maxBytes = maxBytes;
    return request;
}

ScriptDownloadResult waitFor(ScriptDownloader& downloader, QSignalSpy& spy)
{
    if (spy.isEmpty())
        spy.wait(5000);
    if (spy.isEmpty())
        return {};
    return qvariant_cast<ScriptDownloadResult>(spy.takeFirst().first());
}
}  // namespace

class ScriptDownloaderTest : public QObject {
    Q_OBJECT

private slots:
    void downloadsScriptAndCapturesValidators();
    void sendsConditionalHeaders();
    void handlesNotModified();
    void reportsHttpErrors();
    void timesOutHangingRequest();
    void rejectsOversizedResponse();
    void rejectsUnsafeUrls();
    void rejectsConcurrentSameScript();
};

void ScriptDownloaderTest::downloadsScriptAndCapturesValidators()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "function main(config) { return config; }\n";
    route.etag = "\"v1\"";
    route.lastModified = "Wed, 21 Oct 2015 07:28:00 GMT";
    server.setRoute(QStringLiteral("/script.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/script.js"))));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.data, route.body);
    QCOMPARE(result.etag, QStringLiteral("\"v1\""));
    QCOMPARE(result.lastModified, QString::fromLatin1(route.lastModified));
    QCOMPARE(result.httpStatus, 200);
    QCOMPARE(result.bytes, static_cast<qint64>(route.body.size()));
}

void ScriptDownloaderTest::sendsConditionalHeaders()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = "x";
    route.etag = "\"v2\"";
    route.lastModified = "Wed, 21 Oct 2015 07:28:00 GMT";
    server.setRoute(QStringLiteral("/conditional.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    ScriptDownloadRequest request = requestFor(server.url(QStringLiteral("/conditional.js")));
    request.etag = QStringLiteral("\"v2\"");
    request.lastModified = QStringLiteral("Wed, 21 Oct 2015 07:28:00 GMT");
    downloader.download(request);
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    const QHash<QByteArray, QByteArray> headers =
        server.lastRequestHeaders(QStringLiteral("/conditional.js"));
    QCOMPARE(headers.value("if-none-match"), QByteArray("\"v2\""));
    QVERIFY(headers.value("if-modified-since").contains("21 Oct 2015"));
}

void ScriptDownloaderTest::handlesNotModified()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.status = 304;
    route.etag = "\"same\"";
    server.setRoute(QStringLiteral("/same.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/same.js"))));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.notModified);
    QCOMPARE(result.data.size(), 0);
    QCOMPARE(result.etag, QStringLiteral("\"same\""));
}

void ScriptDownloaderTest::reportsHttpErrors()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.status = 500;
    route.body = "nope";
    server.setRoute(QStringLiteral("/error.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/error.js"))));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY(!result.ok);
    QVERIFY(result.httpError);
    QCOMPARE(result.httpStatus, 500);
    QVERIFY(result.error.contains(QStringLiteral("HTTP 500")));
}

void ScriptDownloaderTest::timesOutHangingRequest()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.hang = true;
    server.setRoute(QStringLiteral("/hang.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/hang.js")), 1000));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY(!result.ok);
    QVERIFY(result.timedOut);
    QVERIFY(result.error.contains(QStringLiteral("超时")));
}

void ScriptDownloaderTest::rejectsOversizedResponse()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.body = QByteArray(4096, 'a');
    server.setRoute(QStringLiteral("/large.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/large.js")), 5000, 1024));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY(!result.ok);
    QVERIFY(result.tooLarge);
}

void ScriptDownloaderTest::rejectsUnsafeUrls()
{
    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(QUrl(QStringLiteral("http://example.com/a.js"))));
    const ScriptDownloadResult result = waitFor(downloader, spy);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("HTTPS")));

    QString error;
    QVERIFY(!ScriptDownloader::isAllowedUrl(QUrl(QStringLiteral("ftp://example.com/a.js")),
                                            &error));
    QVERIFY(ScriptDownloader::isAllowedUrl(QUrl(QStringLiteral("http://127.0.0.1/a.js"))));
    QVERIFY(ScriptDownloader::isAllowedUrl(QUrl(QStringLiteral("https://example.com/a.js"))));
}

void ScriptDownloaderTest::rejectsConcurrentSameScript()
{
    TestHttpServer server;
    QVERIFY(server.start());
    TestHttpServer::Route route;
    route.hang = true;
    server.setRoute(QStringLiteral("/slow.js"), route);

    ScriptDownloader downloader;
    QSignalSpy spy(&downloader, &ScriptDownloader::finished);
    downloader.download(requestFor(server.url(QStringLiteral("/slow.js")), 2000));
    QVERIFY(downloader.isBusy(QStringLiteral("script")));
    downloader.download(requestFor(server.url(QStringLiteral("/slow.js")), 2000));
    QVERIFY(spy.count() >= 1);
    const ScriptDownloadResult rejected =
        qvariant_cast<ScriptDownloadResult>(spy.takeFirst().first());
    QVERIFY(!rejected.ok);
    QVERIFY(rejected.error.contains(QStringLiteral("正在更新")));
    downloader.abortAll();
}

QTEST_GUILESS_MAIN(ScriptDownloaderTest)

#include "test_script_downloader.moc"
