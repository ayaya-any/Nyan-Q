#include "core/ClashController.h"

#include "app/AppPaths.h"
#include "core/Formatters.h"
#include "core/RuntimeConfig.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <tuple>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
constexpr int kStartupTimeoutMs = 35000;
constexpr int kMaxLogEntries = 2000;

int appLogThreshold(const QString& level)
{
    if (level == QStringLiteral("debug"))
        return static_cast<int>(cq::LogLevel::Debug);
    if (level == QStringLiteral("warning"))
        return static_cast<int>(cq::LogLevel::Warning);
    if (level == QStringLiteral("error"))
        return static_cast<int>(cq::LogLevel::Error);
    return static_cast<int>(cq::LogLevel::Info);
}

QString demoNodeType(const QString& name)
{
    if (name.contains(QStringLiteral("东京")) || name.contains(QStringLiteral("大阪")))
        return QStringLiteral("Hysteria2");
    if (name.contains(QStringLiteral("新加坡")))
        return QStringLiteral("VLESS");
    if (name.contains(QStringLiteral("洛杉矶")))
        return QStringLiteral("Trojan");
    if (name.contains(QStringLiteral("香港")))
        return QStringLiteral("Shadowsocks");
    if (name.contains(QStringLiteral("DIRECT")))
        return QStringLiteral("Direct");
    if (name.contains(QStringLiteral("REJECT")))
        return QStringLiteral("Reject");
    return QStringLiteral("VMess");
}

int demoDelayFor(const QString& name)
{
    const uint hash = static_cast<uint>(qHash(name));
    if (name.contains(QStringLiteral("DIRECT")))
        return -1;
    if (name.contains(QStringLiteral("REJECT")))
        return -1;
    if (name.contains(QStringLiteral("东京")))
        return 42 + static_cast<int>(hash % 48);
    if (name.contains(QStringLiteral("香港")))
        return 28 + static_cast<int>(hash % 36);
    if (name.contains(QStringLiteral("新加坡")))
        return 78 + static_cast<int>(hash % 50);
    if (name.contains(QStringLiteral("台北")))
        return 55 + static_cast<int>(hash % 35);
    if (name.contains(QStringLiteral("洛杉矶")))
        return 155 + static_cast<int>(hash % 70);
    return 90 + static_cast<int>(hash % 220);
}
}  // namespace

ClashController::ClashController(QObject* parent)
    : QObject(parent)
    , m_settingsStore(AppPaths::settingsFile())
{
    m_probeTimer.setInterval(800);
    m_startupTimer.setSingleShot(true);
    m_startupTimer.setInterval(kStartupTimeoutMs);
    m_statsTimer.setInterval(1000);
    m_subscriptionTimer.setInterval(30 * 60 * 1000);
    m_demoTimer.setInterval(1100);
    m_configReloadTimer.setSingleShot(true);
    m_configReloadTimer.setInterval(160);
    m_memoryTimer.setInterval(2500);
    m_lastApiErrorAt.start();

    connect(&m_probeTimer, &QTimer::timeout, this, [this] {
        if (m_state == cq::CoreState::Starting)
            m_api.probe();
    });
    connect(&m_startupTimer, &QTimer::timeout, this, &ClashController::handleStartupTimeout);
    connect(&m_statsTimer, &QTimer::timeout, this, &ClashController::updateStatsTick);
    connect(&m_subscriptionTimer, &QTimer::timeout, this,
            &ClashController::maybeAutoUpdateSubscription);
    connect(&m_demoTimer, &QTimer::timeout, this, &ClashController::demoTick);
    connect(&m_configReloadTimer, &QTimer::timeout, this,
            &ClashController::performConfigReload);
    connect(&m_memoryTimer, &QTimer::timeout, this, &ClashController::updateMemoryUsage);

    wire();
}

ClashController::~ClashController()
{
    shutdown();
}

void ClashController::wire()
{
    connect(&m_api, &MihomoApi::probeSucceeded, this, [this](const QString& version, bool meta) {
        Q_UNUSED(meta)
        if (m_state == cq::CoreState::Starting)
            handleCoreReady(version);
    });
    connect(&m_api, &MihomoApi::probeFailed, this, [this](const QString& message) {
        if (m_state == cq::CoreState::Starting)
            m_lastProbeError = message;
    });
    connect(&m_api, &MihomoApi::configReloaded, this,
            &ClashController::handleConfigReloadFinished);
    connect(this, &ClashController::stateChanged, this, [this](cq::CoreState state) {
        if (m_demo || m_ephemeral)
            return;
        if (state == cq::CoreState::Running) {
            m_networkProbe.setProxyPort(m_settings.mixedPort);
            m_networkProbe.setRunning(true);
        } else {
            m_networkProbe.setRunning(false);
        }
        updateMemoryUsage();
    });
    connect(&m_api, &MihomoApi::configReceived, this, [this](const QJsonObject& config) {
        const QString mode = config.value(QStringLiteral("mode")).toString().toLower();
        if (!mode.isEmpty() && mode != m_mode) {
            m_mode = mode;
            m_settings.mode = mode;
            m_settingsStore.save(m_settings);
            emit modeChanged(mode);
            emit settingsChanged();
        }
    });
    connect(&m_api, &MihomoApi::proxiesReceived, this,
            [this](const QVector<cq::ProxyGroup>& groups) {
                m_proxyGroups = groups;
                emit proxyGroupsChanged(m_proxyGroups);
            });
    connect(&m_api, &MihomoApi::connectionsReceived, this,
            [this](const QVector<cq::ConnectionInfo>& connections, qint64 uploadTotal,
                   qint64 downloadTotal) {
                m_connections = connections;
                m_stats.uploadTotal = uploadTotal;
                m_stats.downloadTotal = downloadTotal;
                m_stats.activeConnections = connections.size();
                emit connectionsChanged(m_connections);
            });
    connect(&m_api, &MihomoApi::rulesReceived, this,
            [this](const QVector<cq::RuleInfo>& rules) {
                m_rules = rules;
                emit rulesChanged(m_rules);
            });
    connect(&m_api, &MihomoApi::trafficReceived, this,
            [this](qint64 upSpeed, qint64 downSpeed) {
                if (m_state != cq::CoreState::Running)
                    return;
                m_stats.uploadSpeed = qMax<qint64>(0, upSpeed);
                m_stats.downloadSpeed = qMax<qint64>(0, downSpeed);
                m_lastTrafficAt = QDateTime::currentDateTime();
            });
    connect(&m_api, &MihomoApi::delayTested, this,
            [this](const QString& name, int delayMs) {
                for (cq::ProxyGroup& group : m_proxyGroups) {
                    if (cq::ProxyNode* node = group.findNode(name))
                        node->delay = delayMs;
                }
                emit delayUpdated(name, delayMs);
            });
    connect(&m_api, &MihomoApi::actionSucceeded, this, [this](const QString& description) {
        if (!description.isEmpty())
            log(cq::LogLevel::Debug, description);
    });
    connect(&m_api, &MihomoApi::actionFailed, this,
            [this](const QString& description, const QString& message) {
                const QString text = QStringLiteral("%1失败：%2").arg(description, message);
                log(cq::LogLevel::Warning, text);
                emit notification(text, true);
            });
    connect(&m_api, &MihomoApi::requestFailed, this,
            [this](const QString& context, const QString& message) {
                const QString key = context + QLatin1Char('|') + message;
                if (key == m_lastApiError && m_lastApiErrorAt.elapsed() < 15000)
                    return;
                m_lastApiError = key;
                m_lastApiErrorAt.restart();
                log(cq::LogLevel::Warning,
                    QStringLiteral("%1失败：%2").arg(context, message));
            });

    connect(&m_process, &MihomoProcess::logLine, this, &ClashController::logCoreLine);
    connect(&m_process, &MihomoProcess::started, this, [this] {
        log(cq::LogLevel::Info, QStringLiteral("Mihomo 核心进程已启动，等待外部控制器就绪…"));
        if (m_state == cq::CoreState::Starting)
            m_probeTimer.start();
    });
    connect(&m_process, &MihomoProcess::failed, this, &ClashController::handleProcessFailure);
    connect(&m_process, &MihomoProcess::finished, this,
            [this](int exitCode, bool crashed) {
                handleProcessFinished(exitCode, crashed);
            });

    connect(&m_profiles, &ProfileStore::operationSucceeded, this, [this](const QString& message) {
        log(cq::LogLevel::Info, message);
        emit notification(message, false);
    });
    connect(&m_profiles, &ProfileStore::operationFailed, this, [this](const QString& message) {
        log(cq::LogLevel::Error, message);
        emit notification(message, true);
    });
    connect(&m_profiles, &ProfileStore::activeChanged, this, [this](const QString& id) {
        m_scripts.setActiveProfileId(id);
        emit activeProfileChanged();
        scheduleConfigReload();
    });
    connect(&m_profiles, &ProfileStore::profileUpdated, this, [this](const QString& id) {
        if (id != m_profiles.activeId())
            return;
        scheduleConfigReload();
    });
    connect(&m_profiles, &ProfileStore::profileUpdated, this, [this](const QString& id) {
        m_scripts.checkUpdatesForProfile(id, false);
    });
    connect(&m_profiles, &ProfileStore::changed, this, [this] { pruneScriptBindings(); });
    connect(&m_composer, &nq::scripts::ConfigComposer::finished, this,
            &ClashController::handleComposerFinished);
    connect(&m_composer, &nq::scripts::ConfigComposer::started, this,
            [this](const QString&) {
                log(cq::LogLevel::Debug, QStringLiteral("正在生成 JavaScript 覆写配置…"));
            });
}

