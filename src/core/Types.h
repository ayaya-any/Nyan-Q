#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cq {

enum class CoreState { Stopped, Starting, Running, Stopping, Failed };
enum class LogLevel { Debug, Info, Warning, Error };

struct AppSettings {
    int mixedPort = 7890;
    int controllerPort = 9090;
    QString secret;
    bool allowLan = false;
    QString mode = QStringLiteral("rule");
    QString logLevel = QStringLiteral("info");
    bool ipv6 = false;
    bool tunEnabled = false;
    bool autoLaunch = false;
    bool startMinimized = false;
    bool closeToTray = true;
    QString theme = QStringLiteral("dark");
    QString corePath;
    QString delayTestUrl = QStringLiteral("http://www.gstatic.com/generate_204");
    int subscriptionIntervalHours = 0;
};

struct ProfileMeta {
    QString id;
    QString name;
    QString type;   // remote, local, builtin
    QString url;
    QString fileName;
    QDateTime updatedAt;
    qint64 upload = 0;
    qint64 download = 0;
    qint64 total = 0;
    qint64 expireAt = 0;
    int updateIntervalHours = 0;
    bool active = false;

    bool remote() const { return type == QStringLiteral("remote"); }
};

struct ProxyNode {
    QString name;
    QString type;
    int delay = -1;
    bool udp = false;
    bool alive = true;
};

struct ProxyGroup {
    QString name;
    QString type;
    QString now;
    QVector<ProxyNode> nodes;

    ProxyNode* findNode(const QString& nodeName)
    {
        for (auto& node : nodes)
            if (node.name == nodeName)
                return &node;
        return nullptr;
    }
};

struct ConnectionInfo {
    QString id;
    QString host;
    QString network;
    QString type;
    QString rule;
    QString chain;
    QString process;
    qint64 upload = 0;
    qint64 download = 0;
    QDateTime startedAt;
};

struct RuleInfo {
    QString type;
    QString payload;
    QString proxy;
};

struct TrafficSample {
    qint64 upload = 0;
    qint64 download = 0;
};

struct SessionStats {
    qint64 uploadSpeed = 0;
    qint64 downloadSpeed = 0;
    qint64 uploadTotal = 0;
    qint64 downloadTotal = 0;
    int activeConnections = 0;
    qint64 connectedSeconds = 0;
};

struct LogEntry {
    QDateTime time;
    LogLevel level = LogLevel::Info;
    QString message;
};

struct ExitInfo {
    QString ip;
    QString countryCode;
    QString countryName;
    QString region;
    QString city;
    QString organization;
    QDateTime updatedAt;
    QString error;
    bool valid = false;
    bool loading = false;
};

struct SiteLatency {
    QString id;
    QString name;
    QString url;
    int delayMs = -1;
    bool ok = false;
    bool pending = false;
    QDateTime testedAt;
};

struct MemoryUsage {
    qint64 appWorkingSet = 0;
    qint64 kernelWorkingSet = 0;
    bool kernelValid = false;
    bool appValid = false;
};

QString coreStateLabel(CoreState state);
QString modeDisplayName(const QString& mode);
QString proxyTypeDisplay(const QString& type);
QString profileTypeLabel(const QString& type);

}  // namespace cq

Q_DECLARE_METATYPE(cq::CoreState)
Q_DECLARE_METATYPE(cq::LogLevel)
Q_DECLARE_METATYPE(cq::ProfileMeta)
Q_DECLARE_METATYPE(cq::ProxyNode)
Q_DECLARE_METATYPE(cq::ProxyGroup)
Q_DECLARE_METATYPE(cq::ConnectionInfo)
Q_DECLARE_METATYPE(cq::RuleInfo)
Q_DECLARE_METATYPE(cq::TrafficSample)
Q_DECLARE_METATYPE(cq::SessionStats)
Q_DECLARE_METATYPE(cq::LogEntry)
Q_DECLARE_METATYPE(cq::ExitInfo)
Q_DECLARE_METATYPE(cq::SiteLatency)
Q_DECLARE_METATYPE(cq::MemoryUsage)
