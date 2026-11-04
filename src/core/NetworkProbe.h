#pragma once

#include "core/Types.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVector>

class QNetworkReply;

class NetworkProbe final : public QObject {
    Q_OBJECT

public:
    explicit NetworkProbe(QObject* parent = nullptr);

    void setProxyPort(int port);
    int proxyPort() const { return m_proxyPort; }
    void setRunning(bool running);
    bool isRunning() const { return m_running; }

    const cq::ExitInfo& exitInfo() const { return m_exitInfo; }
    const QVector<cq::SiteLatency>& siteLatencies() const { return m_sites; }

    void refreshAll();
    void refreshExitInfo();
    void refreshSiteLatencies();
    void loadDemoData();

signals:
    void exitInfoChanged();
    void siteLatenciesChanged();

private:
    struct SiteDefinition {
        QString id;
        QString name;
        QUrl url;
    };

    static QVector<SiteDefinition> siteDefinitions();
    void applyProxy();
    void cancelExitRequest();
    void cancelSiteRequests();
    void fetchExitPrimary(int generation);
    void fetchExitFallback(int generation);
    void completeExit(const cq::ExitInfo& info);
    void failExit(const QString& error);
    void markSitesPending();
    void requestSite(const SiteDefinition& site, int generation);
    static QString localizedCountryName(const QString& code, const QString& fallback);

    QNetworkAccessManager m_network;
    QTimer m_siteTimer;
    QTimer m_exitTimer;
    cq::ExitInfo m_exitInfo;
    QVector<cq::SiteLatency> m_sites;
    QSet<QNetworkReply*> m_siteReplies;
    QNetworkReply* m_exitReply = nullptr;
    int m_proxyPort = 0;
    int m_exitGeneration = 0;
    int m_siteGeneration = 0;
    bool m_running = false;
    bool m_demo = false;
};