void ClashController::initializeScripts()
{
    QString error;
    if (!m_scripts.initialize(AppPaths::scriptsDir(), &error)) {
        log(cq::LogLevel::Warning,
            QStringLiteral("JavaScript 脚本系统初始化失败：%1").arg(error));
        return;
    }
    wireScripts();
    pruneScriptBindings();
    m_scripts.setActiveProfileId(m_profiles.activeId());
    if (m_demo && m_scripts.scripts().isEmpty()) {
        m_scripts.createLocalScript(
            QStringLiteral("示例 · 规则整理"),
            QStringLiteral("演示用脚本，展示 console 日志和规则覆写"),
            QStringLiteral(
                "// 演示：为订阅补充一条本地直连规则，并输出执行日志\n"
                "function main(config, profileName) {\n"
                "    console.log(`正在处理订阅：${profileName}`);\n"
                "    const rules = Array.isArray(config.rules) ? config.rules : [];\n"
                "    const localRule = \"DOMAIN-SUFFIX,demo.nyanq,DIRECT\";\n"
                "    if (!rules.includes(localRule))\n"
                "        rules.unshift(localRule);\n"
                "    config.rules = rules;\n"
                "    return config;\n"
                "}\n"),
            nullptr);
    }
    if (!m_scripts.scripts().isEmpty()) {
        log(cq::LogLevel::Info,
            QStringLiteral("已加载 %1 个 JavaScript 覆写脚本").arg(m_scripts.scripts().size()));
    }
}

void ClashController::wireScripts()
{
    connect(&m_scripts, &nq::scripts::ScriptManager::scriptUpdated, this, [this](const QString&) {
        if (m_demo || m_shuttingDown)
            return;
        if (m_state == cq::CoreState::Running)
            scheduleConfigReload();
        else if (m_state == cq::CoreState::Starting)
            m_configReloadWhenReady = true;
    });
    connect(&m_scripts, &nq::scripts::ScriptManager::operationSucceeded, this,
            [this](const QString& message) {
                log(cq::LogLevel::Info, QStringLiteral("[脚本] %1").arg(message));
            });
    connect(&m_scripts, &nq::scripts::ScriptManager::operationFailed, this,
            [this](const QString& message) {
                log(cq::LogLevel::Error, QStringLiteral("[脚本] %1").arg(message));
                if (!m_demo)
                    emit notification(message, true);
            });
    connect(&m_scripts, &nq::scripts::ScriptManager::updateFinished, this,
            [this](const nq::scripts::ScriptUpdateResult& result) {
                if (!result.ok) {
                    log(cq::LogLevel::Warning,
                        QStringLiteral("[脚本] 「%1」更新失败：%2")
                            .arg(result.scriptName, result.error));
                } else if (!result.notModified && !result.skipped) {
                    log(cq::LogLevel::Info,
                        QStringLiteral("[脚本] 「%1」已更新至 %2")
                            .arg(result.scriptName, result.newSha256.left(12)));
                }
            });
    connect(&m_scripts, &nq::scripts::ScriptManager::logAdded, this,
            [this](const nq::scripts::ScriptLogEntry& entry) {
                if (entry.level == nq::scripts::ScriptLogLevel::Error) {
                    log(cq::LogLevel::Error,
                        QStringLiteral("[脚本:%1] %2").arg(entry.scriptName, entry.message));
                } else if (entry.level == nq::scripts::ScriptLogLevel::Warning) {
                    log(cq::LogLevel::Warning,
                        QStringLiteral("[脚本:%1] %2").arg(entry.scriptName, entry.message));
                }
            });
}

void ClashController::pruneScriptBindings()
{
    QStringList ids;
    for (const cq::ProfileMeta& profile : m_profiles.profiles())
        ids.append(profile.id);
    QString error;
    m_scripts.retainProfiles(ids, &error);
    if (!error.isEmpty())
        log(cq::LogLevel::Warning, QStringLiteral("清理脚本关联失败：%1").arg(error));
}

QVector<nq::scripts::ScriptStep> ClashController::activeScriptSteps() const
{
    const auto active = m_profiles.activeProfile();
    if (!active)
        return {};
    return m_scripts.resolveSteps(active->id);
}

void ClashController::initialize(bool demo, bool ephemeral)
{
    if (m_initialized)
        return;
    m_ephemeral = ephemeral;
    AppPaths::ensureDirectories();

    QFile logFile(AppPaths::logFile());
    if (logFile.size() > 2 * 1024 * 1024) {
        QFile::remove(AppPaths::logFile() + QStringLiteral(".1"));
        QFile::rename(AppPaths::logFile(), AppPaths::logFile() + QStringLiteral(".1"));
    }

    m_settings = m_settingsStore.load();
    if (m_settings.secret.trimmed().isEmpty())
        m_settings.secret = SettingsStore::createSecret();
    m_mode = m_settings.mode;
    m_settingsStore.save(m_settings);

    m_demo = demo;
    m_profiles.initialize(AppPaths::profilesDir());
    if (m_demo)
        m_profiles.seedDemoProfiles();
    initializeScripts();

    m_api.setEndpoint(m_settings.controllerPort);
    m_api.setSecret(m_settings.secret);
    m_api.setDelayTestUrl(m_settings.delayTestUrl);

    if (!m_demo && !m_ephemeral) {
        QString launchError;
        if (!SettingsStore::applyLaunchAtLogin(m_settings.autoLaunch, &launchError))
            log(cq::LogLevel::Warning, launchError);
        m_subscriptionTimer.start();
    }

    connect(qApp, &QCoreApplication::aboutToQuit, this, &ClashController::shutdown);

    m_memoryTimer.start();
    if (m_demo) {
        enterDemo();
    } else {
        updateMemoryUsage();
        log(cq::LogLevel::Info,
            QStringLiteral("Nyan-Q %1 已启动 · 数据目录 %2")
                .arg(AppPaths::appVersion(), QDir::toNativeSeparators(AppPaths::dataDir())));
    }
    m_initialized = true;
}

void ClashController::enterDemo()
{
    m_demo = true;
    m_mode = QStringLiteral("rule");
    m_coreVersion = QStringLiteral("v1.19.33");
    m_connectedAt = QDateTime::currentDateTime().addSecs(-(2 * 3600 + 37 * 60 + 12));
    m_stats = {};
    populateDemoData();
    m_networkProbe.loadDemoData();
    m_memoryUsage = {};
    m_memoryUsage.appWorkingSet = 84LL * 1024 * 1024;
    m_memoryUsage.kernelWorkingSet = 46LL * 1024 * 1024;
    m_memoryUsage.appValid = true;
    m_memoryUsage.kernelValid = true;
    m_state = cq::CoreState::Running;
    m_demoTick = 0;
    m_demoTimer.start();

    m_logHistory.clear();
    const QList<QPair<cq::LogLevel, QString>> seed = {
        {cq::LogLevel::Info, QStringLiteral("Mihomo Meta v1.19.33 windows/amd64 已启动")},
        {cq::LogLevel::Debug, QStringLiteral("RESTful API listening at 127.0.0.1:9090")},
        {cq::LogLevel::Info, QStringLiteral("mixed listener started at 127.0.0.1:7890")},
        {cq::LogLevel::Info, QStringLiteral("DNS server listening at 127.0.0.1:1053")},
        {cq::LogLevel::Debug, QStringLiteral("loaded 248 rules from 14 rule providers")},
        {cq::LogLevel::Info, QStringLiteral("已应用规则模式，流量将根据分流规则路由")},
        {cq::LogLevel::Info, QStringLiteral("订阅「Aurora · 流媒体」将在 24 小时后自动更新")},
    };
    for (const auto& [level, message] : seed)
        log(level, message);
    emit stateChanged(m_state);
}

void ClashController::populateDemoData()
{
    m_proxyGroups.clear();

    auto makeNode = [](const QString& name) {
        cq::ProxyNode node;
        node.name = name;
        node.type = demoNodeType(name);
        node.delay = demoDelayFor(name);
        node.udp = true;
        node.alive = true;
        return node;
    };

    const QStringList primary = {
        QStringLiteral("DIRECT"),         QStringLiteral("香港 · 01"),
        QStringLiteral("香港 · 02"),       QStringLiteral("东京 · Hikari"),
        QStringLiteral("东京 · Sakura"),   QStringLiteral("新加坡 · Merlion"),
        QStringLiteral("台北 · 101"),       QStringLiteral("洛杉矶 · Apex"),
    };

    auto addGroup = [&](const QString& name, const QString& type, const QString& now,
                        const QStringList& nodes, const QVector<int>& delays = {}) {
        cq::ProxyGroup group;
        group.name = name;
        group.type = type;
        group.now = now;
        int index = 0;
        for (const QString& nodeName : nodes) {
            cq::ProxyNode node = makeNode(nodeName);
            if (index < delays.size())
                node.delay = delays.at(index);
            group.nodes.append(node);
            ++index;
        }
        m_proxyGroups.append(group);
    };

    addGroup(QStringLiteral("节点选择"), QStringLiteral("Selector"),
             QStringLiteral("东京 · Hikari"), primary);
    addGroup(QStringLiteral("自动选择"), QStringLiteral("URLTest"),
             QStringLiteral("东京 · Sakura"),
             {QStringLiteral("香港 · 01"), QStringLiteral("香港 · 02"),
              QStringLiteral("东京 · Hikari"), QStringLiteral("东京 · Sakura"),
              QStringLiteral("新加坡 · Merlion")});
    addGroup(QStringLiteral("流媒体"), QStringLiteral("Selector"),
             QStringLiteral("香港 · 02"),
             {QStringLiteral("DIRECT"), QStringLiteral("香港 · 01"), QStringLiteral("香港 · 02"),
              QStringLiteral("东京 · Sakura"), QStringLiteral("洛杉矶 · Apex")});
    addGroup(QStringLiteral("AI 服务"), QStringLiteral("Selector"),
             QStringLiteral("洛杉矶 · Apex"),
             {QStringLiteral("DIRECT"), QStringLiteral("东京 · Hikari"),
              QStringLiteral("洛杉矶 · Apex"), QStringLiteral("REJECT")});
    addGroup(QStringLiteral("游戏专线"), QStringLiteral("Fallback"),
             QStringLiteral("东京 · Hikari"),
             {QStringLiteral("东京 · Hikari"), QStringLiteral("大阪 · Namba"),
              QStringLiteral("香港 · 01")});

    m_connections.clear();
    const QList<QStringList> seeds = {
        {QStringLiteral("api.github.com:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("节点选择 → 东京 · Hikari")},
        {QStringLiteral("www.youtube.com:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("RULE-SET"), QStringLiteral("流媒体 → 香港 · 02")},
        {QStringLiteral("api.openai.com:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("AI 服务 → 洛杉矶 · Apex")},
        {QStringLiteral("cdn.jsdelivr.net:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("GEOIP"), QStringLiteral("节点选择 → 东京 · Sakura")},
        {QStringLiteral("store.steampowered.com:443"), QStringLiteral("tcp"),
         QStringLiteral("HTTPS"), QStringLiteral("RULE-SET"),
         QStringLiteral("游戏专线 → 东京 · Hikari")},
        {QStringLiteral("1.1.1.1:53"), QStringLiteral("udp"), QStringLiteral("DNS"),
         QStringLiteral("MATCH"), QStringLiteral("节点选择 → 东京 · Hikari")},
        {QStringLiteral("music.163.com:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("GEOIP"), QStringLiteral("DIRECT")},
        {QStringLiteral("discord.com:443"), QStringLiteral("tcp"), QStringLiteral("HTTPS"),
         QStringLiteral("DOMAIN"), QStringLiteral("节点选择 → 新加坡 · Merlion")},
    };
    int index = 0;
    for (const QStringList& seed : seeds) {
        cq::ConnectionInfo info;
        info.id = QStringLiteral("demo-%1").arg(index++);
        info.host = seed.value(0);
        info.network = seed.value(1);
        info.type = seed.value(2);
        info.rule = seed.value(3);
        info.chain = seed.value(4);
        info.upload = 48000 + static_cast<qint64>(QRandomGenerator::global()->bounded(900000));
        info.download = 120000 + static_cast<qint64>(QRandomGenerator::global()->bounded(9000000));
        info.startedAt = QDateTime::currentDateTime().addSecs(
            -static_cast<int>(QRandomGenerator::global()->bounded(40, 900)));
        m_connections.append(info);
    }

    m_rules.clear();
    struct RuleSeed {
        QString type;
        QString payload;
        QString proxy;
    };
    const QList<RuleSeed> ruleSeeds = {
        {QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("github.com"), QStringLiteral("节点选择")},
        {QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("githubusercontent.com"),
         QStringLiteral("节点选择")},
        {QStringLiteral("DOMAIN-KEYWORD"), QStringLiteral("youtube"), QStringLiteral("流媒体")},
        {QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("netflix.com"), QStringLiteral("流媒体")},
        {QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("openai.com"), QStringLiteral("AI 服务")},
        {QStringLiteral("DOMAIN-SUFFIX"), QStringLiteral("anthropic.com"),
         QStringLiteral("AI 服务")},
        {QStringLiteral("RULE-SET"), QStringLiteral("private"), QStringLiteral("DIRECT")},
        {QStringLiteral("RULE-SET"), QStringLiteral("cn-domain"), QStringLiteral("DIRECT")},
        {QStringLiteral("GEOIP"), QStringLiteral("CN"), QStringLiteral("DIRECT")},
        {QStringLiteral("RULE-SET"), QStringLiteral("streaming"), QStringLiteral("流媒体")},
        {QStringLiteral("PROCESS-NAME"), QStringLiteral("Discord.exe"),
         QStringLiteral("节点选择")},
        {QStringLiteral("PROCESS-NAME"), QStringLiteral("Steam.exe"), QStringLiteral("游戏专线")},
        {QStringLiteral("IP-CIDR"), QStringLiteral("192.168.0.0/16"), QStringLiteral("DIRECT")},
        {QStringLiteral("IP-CIDR"), QStringLiteral("10.0.0.0/8"), QStringLiteral("DIRECT")},
        {QStringLiteral("DOMAIN"), QStringLiteral("api.telegram.org"), QStringLiteral("节点选择")},
        {QStringLiteral("DOMAIN"), QStringLiteral("clash.razord.top"), QStringLiteral("REJECT")},
        {QStringLiteral("MATCH"), QStringLiteral("节点选择")},
    };
    for (const RuleSeed& seed : ruleSeeds) {
        cq::RuleInfo rule;
        rule.type = seed.type;
        rule.payload = seed.payload;
        rule.proxy = seed.proxy;
        m_rules.append(rule);
    }

    m_stats.uploadTotal = 417LL * 1024 * 1024;
    m_stats.downloadTotal = 3341LL * 1024 * 1024;
    m_stats.uploadSpeed = 126000;
    m_stats.downloadSpeed = 2380000;
    m_stats.activeConnections = m_connections.size();
    m_stats.connectedSeconds = m_connectedAt.isValid()
                                   ? m_connectedAt.secsTo(QDateTime::currentDateTime())
                                   : 0;
}

void ClashController::demoTick()
{
    if (m_state != cq::CoreState::Running)
        return;
    ++m_demoTick;
    const double wave = std::sin(m_demoTick / 4.2) * 0.35 + std::sin(m_demoTick / 1.7) * 0.12;
    m_stats.downloadSpeed =
        qMax<qint64>(220000, static_cast<qint64>(2350000.0 * (1.0 + wave)));
    m_stats.uploadSpeed =
        qMax<qint64>(18000, static_cast<qint64>(128000.0 * (1.0 - wave * 0.6)));
    m_stats.uploadTotal += m_stats.uploadSpeed;
    m_stats.downloadTotal += m_stats.downloadSpeed;
    m_stats.activeConnections = m_connections.size();
    m_stats.connectedSeconds = m_connectedAt.secsTo(QDateTime::currentDateTime());

    if (m_demoTick % 2 == 0) {
        for (cq::ConnectionInfo& connection : m_connections) {
            connection.upload += static_cast<qint64>(QRandomGenerator::global()->bounded(20000, 280000));
            connection.download +=
                static_cast<qint64>(QRandomGenerator::global()->bounded(90000, 2600000));
        }
        emit connectionsChanged(m_connections);
    }
    emit statsChanged(m_stats);
}

void ClashController::startDemo()
{
    if (m_demo && m_state == cq::CoreState::Running)
        return;
    m_state = cq::CoreState::Starting;
    m_lastError.clear();
    emit stateChanged(m_state);
    log(cq::LogLevel::Info, QStringLiteral("正在启动演示内核…"));
    QTimer::singleShot(650, this, [this] {
        if (!m_demo || m_state != cq::CoreState::Starting)
            return;
        populateDemoData();
        m_connectedAt = QDateTime::currentDateTime();
        m_state = cq::CoreState::Running;
        m_demoTimer.start();
        emit stateChanged(m_state);
        emit proxyGroupsChanged(m_proxyGroups);
        emit rulesChanged(m_rules);
        emit connectionsChanged(m_connections);
        emit statsChanged(m_stats);
        log(cq::LogLevel::Info, QStringLiteral("演示内核已就绪（不产生真实代理流量）"));
    });
}

void ClashController::stopDemo()
{
    m_demoTimer.stop();
    m_probeTimer.stop();
    m_pendingSystemProxy = false;
    if (m_systemProxyEnabled) {
        m_systemProxyEnabled = false;
        emit systemProxyChanged(false);
    }
    m_state = cq::CoreState::Stopped;
    m_stats.uploadSpeed = 0;
    m_stats.downloadSpeed = 0;
    emit stateChanged(m_state);
    emit statsChanged(m_stats);
    log(cq::LogLevel::Info, QStringLiteral("演示内核已断开"));
}

void ClashController::start()
{
    if (m_shuttingDown)
        return;
    if (m_demo) {
        startDemo();
        return;
    }
    if (m_state == cq::CoreState::Starting || m_state == cq::CoreState::Running) {
        emit notification(QStringLiteral("内核已经在运行"), false);
        return;
    }
    if (m_state == cq::CoreState::Stopping || m_process.isRunning()) {
        m_restartPending = true;
        return;
    }

    m_configReloadTimer.stop();
    m_configReloadInFlight = false;
    m_configReloadQueued = false;
    m_configReloadWhenReady = false;

    QString settingsError;
    if (!validateSettings(m_settings, &settingsError)) {
        failStartup(settingsError);
        return;
    }

    const auto active = m_profiles.activeProfile();
    if (!active) {
        failStartup(QStringLiteral("没有可用的配置，请先导入订阅"));
        return;
    }
    const QString sourcePath = m_profiles.configPath(*active);
    if (!QFileInfo::exists(sourcePath)) {
        failStartup(QStringLiteral("当前配置文件不存在：%1").arg(sourcePath));
        return;
    }

    m_coreBinary = AppPaths::resolveCorePath(m_settings.corePath);
    if (m_coreBinary.isEmpty()) {
        failStartup(QStringLiteral("未找到 Mihomo 核心。请把 mihomo.exe 放到程序目录，"
                                   "或在设置中指定可执行文件路径。"));
        return;
    }

    m_activeConfigPath = AppPaths::runtimeConfigFile();

    const QVector<nq::scripts::ScriptStep> steps = activeScriptSteps();
    if (!steps.isEmpty()) {
        startWithScripts(*active, sourcePath);
        return;
    }

    QString configError;
    if (!RuntimeConfig::write(sourcePath, m_activeConfigPath, m_settings, &configError)) {
        failStartup(configError);
        return;
    }

    launchCore(*active, m_activeConfigPath);
}

bool ClashController::startWithScripts(const cq::ProfileMeta& profile, const QString& sourcePath)
{
    if (m_composer.isBusy()) {
        failStartup(QStringLiteral("JavaScript 脚本配置正在生成，请稍后重试"));
        return false;
    }

    QString readError;
    const QByteArray raw = m_profiles.configContent(profile.id, &readError);
    if (!readError.isEmpty() || raw.isEmpty()) {
        failStartup(readError.isEmpty() ? QStringLiteral("当前订阅配置为空") : readError);
        return false;
    }

    const QVector<nq::scripts::ScriptStep> steps = m_scripts.resolveSteps(profile.id);
    m_scriptStartPending = true;
    m_scriptStartProfileId = profile.id;
    m_scriptStartProfileName = profile.name;
    m_startRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    nq::scripts::ConfigComposer::Request request;
    request.requestId = m_startRequestId;
    request.profileId = profile.id;
    request.profileName = profile.name;
    request.rawConfig = raw;
    request.settings = m_settings;
    request.steps = steps;
    request.scriptSettings = m_scripts.settings();
    request.corePath = m_coreBinary;
    request.dataDirectory = AppPaths::dataDir();
    request.candidatePath =
        AppPaths::runtimeDir() + QStringLiteral("/config.candidate.yaml");
    request.outputPath = AppPaths::runtimeConfigFile();
    request.validationTimeoutMs = 20000;
    request.validate = true;
    request.stopOnError = m_scripts.settings().stopOnError;

    m_state = cq::CoreState::Starting;
    emit stateChanged(m_state);
    log(cq::LogLevel::Info,
        QStringLiteral("正在执行 %1 个 JavaScript 脚本并校验最终配置…").arg(steps.size()));

    if (!m_composer.build(request)) {
        m_scriptStartPending = false;
        m_startRequestId.clear();
        failStartup(QStringLiteral("无法启动脚本配置生成"));
        return false;
    }
    return true;
}

void ClashController::launchCore(const cq::ProfileMeta& profile, const QString& runtimePath)
{
    m_activeConfigPath = runtimePath;
    m_api.setEndpoint(m_settings.controllerPort);
    m_api.setSecret(m_settings.secret);
    m_api.setDelayTestUrl(m_settings.delayTestUrl);
    m_api.stopPolling();
    m_process.setProgram(m_coreBinary);
    m_process.setDataDirectory(AppPaths::dataDir());

    m_lastError.clear();
    m_lastProbeError.clear();
    m_connectedAt = QDateTime();
    m_lastTrafficAt = QDateTime();
    m_stats = {};
    m_connections.clear();
    m_rules.clear();
    m_proxyGroups.clear();
    emit connectionsChanged(m_connections);
    emit rulesChanged(m_rules);
    emit proxyGroupsChanged(m_proxyGroups);
    emit statsChanged(m_stats);

    m_state = cq::CoreState::Starting;
    emit stateChanged(m_state);
    log(cq::LogLevel::Info,
        QStringLiteral("正在启动 Mihomo · 配置「%1」 · 混合端口 %2")
            .arg(profile.name)
            .arg(m_settings.mixedPort));
    if (m_settings.tunEnabled && !isElevated()) {
        log(cq::LogLevel::Warning,
            QStringLiteral("TUN 模式通常需要管理员权限，如果启动失败请以管理员身份运行"));
    }

    m_startupTimer.start();
    m_process.start(m_activeConfigPath, m_settings.controllerPort, m_settings.secret);
}

void ClashController::stop()
{
    if (m_demo) {
        stopDemo();
        return;
    }
    m_restartPending = false;
    requestStop(false);
}

void ClashController::restart()
{
    if (m_demo) {
        stopDemo();
        QTimer::singleShot(220, this, &ClashController::startDemo);
        return;
    }
    if (m_state == cq::CoreState::Starting || m_state == cq::CoreState::Running ||
        m_state == cq::CoreState::Stopping || m_process.isRunning()) {
        m_restoreSystemProxyAfterRestart = m_systemProxyEnabled;
        m_restartPending = true;
        requestStop(true);
    } else {
        start();
    }
}

void ClashController::requestStop(bool restartAfter)
{
    m_restartPending = restartAfter;
    m_pendingSystemProxy = false;
    if (!restartAfter)
        m_restoreSystemProxyAfterRestart = false;
    m_configReloadTimer.stop();
    m_configReloadInFlight = false;
    m_configReloadQueued = false;
    m_configReloadWhenReady = false;
    m_scriptStartPending = false;
    m_startRequestId.clear();
    m_reloadRequestId.clear();
    m_composer.cancel();
    m_probeTimer.stop();
    m_startupTimer.stop();
    m_statsTimer.stop();
    m_api.stopPolling();
    syncSystemProxy(false);

    if (m_process.isRunning()) {
        if (m_state != cq::CoreState::Stopping) {
            m_state = cq::CoreState::Stopping;
            emit stateChanged(m_state);
        }
        log(cq::LogLevel::Info, QStringLiteral("正在停止 Mihomo 核心…"));
        m_process.stop();
        return;
    }

    clearTransientState();
    m_state = cq::CoreState::Stopped;
    emit stateChanged(m_state);
    if (m_restartPending) {
        m_restartPending = false;
        QTimer::singleShot(180, this, &ClashController::start);
    }
}

void ClashController::handleCoreReady(const QString& version)
{
    if (m_state != cq::CoreState::Starting)
        return;
    m_probeTimer.stop();
    m_startupTimer.stop();
    m_coreVersion = version;
    m_connectedAt = QDateTime::currentDateTime();
    m_lastTrafficAt = m_connectedAt;
    m_stats.connectedSeconds = 0;
    m_lastProbeError.clear();
    m_state = cq::CoreState::Running;
    emit stateChanged(m_state);
    emit coreVersionChanged(m_coreVersion);
    emit statsChanged(m_stats);
    m_api.startPolling();
    m_statsTimer.start();
    log(cq::LogLevel::Info, QStringLiteral("Mihomo %1 已就绪，外部控制器连接成功").arg(version));
    emit notification(QStringLiteral("Mihomo 核心已连接"), false);

    if (m_configReloadWhenReady) {
        m_configReloadWhenReady = false;
        m_configReloadTimer.start(0);
    }

    if (m_pendingSystemProxy) {
        m_pendingSystemProxy = false;
        setSystemProxy(true);
    } else if (m_restoreSystemProxyAfterRestart) {
        m_restoreSystemProxyAfterRestart = false;
        setSystemProxy(true);
    } else if (m_settings.systemProxyEnabled) {
        // Restore the user's system-proxy preference after an app restart.
        setSystemProxy(true);
    }
}

void ClashController::handleStartupTimeout()
{
    if (m_state != cq::CoreState::Starting)
        return;
    QString message = QStringLiteral("Mihomo 启动超时。请检查端口 %1 / %2 是否被占用。")
                          .arg(m_settings.mixedPort)
                          .arg(m_settings.controllerPort);
    if (!m_lastProbeError.isEmpty())
        message += QStringLiteral("\n控制器错误：%1").arg(m_lastProbeError);
    failStartup(message);
}

void ClashController::failStartup(const QString& message)
{
    m_lastError = message;
    m_scriptStartPending = false;
    m_startRequestId.clear();
    m_reloadRequestId.clear();
    if (m_composer.isBusy())
        m_composer.cancel();
    m_probeTimer.stop();
    m_startupTimer.stop();
    m_statsTimer.stop();
    m_configReloadTimer.stop();
    m_configReloadInFlight = false;
    m_configReloadQueued = false;
    m_configReloadWhenReady = false;
    m_api.stopPolling();
    m_pendingSystemProxy = false;
    m_restartPending = false;
    m_restoreSystemProxyAfterRestart = false;
    syncSystemProxy(false);

    const bool alreadyFailed = m_state == cq::CoreState::Failed;
    m_state = cq::CoreState::Failed;
    emit stateChanged(m_state);
    clearTransientState();
    if (m_process.isRunning())
        m_process.stop();

    if (!alreadyFailed) {
        log(cq::LogLevel::Error, message);
        emit notification(message, true);
    }
}

void ClashController::handleProcessFinished(int exitCode, bool crashed)
{
    m_probeTimer.stop();
    m_startupTimer.stop();
    m_statsTimer.stop();
    m_configReloadTimer.stop();
    m_configReloadInFlight = false;
    m_configReloadQueued = false;
    m_configReloadWhenReady = false;
    m_api.stopPolling();
    syncSystemProxy(false);

    const bool restart = m_restartPending;
    m_restartPending = false;
    clearTransientState();

    if (m_shuttingDown) {
        m_state = cq::CoreState::Stopped;
        emit stateChanged(m_state);
        return;
    }

    if (restart) {
        m_state = cq::CoreState::Stopped;
        emit stateChanged(m_state);
        log(cq::LogLevel::Info, QStringLiteral("核心已停止，正在应用新配置…"));
        QTimer::singleShot(220, this, &ClashController::start);
        return;
    }

    if (m_state == cq::CoreState::Stopping) {
        m_state = cq::CoreState::Stopped;
        emit stateChanged(m_state);
        log(cq::LogLevel::Info, QStringLiteral("Mihomo 核心已停止"));
        return;
    }

    if (m_state == cq::CoreState::Failed) {
        // The failure signal already reported the actionable error.
        emit stateChanged(m_state);
        return;
    }

    const QString message =
        crashed ? QStringLiteral("Mihomo 核心异常崩溃，请检查日志")
                : QStringLiteral("Mihomo 核心意外退出（代码 %1）").arg(exitCode);
    m_lastError = message;
    m_state = cq::CoreState::Failed;
    emit stateChanged(m_state);
    log(cq::LogLevel::Error, message);
    emit notification(message, true);
}

void ClashController::handleProcessFailure(const QString& message)
{
    if (m_shuttingDown || m_state == cq::CoreState::Stopping ||
        m_state == cq::CoreState::Stopped || m_state == cq::CoreState::Failed)
        return;
    failStartup(message);
}

void ClashController::updateStatsTick()
{
    if (m_state != cq::CoreState::Running)
        return;
    if (m_connectedAt.isValid())
        m_stats.connectedSeconds = m_connectedAt.secsTo(QDateTime::currentDateTime());
    m_stats.activeConnections = m_connections.size();
    if (!m_lastTrafficAt.isValid() || m_lastTrafficAt.secsTo(QDateTime::currentDateTime()) > 3) {
        m_stats.uploadSpeed = 0;
        m_stats.downloadSpeed = 0;
    }
    emit statsChanged(m_stats);
}

void ClashController::clearTransientState()
{
    m_stats.uploadSpeed = 0;
    m_stats.downloadSpeed = 0;
    m_stats.activeConnections = 0;
    if (!m_connections.isEmpty()) {
        m_connections.clear();
        emit connectionsChanged(m_connections);
    }
    if (!m_rules.isEmpty()) {
        m_rules.clear();
        emit rulesChanged(m_rules);
    }
    if (!m_proxyGroups.isEmpty()) {
        m_proxyGroups.clear();
        emit proxyGroupsChanged(m_proxyGroups);
    }
    emit statsChanged(m_stats);
}

void ClashController::setMode(const QString& mode)
{
    const QString normalized = mode.toLower();
    if (normalized != QStringLiteral("rule") && normalized != QStringLiteral("global") &&
        normalized != QStringLiteral("direct")) {
        return;
    }
    if (m_mode == normalized)
        return;
    m_mode = normalized;
    m_settings.mode = normalized;
    m_settingsStore.save(m_settings);
    emit modeChanged(m_mode);
    emit settingsChanged();
    log(cq::LogLevel::Info,
        QStringLiteral("已切换到%1模式").arg(cq::modeDisplayName(normalized)));
    if (m_state == cq::CoreState::Running && !m_demo) {
        m_api.setMode(normalized);
        QTimer::singleShot(600, &m_networkProbe, &NetworkProbe::refreshAll);
    }
}

void ClashController::setAllowLanEnabled(bool enabled)
{
    if (m_settings.allowLan == enabled)
        return;
    m_settings.allowLan = enabled;
    m_settingsStore.save(m_settings);
    emit settingsChanged();
    log(cq::LogLevel::Info,
        enabled ? QStringLiteral("已启用局域网代理") : QStringLiteral("已关闭局域网代理"));
    if (m_demo)
        return;
    if (m_state == cq::CoreState::Running || m_state == cq::CoreState::Starting) {
        emit notification(enabled ? QStringLiteral("正在启用局域网代理…")
                                  : QStringLiteral("正在关闭局域网代理…"),
                          false);
        scheduleConfigReload();
    }
}

void ClashController::setTunEnabled(bool enabled)
{
    if (m_settings.tunEnabled == enabled)
        return;
    if (enabled && !m_demo && !isElevated()) {
        // The switch was already toggled by the user, so resync it with the
        // stored setting while the UI asks for elevation in a queued slot.
        emit tunChanged(m_settings.tunEnabled);
        emit tunElevationRequired();
        log(cq::LogLevel::Warning, QStringLiteral("TUN 模式需要管理员权限，正在请求提权"));
        return;
    }
    m_settings.tunEnabled = enabled;
    m_settingsStore.save(m_settings);
    emit tunChanged(enabled);
    emit settingsChanged();
    log(cq::LogLevel::Info, enabled ? QStringLiteral("已启用 TUN 模式")
                                    : QStringLiteral("已关闭 TUN 模式"));
    if (m_demo)
        return;
    if (m_state == cq::CoreState::Running || m_state == cq::CoreState::Starting) {
        emit notification(enabled ? QStringLiteral("正在热应用 TUN 模式…")
                                  : QStringLiteral("正在关闭 TUN 模式…"),
                          false);
        scheduleConfigReload();
    }
}

bool ClashController::validateSettings(const cq::AppSettings& settings, QString* error) const
{
    if (!SettingsStore::isValidPort(settings.mixedPort) ||
        !SettingsStore::isValidPort(settings.controllerPort)) {
        if (error)
            *error = QStringLiteral("端口必须位于 1–65535 之间");
        return false;
    }
    if (settings.mixedPort == settings.controllerPort) {
        if (error)
            *error = QStringLiteral("混合端口和控制器端口不能相同");
        return false;
    }
    if (settings.secret.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("外部控制器密钥不能为空");
        return false;
    }
    if (settings.mode != QStringLiteral("rule") && settings.mode != QStringLiteral("global") &&
        settings.mode != QStringLiteral("direct")) {
        if (error)
            *error = QStringLiteral("代理模式无效");
        return false;
    }
    return true;
}

bool ClashController::updateSettings(const cq::AppSettings& settings, bool restartIfNeeded)
{
    cq::AppSettings next = settings;
    if (next.secret.trimmed().isEmpty())
        next.secret = SettingsStore::createSecret();
    next.secret = next.secret.trimmed();
    next.delayTestUrl = next.delayTestUrl.trimmed();
    if (next.delayTestUrl.isEmpty())
        next.delayTestUrl = QStringLiteral("http://www.gstatic.com/generate_204");

    QString error;
    if (!validateSettings(next, &error)) {
        emit notification(error, true);
        return false;
    }

    const cq::AppSettings previous = m_settings;
    bool elevationRequested = false;
    if (next.tunEnabled && !previous.tunEnabled && !m_demo && !isElevated()) {
        // Persist the rest of the form, but leave TUN off until the elevated
        // instance takes over; it re-enables TUN via --enable-tun.
        next.tunEnabled = false;
        elevationRequested = true;
    }
    const bool launchChanged = next.autoLaunch != previous.autoLaunch;
    if (launchChanged && !m_demo) {
        QString launchError;
        if (!SettingsStore::applyLaunchAtLogin(next.autoLaunch, &launchError)) {
            emit notification(launchError, true);
            return false;
        }
    }

    const bool modeDidChange = next.mode != previous.mode;
    const bool tunChangedValue = next.tunEnabled != previous.tunEnabled;
    const bool processSettingsChanged =
        next.controllerPort != previous.controllerPort || next.secret != previous.secret ||
        next.corePath != previous.corePath;
    const bool reloadSettingsChanged =
        next.mixedPort != previous.mixedPort || next.allowLan != previous.allowLan ||
        next.logLevel != previous.logLevel || next.ipv6 != previous.ipv6 ||
        next.tunEnabled != previous.tunEnabled;

    if (next.corePath != previous.corePath && !next.corePath.trimmed().isEmpty() &&
        !QFileInfo::exists(next.corePath)) {
        emit notification(QStringLiteral("指定的 Mihomo 路径不存在"), true);
        return false;
    }

    m_settings = next;
    m_mode = next.mode;
    m_settingsStore.save(m_settings);
    m_api.setEndpoint(m_settings.controllerPort);
    m_api.setSecret(m_settings.secret);
    m_api.setDelayTestUrl(m_settings.delayTestUrl);

    if (modeDidChange) {
        emit modeChanged(m_mode);
        if (m_state == cq::CoreState::Starting && !processSettingsChanged)
            m_configReloadWhenReady = true;
        if (m_state == cq::CoreState::Running && !m_demo && !processSettingsChanged &&
            !reloadSettingsChanged)
            m_api.setMode(m_mode);
    }
    if (tunChangedValue)
        emit tunChanged(m_settings.tunEnabled);
    emit settingsChanged();
    log(cq::LogLevel::Info, QStringLiteral("设置已保存"));

    if (!m_demo && (m_state == cq::CoreState::Running ||
                    m_state == cq::CoreState::Starting)) {
        if (processSettingsChanged) {
            if (restartIfNeeded) {
                restart();
            } else {
                emit notification(QStringLiteral("控制器或内核路径设置将在下次连接时生效"),
                                  false);
            }
        } else if (reloadSettingsChanged) {
            if (m_state == cq::CoreState::Running)
                log(cq::LogLevel::Info, QStringLiteral("正在热应用网络设置…"));
            scheduleConfigReload();
        }
    }
    if (elevationRequested) {
        emit tunElevationRequired();
        log(cq::LogLevel::Warning, QStringLiteral("TUN 模式需要管理员权限，正在请求提权"));
    }
    return true;
}

void ClashController::setSystemProxy(bool enabled)
{
    if (m_demo) {
        if (m_systemProxyEnabled == enabled)
            return;
        m_systemProxyEnabled = enabled;
        emit systemProxyChanged(enabled);
        log(cq::LogLevel::Info,
            enabled ? QStringLiteral("系统代理已开启（演示）")
                    : QStringLiteral("系统代理已关闭（演示）"));
        return;
    }

    if (!enabled) {
        m_pendingSystemProxy = false;
        if (m_systemProxy.appliedByUs()) {
            QString error;
            if (!m_systemProxy.disable(&error)) {
                emit notification(error, true);
                emit systemProxyChanged(m_systemProxyEnabled);
                return;
            }
        }
        m_systemProxyEnabled = false;
        persistSystemProxyPreference(false);
        emit systemProxyChanged(false);
        log(cq::LogLevel::Info, QStringLiteral("系统代理已关闭"));
        return;
    }

    if (m_state != cq::CoreState::Running) {
        if (m_state == cq::CoreState::Starting) {
            m_pendingSystemProxy = true;
            persistSystemProxyPreference(true);
            emit notification(QStringLiteral("内核就绪后将自动开启系统代理"), false);
            emit systemProxyChanged(false);
            return;
        }
        emit notification(QStringLiteral("Mihomo 内核未运行，请在设置中重启内核"), true);
        emit systemProxyChanged(false);
        return;
    }

    if (m_systemProxyEnabled) {
        persistSystemProxyPreference(true);
        return;
    }
    QString error;
    if (!m_systemProxy.enable(m_settings.mixedPort, &error)) {
        m_systemProxyEnabled = false;
        emit notification(error, true);
        emit systemProxyChanged(false);
        return;
    }
    m_systemProxyEnabled = true;
    m_pendingSystemProxy = false;
    persistSystemProxyPreference(true);
    emit systemProxyChanged(true);
    log(cq::LogLevel::Info,
        QStringLiteral("系统代理已指向 127.0.0.1:%1").arg(m_settings.mixedPort));
}

void ClashController::persistSystemProxyPreference(bool enabled)
{
    if (m_settings.systemProxyEnabled == enabled)
        return;
    m_settings.systemProxyEnabled = enabled;
    m_settingsStore.save(m_settings);
}

void ClashController::syncSystemProxy(bool enabled)
{
    if (m_demo) {
        if (m_systemProxyEnabled != enabled) {
            m_systemProxyEnabled = enabled;
            emit systemProxyChanged(enabled);
        }
        return;
    }
    if (enabled) {
        if (!m_systemProxyEnabled) {
            QString error;
            if (m_systemProxy.enable(m_settings.mixedPort, &error)) {
                m_systemProxyEnabled = true;
                emit systemProxyChanged(true);
            } else {
                emit notification(error, true);
            }
        }
        return;
    }
    if (m_systemProxy.appliedByUs())
        m_systemProxy.disable();
    if (m_systemProxyEnabled) {
        m_systemProxyEnabled = false;
        emit systemProxyChanged(false);
    }
}

void ClashController::selectProxy(const QString& group, const QString& node)
{
    for (cq::ProxyGroup& item : m_proxyGroups) {
        if (item.name == group) {
            item.now = node;
            break;
        }
    }
    emit proxyGroupsChanged(m_proxyGroups);
    if (m_demo) {
        log(cq::LogLevel::Info, QStringLiteral("已选择节点：%1").arg(node));
        return;
    }
    if (m_state == cq::CoreState::Running) {
        m_api.selectProxy(group, node);
        QTimer::singleShot(500, this, [this] {
            if (m_state == cq::CoreState::Running)
                m_api.fetchProxies();
        });
        QTimer::singleShot(1000, &m_networkProbe, &NetworkProbe::refreshAll);
    }
}

void ClashController::testProxy(const QString& node)
{
    if (node.compare(QStringLiteral("DIRECT"), Qt::CaseInsensitive) == 0 ||
        node.compare(QStringLiteral("REJECT"), Qt::CaseInsensitive) == 0)
        return;
    if (m_demo) {
        QTimer::singleShot(180, this, [this, node] {
            int delay = demoDelayFor(node);
            if (!node.contains(QStringLiteral("DIRECT")) && !node.contains(QStringLiteral("REJECT")))
                delay += static_cast<int>(QRandomGenerator::global()->bounded(-12, 20));
            for (cq::ProxyGroup& group : m_proxyGroups) {
                if (cq::ProxyNode* item = group.findNode(node))
                    item->delay = delay;
            }
            emit delayUpdated(node, delay);
        });
        return;
    }
    if (m_state == cq::CoreState::Running)
        m_api.testDelay(node);
}

void ClashController::testGroup(const QString& groupName)
{
    for (const cq::ProxyGroup& group : m_proxyGroups) {
        if (group.name != groupName)
            continue;
        for (const cq::ProxyNode& node : group.nodes)
            testProxy(node.name);
        return;
    }
}

void ClashController::closeConnection(const QString& id)
{
    if (m_demo) {
        for (int i = 0; i < m_connections.size(); ++i) {
            if (m_connections.at(i).id == id) {
                m_connections.removeAt(i);
                break;
            }
        }
        emit connectionsChanged(m_connections);
        return;
    }
    if (m_state == cq::CoreState::Running) {
        m_api.closeConnection(id);
        QTimer::singleShot(300, this, [this] {
            if (m_state == cq::CoreState::Running)
                m_api.fetchConnections();
        });
    }
}

void ClashController::closeAllConnections()
{
    if (m_demo) {
        m_connections.clear();
        emit connectionsChanged(m_connections);
        return;
    }
    if (m_state == cq::CoreState::Running) {
        m_api.closeAllConnections();
        QTimer::singleShot(300, this, [this] {
            if (m_state == cq::CoreState::Running)
                m_api.fetchConnections();
        });
    }
}

void ClashController::importUrl(const QString& name, const QUrl& url)
{
    m_profiles.importUrl(name, url);
}

void ClashController::importFile(const QString& path)
{
    m_profiles.importFile(path);
}

void ClashController::refreshProfile(const QString& id)
{
    m_profiles.refreshProfile(id);
}

void ClashController::removeProfile(const QString& id)
{
    m_profiles.removeProfile(id);
}

void ClashController::activateProfile(const QString& id)
{
    m_profiles.activate(id);
}

void ClashController::scheduleConfigReload()
{
    if (m_demo)
        return;
    if (m_state == cq::CoreState::Starting) {
        m_configReloadWhenReady = true;
        return;
    }
    if (m_state == cq::CoreState::Running)
        m_configReloadTimer.start();
}

bool ClashController::prepareRuntimeConfig(QString* error)
{
    const auto active = m_profiles.activeProfile();
    if (!active) {
        if (error)
            *error = QStringLiteral("没有可用的配置，无法热重载");
        return false;
    }

    const QString source = m_profiles.configPath(*active);
    if (!QFileInfo::exists(source)) {
        if (error)
            *error = QStringLiteral("当前配置文件不存在：%1").arg(source);
        return false;
    }

    const QString runtime = AppPaths::runtimeConfigFile();
    if (!RuntimeConfig::write(source, runtime, m_settings, error))
        return false;
    m_activeConfigPath = runtime;
    return true;
}

void ClashController::performConfigReload()
{
    m_configReloadTimer.stop();
    if (m_state == cq::CoreState::Starting) {
        m_configReloadWhenReady = true;
        return;
    }
    if (m_demo || m_state != cq::CoreState::Running)
        return;

    if (m_configReloadInFlight) {
        m_configReloadQueued = true;
        return;
    }

    const auto active = m_profiles.activeProfile();
    if (!active) {
        log(cq::LogLevel::Error, QStringLiteral("没有可用的配置，无法热重载"));
        return;
    }

    const QVector<nq::scripts::ScriptStep> steps = m_scripts.resolveSteps(active->id);
    if (!steps.isEmpty()) {
        if (m_composer.isBusy()) {
            m_configReloadQueued = true;
            return;
        }
        QString readError;
        const QByteArray raw = m_profiles.configContent(active->id, &readError);
        if (!readError.isEmpty() || raw.isEmpty()) {
            log(cq::LogLevel::Error, readError.isEmpty()
                                          ? QStringLiteral("当前订阅配置为空")
                                          : readError);
            return;
        }

        m_reloadRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_configReloadInFlight = true;
        m_configReloadQueued = false;
        log(cq::LogLevel::Info,
            QStringLiteral("正在执行 %1 个 JavaScript 脚本并校验配置…").arg(steps.size()));

        nq::scripts::ConfigComposer::Request request;
        request.requestId = m_reloadRequestId;
        request.profileId = active->id;
        request.profileName = active->name;
        request.rawConfig = raw;
        request.settings = m_settings;
        request.steps = steps;
        request.scriptSettings = m_scripts.settings();
        request.corePath = m_coreBinary.isEmpty()
                               ? AppPaths::resolveCorePath(m_settings.corePath)
                               : m_coreBinary;
        request.dataDirectory = AppPaths::dataDir();
        request.candidatePath =
            AppPaths::runtimeDir() + QStringLiteral("/config.candidate.yaml");
        request.outputPath = AppPaths::runtimeConfigFile();
        request.validationTimeoutMs = 20000;
        request.validate = true;
        request.stopOnError = m_scripts.settings().stopOnError;

        if (!m_composer.build(request)) {
            m_reloadRequestId.clear();
            m_configReloadInFlight = false;
            log(cq::LogLevel::Error, QStringLiteral("无法启动脚本配置生成"));
        }
        return;
    }

    QString error;
    if (!prepareRuntimeConfig(&error)) {
        log(cq::LogLevel::Error, error);
        emit notification(error, true);
        return;
    }

    m_configReloadInFlight = true;
    m_configReloadQueued = false;
    log(cq::LogLevel::Info,
        QStringLiteral("正在热重载配置「%1」（不重启内核）…").arg(activeProfileName()));
    m_api.reloadConfig(QDir::toNativeSeparators(m_activeConfigPath), true);
}

void ClashController::handleComposerFinished(const nq::scripts::ConfigComposer::Result& result)
{
    if (m_shuttingDown)
        return;

    if (!m_startRequestId.isEmpty() && result.requestId == m_startRequestId) {
        m_startRequestId.clear();
        m_scriptStartPending = false;
        if (m_state != cq::CoreState::Starting)
            return;
        if (result.canceled) {
            if (m_state == cq::CoreState::Starting) {
                m_state = cq::CoreState::Stopped;
                emit stateChanged(m_state);
            }
            return;
        }
        if (!result.ok) {
            m_scripts.recordPipelineResult(result.pipeline, result.profileName);
            if (!result.pipeline.failingScriptId.isEmpty())
                m_scripts.handlePipelineFailure(result.pipeline);
            failStartup(result.error);
            return;
        }
        const auto profile = m_profiles.find(result.profileId);
        if (!profile || m_profiles.activeId() != result.profileId) {
            failStartup(QStringLiteral("配置在脚本生成期间已切换，请重新连接"));
            return;
        }
        m_scripts.recordPipelineResult(result.pipeline, result.profileName);
        m_scriptStartProfileId.clear();
        m_scriptStartProfileName.clear();
        launchCore(*profile, result.outputPath);
        return;
    }

    if (!m_reloadRequestId.isEmpty() && result.requestId == m_reloadRequestId) {
        m_reloadRequestId.clear();
        m_configReloadInFlight = false;
        m_scripts.recordPipelineResult(result.pipeline, result.profileName);
        if (m_state != cq::CoreState::Running) {
            m_configReloadQueued = false;
            return;
        }
        if (result.canceled) {
            m_configReloadQueued = false;
            return;
        }
        if (m_profiles.activeId() != result.profileId) {
            log(cq::LogLevel::Info,
                QStringLiteral("配置在脚本生成期间已切换，忽略本次热重载结果"));
            m_configReloadQueued = false;
            scheduleConfigReload();
            return;
        }
        if (!result.ok) {
            if (!result.pipeline.failingScriptId.isEmpty())
                m_scripts.handlePipelineFailure(result.pipeline);
            const QString message =
                QStringLiteral("JavaScript 覆写后的配置未通过校验，已保留原有配置：%1")
                    .arg(result.error);
            m_lastError = message;
            log(cq::LogLevel::Error, message);
            emit notification(message, true);
            if (m_configReloadQueued) {
                m_configReloadQueued = false;
                m_configReloadTimer.start(80);
            }
            return;
        }

        m_configReloadInFlight = true;
        m_configReloadQueued = false;
        log(cq::LogLevel::Info,
            QStringLiteral("脚本配置校验通过，正在热重载（不重启内核）…"));
        m_api.reloadConfig(QDir::toNativeSeparators(result.outputPath), true);
        return;
    }
}

void ClashController::handleConfigReloadFinished(bool success, const QString& error)
{
    if (!m_configReloadInFlight)
        return;
    m_configReloadInFlight = false;

    if (m_state != cq::CoreState::Running) {
        m_configReloadQueued = false;
        return;
    }

    if (!success) {
        m_configReloadQueued = false;
        const QString message = QStringLiteral("配置热重载失败：%1")
                                    .arg(error.isEmpty() ? QStringLiteral("未知错误") : error);
        m_lastError = message;
        log(cq::LogLevel::Error, message);
        emit notification(message, true);
        return;
    }

    m_lastError.clear();
    log(cq::LogLevel::Info, QStringLiteral("配置已热重载，Mihomo 内核进程保持运行"));
    m_networkProbe.setProxyPort(m_settings.mixedPort);
    m_networkProbe.refreshAll();

    if (m_systemProxyEnabled && m_systemProxy.port() != m_settings.mixedPort) {
        QString proxyError;
        if (m_systemProxy.enable(m_settings.mixedPort, &proxyError)) {
            log(cq::LogLevel::Info,
                QStringLiteral("系统代理端口已更新为 127.0.0.1:%1").arg(m_settings.mixedPort));
        } else {
            m_systemProxy.disable();
            m_systemProxyEnabled = false;
            emit systemProxyChanged(false);
            emit notification(QStringLiteral("系统代理端口更新失败，已恢复原设置：%1")
                                  .arg(proxyError),
                              true);
        }
    }

    m_api.fetchConfig();
    m_api.fetchProxies();
    m_api.fetchRules();
    m_api.fetchConnections();

    if (m_configReloadQueued) {
        m_configReloadQueued = false;
        m_configReloadTimer.start(80);
    }
}

void ClashController::updateMemoryUsage()
{
    if (m_demo)
        return;

    cq::MemoryUsage usage;
#ifdef Q_OS_WIN
    auto readWorkingSet = [](HANDLE process, qint64* bytes) {
        if (!process)
            return false;
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        if (!GetProcessMemoryInfo(process,
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters))) {
            return false;
        }
        *bytes = static_cast<qint64>(counters.WorkingSetSize);
        return true;
    };

    usage.appValid = readWorkingSet(GetCurrentProcess(), &usage.appWorkingSet);
    const qint64 kernelPid = m_process.processId();
    if (kernelPid > 0) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                     static_cast<DWORD>(kernelPid));
        if (process) {
            usage.kernelValid = readWorkingSet(process, &usage.kernelWorkingSet);
            CloseHandle(process);
        }
    }
#endif

    if (usage.appWorkingSet == m_memoryUsage.appWorkingSet &&
        usage.kernelWorkingSet == m_memoryUsage.kernelWorkingSet &&
        usage.appValid == m_memoryUsage.appValid &&
        usage.kernelValid == m_memoryUsage.kernelValid) {
        return;
    }

    m_memoryUsage = usage;
    emit memoryUsageChanged(m_memoryUsage);
}

void ClashController::maybeAutoUpdateSubscription()
{
    if (m_demo || m_settings.subscriptionIntervalHours <= 0 || m_profiles.isBusy())
        return;
    const auto active = m_profiles.activeProfile();
    if (!active || !active->remote() || active->url.isEmpty())
        return;
    const qint64 interval = static_cast<qint64>(m_settings.subscriptionIntervalHours) * 3600;
    if (active->updatedAt.isValid() &&
        active->updatedAt.secsTo(QDateTime::currentDateTime()) < interval)
        return;
    log(cq::LogLevel::Info, QStringLiteral("开始自动更新当前订阅…"));
    m_profiles.refreshProfile(active->id);
}

QString ClashController::controllerEndpoint() const
{
    return QStringLiteral("127.0.0.1:%1").arg(m_settings.controllerPort);
}

QString ClashController::dataDirectory() const
{
    return AppPaths::dataDir();
}

QString ClashController::activeProfileName() const
{
    const auto active = m_profiles.activeProfile();
    return active ? active->name : QStringLiteral("未选择配置");
}

cq::LogLevel ClashController::parseLogLevel(const QString& raw)
{
    const QString lower = raw.toLower();
    if (lower.contains(QStringLiteral("level=error")) ||
        lower.contains(QStringLiteral("level=fatal")) ||
        lower.contains(QStringLiteral("\"level\":\"error\"")))
        return cq::LogLevel::Error;
    if (lower.contains(QStringLiteral("level=warn")) ||
        lower.contains(QStringLiteral("\"level\":\"warn")))
        return cq::LogLevel::Warning;
    if (lower.contains(QStringLiteral("level=debug")) ||
        lower.contains(QStringLiteral("level=trace")) ||
        lower.contains(QStringLiteral("\"level\":\"debug\"")))
        return cq::LogLevel::Debug;
    return cq::LogLevel::Info;
}

QString ClashController::cleanCoreMessage(const QString& raw)
{
    QString message = raw.trimmed();
    if (message.startsWith(QLatin1Char('{'))) {
        const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
        const QString jsonMessage = object.value(QStringLiteral("msg")).toString();
        if (!jsonMessage.isEmpty())
            return jsonMessage;
    }

    static const QRegularExpression messageRe(
        QStringLiteral("msg=\"((?:[^\"\\\\]|\\\\.)*)\""));
    const QRegularExpressionMatch match = messageRe.match(message);
    if (match.hasMatch()) {
        message = match.captured(1);
        message.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        message.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
        return message;
    }

    if (message.size() > 2000)
        return message.left(2000) + QStringLiteral("…");
    return message;
}

void ClashController::logCoreLine(const QString& raw)
{
    if (raw.trimmed().isEmpty())
        return;
    const cq::LogLevel level = parseLogLevel(raw);
    log(level, cleanCoreMessage(raw));
}

void ClashController::log(cq::LogLevel level, const QString& message)
{
    if (message.isEmpty())
        return;
    if (static_cast<int>(level) < appLogThreshold(m_settings.appLogLevel))
        return;
    cq::LogEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.message = message;
    m_logHistory.append(entry);
    while (m_logHistory.size() > kMaxLogEntries)
        m_logHistory.removeFirst();
    appendLogFile(entry);
    emit logAdded(entry);
}

void ClashController::appendLogFile(const cq::LogEntry& entry)
{
    if (!m_settings.appLogToFile)
        return;
    QFile file(AppPaths::logFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const char* level = "INFO";
    switch (entry.level) {
    case cq::LogLevel::Debug:
        level = "DEBUG";
        break;
    case cq::LogLevel::Warning:
        level = "WARN";
        break;
    case cq::LogLevel::Error:
        level = "ERROR";
        break;
    default:
        break;
    }
    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(entry.time.toString(Qt::ISODate), QString::fromLatin1(level),
                                  entry.message);
    file.write(line.toUtf8());
}

void ClashController::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_restartPending = false;
    m_pendingSystemProxy = false;
    m_scriptStartPending = false;
    m_startRequestId.clear();
    m_reloadRequestId.clear();
    m_composer.shutdown();
    m_scripts.shutdown();
    m_probeTimer.stop();
    m_startupTimer.stop();
    m_statsTimer.stop();
    m_configReloadTimer.stop();
    m_subscriptionTimer.stop();
    m_demoTimer.stop();
    m_memoryTimer.stop();
    m_networkProbe.setRunning(false);
    m_configReloadInFlight = false;
    m_configReloadQueued = false;
    m_configReloadWhenReady = false;
    m_api.stopPolling();
    if (!m_demo) {
        if (m_systemProxy.appliedByUs())
            m_systemProxy.disable();
        m_systemProxyEnabled = false;
        if (m_process.isRunning()) {
            m_process.stopAndWait(4000);
            log(cq::LogLevel::Info, QStringLiteral("Mihomo 核心已退出"));
        }
    }
}

bool ClashController::isElevated() const
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
#else
    return false;
#endif
}
