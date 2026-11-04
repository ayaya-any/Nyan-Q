#include "ui/pages/DashboardPage.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/Formatters.h"
#include "core/NetworkProbe.h"
#include "models/ConnectionListModel.h"
#include "ui/PageHeader.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/SiteLatencyTile.h"
#include "ui/widgets/TrafficChart.h"

#include <QAbstractSocket>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QNetworkInterface>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {
QString detectLocalIp()
{
    QString fallback;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback())
                continue;
            const QString ip = address.toString();
            if (ip.startsWith(QStringLiteral("169.254.")))
                continue;
            const bool privateAddress =
                ip.startsWith(QStringLiteral("10.")) ||
                ip.startsWith(QStringLiteral("192.168.")) ||
                (ip.startsWith(QStringLiteral("172.")) &&
                 ip.section(QLatin1Char('.'), 1, 1).toInt() >= 16 &&
                 ip.section(QLatin1Char('.'), 1, 1).toInt() <= 31);
            if (privateAddress)
                return ip;
            if (fallback.isEmpty())
                fallback = ip;
        }
    }
    return fallback.isEmpty() ? QStringLiteral("未获取") : fallback;
}
}  // namespace

DashboardPage::DashboardPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();

    if (m_controller->isDemo()) {
        for (int i = 0; i < 44; ++i) {
            const double wave = std::sin(i / 4.2) * 0.36 + std::sin(i / 1.7) * 0.12;
            m_chart->addSample(static_cast<qint64>(2350000.0 * (1.0 + wave)),
                               static_cast<qint64>(128000.0 * (1.0 - wave * 0.6)));
        }
        m_memoryChart->clear();
        for (int i = 0; i < 24; ++i) {
            const double appWave = std::sin(i / 3.1) * 5.0;
            const double kernelWave = std::cos(i / 4.4) * 2.6;
            m_memoryChart->addSample(
                static_cast<qint64>((84.0 + appWave) * 1024 * 1024),
                static_cast<qint64>((46.0 + kernelWave) * 1024 * 1024));
        }
        m_lastSampleSecond = m_controller->stats().connectedSeconds;
    }
}

void DashboardPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scroll);

    auto* content = new QWidget(scroll);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 10, 32, 8);
    layout->setSpacing(10);

    m_header = new PageHeader(QStringLiteral("仪表盘"), {}, content);
    layout->addWidget(m_header);
    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(16);

    auto* traffic = new M3Card(content, 20);
    traffic->setFixedHeight(234);
    auto* trafficLayout = new QVBoxLayout(traffic);
    trafficLayout->setContentsMargins(18, 12, 18, 12);
    trafficLayout->setSpacing(4);

    auto* trafficHeader = new QHBoxLayout;
    trafficHeader->setContentsMargins(0, 0, 0, 0);
    auto* trafficTitle = new QLabel(QStringLiteral("实时流量"), traffic);
    trafficTitle->setProperty("role", "cardTitle");
    trafficHeader->addWidget(trafficTitle);
    trafficHeader->addStretch();
    auto* live = new M3Pill(QStringLiteral("最近 60 秒"), traffic);
    live->setTone(M3Pill::Tone::Neutral);
    trafficHeader->addWidget(live);
    trafficLayout->addLayout(trafficHeader);

    auto* speeds = new QHBoxLayout;
    speeds->setContentsMargins(0, 2, 0, 0);
    speeds->setSpacing(26);
    auto addSpeed = [&](const QString& glyph, const QString& caption, QLabel** value,
                        M3IconLabel** iconOut, const QColor& color) {
        auto* column = new QVBoxLayout;
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);
        auto* captionRow = new QHBoxLayout;
        captionRow->setContentsMargins(0, 0, 0, 0);
        captionRow->setSpacing(6);
        auto* icon = new M3IconLabel(glyph, traffic);
        icon->setFixedBox(15, 15);
        icon->setGlyphSize(14);
        icon->setColor(color);
        *iconOut = icon;
        captionRow->addWidget(icon);
        auto* label = new QLabel(caption, traffic);
        label->setProperty("role", "caption");
        captionRow->addWidget(label);
        captionRow->addStretch();
        column->addLayout(captionRow);
        *value = new QLabel(QStringLiteral("0 B/s"), traffic);
        (*value)->setFont(Fonts::text(22, QFont::DemiBold));
        column->addWidget(*value);
        speeds->addLayout(column);
    };
    addSpeed(QStringLiteral("arrow_downward"), QStringLiteral("下载"), &m_downloadSpeed,
             &m_downloadIcon, pal().chartDownload);
    addSpeed(QStringLiteral("arrow_upward"), QStringLiteral("上传"), &m_uploadSpeed,
             &m_uploadIcon, pal().chartUpload);
    speeds->addStretch();
    trafficLayout->addLayout(speeds);

    m_chart = new TrafficChart(traffic);
    m_chart->setMinimumHeight(104);
    trafficLayout->addWidget(m_chart, 1);

    auto* trafficDivider = new M3Divider(traffic);
    trafficLayout->addWidget(trafficDivider);
    auto* sessionRow = new QHBoxLayout;
    sessionRow->setContentsMargins(0, 1, 0, 0);
    sessionRow->setSpacing(8);
    auto* sessionCaption = new QLabel(QStringLiteral("本次会话"), traffic);
    sessionCaption->setProperty("role", "micro");
    sessionRow->addWidget(sessionCaption);
    m_sessionTotals = new QLabel(QStringLiteral("↓ 0 B   ↑ 0 B"), traffic);
    m_sessionTotals->setProperty("role", "bodyStrong");
    sessionRow->addWidget(m_sessionTotals);
    sessionRow->addStretch();
    m_sessionTime = new QLabel(QStringLiteral("00:00:00"), traffic);
    m_sessionTime->setProperty("role", "micro");
    sessionRow->addWidget(m_sessionTime);
    trafficLayout->addLayout(sessionRow);
    topRow->addWidget(traffic, 7);

    auto* memoryCard = new M3Card(content, 20);
    memoryCard->setFixedHeight(234);
    auto* memoryLayout = new QVBoxLayout(memoryCard);
    memoryLayout->setContentsMargins(18, 12, 18, 12);
    memoryLayout->setSpacing(3);

    auto* memoryHeader = new QHBoxLayout;
    memoryHeader->setContentsMargins(0, 0, 0, 0);
    memoryHeader->setSpacing(7);
    auto* memoryIcon = new M3IconLabel(QStringLiteral("memory"), memoryCard);
    memoryIcon->setFixedBox(24, 24);
    memoryIcon->setGlyphSize(15);
    memoryIcon->setBadge(true);
    memoryHeader->addWidget(memoryIcon, 0, Qt::AlignVCenter);
    auto* memoryTitle = new QLabel(QStringLiteral("内存信息"), memoryCard);
    memoryTitle->setProperty("role", "cardTitle");
    memoryHeader->addWidget(memoryTitle);
    memoryHeader->addStretch();
    memoryLayout->addLayout(memoryHeader);

    auto* memoryValues = new QHBoxLayout;
    memoryValues->setContentsMargins(0, 4, 0, 0);
    memoryValues->setSpacing(22);
    auto addMemoryValue = [&](const QString& name, const QColor& color, QLabel** value) {
        auto* column = new QVBoxLayout;
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        auto* captionRow = new QHBoxLayout;
        captionRow->setContentsMargins(0, 0, 0, 0);
        captionRow->setSpacing(4);
        auto* dot = new QLabel(QStringLiteral("●"), memoryCard);
        dot->setStyleSheet(QStringLiteral("color: %1; font-size: 7px;").arg(color.name()));
        captionRow->addWidget(dot);
        auto* caption = new QLabel(name, memoryCard);
        caption->setProperty("role", "micro");
        captionRow->addWidget(caption);
        captionRow->addStretch();
        column->addLayout(captionRow);
        *value = new QLabel(QStringLiteral("—"), memoryCard);
        (*value)->setFont(Fonts::text(14, QFont::DemiBold));
        (*value)->setToolTip(QStringLiteral("进程工作集内存"));
        column->addWidget(*value);
        memoryValues->addLayout(column);
    };
    addMemoryValue(QStringLiteral("应用"), pal().chartDownload, &m_appMemory);
    addMemoryValue(QStringLiteral("内核"), pal().chartUpload, &m_kernelMemory);
    memoryValues->addStretch();
    memoryLayout->addLayout(memoryValues);

    m_memoryChart = new TrafficChart(memoryCard);
    m_memoryChart->setMinimumHeight(78);
    m_memoryChart->setMaxSamples(24);
    m_memoryChart->setCaption(QString());
    m_memoryChart->setScaleFromZero(false);
    memoryLayout->addWidget(m_memoryChart, 1);
    topRow->addWidget(memoryCard, 4);

    layout->addLayout(topRow);

    auto* middleRow = new QHBoxLayout;
    middleRow->setContentsMargins(0, 0, 0, 0);
    middleRow->setSpacing(16);

    auto* exitCard = new M3Card(content, 20);
    exitCard->setMinimumHeight(130);
    auto* exitLayout = new QVBoxLayout(exitCard);
    exitLayout->setContentsMargins(14, 9, 14, 9);
    exitLayout->setSpacing(3);

    auto* exitHeader = new QHBoxLayout;
    exitHeader->setContentsMargins(0, 0, 0, 0);
    exitHeader->setSpacing(7);
    auto* exitIcon = new M3IconLabel(QStringLiteral("public"), exitCard);
    exitIcon->setFixedBox(23, 23);
    exitIcon->setGlyphSize(15);
    exitIcon->setBadge(true);
    exitHeader->addWidget(exitIcon, 0, Qt::AlignVCenter);
    auto* exitTitle = new QLabel(QStringLiteral("网络出口"), exitCard);
    exitTitle->setProperty("role", "cardTitle");
    exitHeader->addWidget(exitTitle);
    exitHeader->addStretch();
    m_exitRefresh = new M3IconButton(QStringLiteral("refresh"), exitCard);
    m_exitRefresh->setDiameter(26);
    m_exitRefresh->setGlyphSize(15);
    m_exitRefresh->setToolTip(QStringLiteral("刷新出口 IP"));
    exitHeader->addWidget(m_exitRefresh);
    exitLayout->addLayout(exitHeader);

    m_exitIp = new QLabel(QStringLiteral("—"), exitCard);
    m_exitIp->setFont(Fonts::text(23, QFont::DemiBold));
    m_exitIp->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_exitIp->setWordWrap(true);
    exitLayout->addWidget(m_exitIp);

    auto* exitMeta = new QHBoxLayout;
    exitMeta->setContentsMargins(0, 0, 0, 0);
    exitMeta->setSpacing(7);
    m_exitCountryPill = new M3Pill(QStringLiteral("未连接"), exitCard);
    m_exitCountryPill->setTone(M3Pill::Tone::Neutral);
    exitMeta->addWidget(m_exitCountryPill);
    m_exitRegion = new QLabel(QStringLiteral("连接内核后显示地区"), exitCard);
    m_exitRegion->setProperty("role", "caption");
    exitMeta->addWidget(m_exitRegion);
    exitMeta->addStretch();
    exitLayout->addLayout(exitMeta);

    m_exitOrg = new QLabel(QString(), exitCard);
    m_exitOrg->setProperty("role", "micro");
    exitLayout->addWidget(m_exitOrg);
    exitLayout->addStretch();

    auto* leftColumn = new QVBoxLayout;
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(8);
    leftColumn->addWidget(exitCard, 1);

    auto* modeCard = new M3Card(content, 20);
    modeCard->setFixedHeight(62);
    auto* modeLayout = new QHBoxLayout(modeCard);
    modeLayout->setContentsMargins(14, 9, 14, 9);
    modeLayout->setSpacing(8);
    auto* modeTitle = new QLabel(QStringLiteral("代理模式"), modeCard);
    modeTitle->setProperty("role", "cardTitle");
    modeLayout->addWidget(modeTitle);
    m_modeControl = new M3SegmentedControl(
        {QStringLiteral("规则"), QStringLiteral("全局"), QStringLiteral("直连")}, modeCard);
    m_modeControl->setCompact(false);
    m_modeControl->setFixedHeight(40);
    modeLayout->addWidget(m_modeControl, 1);
    leftColumn->addWidget(modeCard);
    middleRow->addLayout(leftColumn, 1);

    auto* kernelCard = new M3Card(content, 20);
    kernelCard->setMinimumHeight(190);
    auto* kernelLayout = new QVBoxLayout(kernelCard);
    kernelLayout->setContentsMargins(14, 8, 14, 7);
    kernelLayout->setSpacing(3);

    auto* ipRow = new QHBoxLayout;
    ipRow->setContentsMargins(0, 0, 0, 0);
    ipRow->setSpacing(7);
    auto* ipIcon = new M3IconLabel(QStringLiteral("lan"), kernelCard);
    ipIcon->setFixedBox(24, 24);
    ipIcon->setGlyphSize(15);
    ipIcon->setBadge(true);
    ipRow->addWidget(ipIcon, 0, Qt::AlignVCenter);

    auto* ipColumn = new QVBoxLayout;
    ipColumn->setContentsMargins(0, 0, 0, 0);
    ipColumn->setSpacing(0);
    auto* ipCaption = new QLabel(QStringLiteral("本机内网 IP"), kernelCard);
    ipCaption->setProperty("role", "micro");
    ipColumn->addWidget(ipCaption);
    const QString localIp = m_controller->isDemo() ? QStringLiteral("192.168.1.24")
                                                   : detectLocalIp();
    m_localIpLabel = new QLabel(localIp, kernelCard);
    m_localIpLabel->setFont(Fonts::text(14, QFont::DemiBold));
    m_localIpLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_localIpLabel->setToolTip(QStringLiteral("本机局域网地址，局域网代理开启后供其他设备使用"));
    ipColumn->addWidget(m_localIpLabel);
    ipRow->addLayout(ipColumn, 1);

    auto* copyIp = new M3IconButton(QStringLiteral("content_copy"), kernelCard);
    copyIp->setDiameter(24);
    copyIp->setGlyphSize(14);
    copyIp->setToolTip(QStringLiteral("复制内网 IP"));
    copyIp->setEnabled(localIp != QStringLiteral("未获取"));
    connect(copyIp, &QAbstractButton::clicked, this, [this, copyIp] {
        const QString ip = m_localIpLabel->text();
        if (ip.isEmpty() || ip == QStringLiteral("未获取"))
            return;
        QApplication::clipboard()->setText(ip);
        QToolTip::showText(QCursor::pos(), QStringLiteral("已复制内网 IP"), copyIp);
    });
    ipRow->addWidget(copyIp, 0, Qt::AlignVCenter);
    kernelLayout->addLayout(ipRow);
    kernelLayout->addWidget(new M3Divider(kernelCard));

    m_sysProxyRow = new M3SettingRow(QStringLiteral("系统代理"),
                                     QStringLiteral("接管 Windows 系统代理"), kernelCard);
    m_sysProxyRow->setLeadingIcon(QStringLiteral("public"));
    m_sysProxyRow->setContentMargins(0, 1, 0, 1);
    m_sysProxySwitch = new M3Switch(m_sysProxyRow);
    m_sysProxyRow->setTrailing(m_sysProxySwitch);
    kernelLayout->addWidget(m_sysProxyRow);

    m_tunRow = new M3SettingRow(QStringLiteral("TUN 模式"),
                                QStringLiteral("虚拟网卡全局透明代理"), kernelCard);
    m_tunRow->setLeadingIcon(QStringLiteral("security"));
    m_tunRow->setContentMargins(0, 1, 0, 1);
    m_tunSwitch = new M3Switch(m_tunRow);
    m_tunRow->setTrailing(m_tunSwitch);
    kernelLayout->addWidget(m_tunRow);

    m_allowLanRow = new M3SettingRow(QStringLiteral("局域网代理"),
                                     QStringLiteral("允许同一网络中的设备通过本机代理上网"),
                                     kernelCard);
    m_allowLanRow->setLeadingIcon(QStringLiteral("wifi"));
    m_allowLanRow->setContentMargins(0, 1, 0, 1);
    m_allowLanSwitch = new M3Switch(m_allowLanRow);
    m_allowLanRow->setTrailing(m_allowLanSwitch);
    kernelLayout->addWidget(m_allowLanRow);
    kernelLayout->addStretch(1);
    middleRow->addWidget(kernelCard, 1);

    auto* serviceCard = new M3Card(content, 20);
    serviceCard->setMinimumHeight(190);
    auto* serviceLayout = new QVBoxLayout(serviceCard);
    serviceLayout->setContentsMargins(16, 11, 16, 10);
    serviceLayout->setSpacing(5);

    auto* serviceHeader = new QHBoxLayout;
    serviceHeader->setContentsMargins(0, 0, 0, 0);
    serviceHeader->setSpacing(7);
    auto* serviceIcon = new M3IconLabel(QStringLiteral("network_check"), serviceCard);
    serviceIcon->setFixedBox(23, 23);
    serviceIcon->setGlyphSize(15);
    serviceIcon->setBadge(true);
    serviceHeader->addWidget(serviceIcon, 0, Qt::AlignVCenter);
    auto* serviceTitle = new QLabel(QStringLiteral("服务状态"), serviceCard);
    serviceTitle->setProperty("role", "cardTitle");
    serviceHeader->addWidget(serviceTitle);
    serviceHeader->addStretch();
    m_siteRefresh = new M3IconButton(QStringLiteral("refresh"), serviceCard);
    m_siteRefresh->setDiameter(26);
    m_siteRefresh->setGlyphSize(15);
    m_siteRefresh->setToolTip(QStringLiteral("重新测试网站延迟"));
    serviceHeader->addWidget(m_siteRefresh);
    serviceLayout->addLayout(serviceHeader);

    const auto sites = m_controller->networkProbe()->siteLatencies();
    for (const cq::SiteLatency& site : sites) {
        auto* tile = new SiteLatencyTile(site, serviceCard);
        tile->setFixedHeight(38);
        m_siteTiles.insert(site.id, tile);
        serviceLayout->addWidget(tile);
    }
    serviceLayout->addStretch(1);
    auto* serviceCaption = new QLabel(QStringLiteral("每 60 秒自动检测"), serviceCard);
    serviceCaption->setProperty("role", "micro");
    serviceLayout->addWidget(serviceCaption);
    middleRow->addWidget(serviceCard, 1);
    layout->addLayout(middleRow);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(16);

    auto* activityCard = new M3Card(content, 20);
    activityCard->setFixedHeight(238);
    auto* activityLayout = new QVBoxLayout(activityCard);
    activityLayout->setContentsMargins(16, 6, 16, 4);
    activityLayout->setSpacing(2);

    auto* activityHeader = new QHBoxLayout;
    activityHeader->setContentsMargins(0, 0, 0, 0);
    activityHeader->setSpacing(7);
    auto* activityIcon = new M3IconLabel(QStringLiteral("device_hub"), activityCard);
    activityIcon->setFixedBox(24, 24);
    activityIcon->setGlyphSize(15);
    activityIcon->setBadge(true);
    activityHeader->addWidget(activityIcon, 0, Qt::AlignVCenter);
    auto* activityTitle = new QLabel(QStringLiteral("活动连接"), activityCard);
    activityTitle->setProperty("role", "cardTitle");
    activityHeader->addWidget(activityTitle);
    m_connectionCountPill = new M3Pill(QStringLiteral("0"), activityCard);
    m_connectionCountPill->setTone(M3Pill::Tone::Neutral);
    activityHeader->addWidget(m_connectionCountPill);
    activityHeader->addStretch();
    auto* allConnections = new M3IconButton(QStringLiteral("arrow_forward"), activityCard);
    allConnections->setDiameter(28);
    allConnections->setGlyphSize(15);
    allConnections->setToolTip(QStringLiteral("查看全部连接"));
    connect(allConnections, &QAbstractButton::clicked, this,
            [this] { emit navigateRequested(3); });
    activityHeader->addWidget(allConnections);
    activityLayout->addLayout(activityHeader);

    m_connectionModel = new ConnectionListModel(this);
    m_connectionModel->setConnections(m_controller->connections());
    m_connectionView = new ConnectionListView(activityCard);
    m_connectionView->setModel(m_connectionModel);
    auto* connectionDelegate = new ConnectionDelegate(m_connectionView);
    connectionDelegate->setCompact(true);
    m_connectionView->setItemDelegate(connectionDelegate);
    m_connectionView->setFrameShape(QFrame::NoFrame);
    m_connectionView->setStyleSheet(
        QStringLiteral("QListView { background: transparent; border: none; }"));
    m_connectionView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_connectionView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_connectionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_connectionView->viewport()->setAutoFillBackground(false);
    connect(m_connectionView, &ConnectionListView::closeRequested, this,
            [this](const QString& id) { m_controller->closeConnection(id); });
    activityLayout->addWidget(m_connectionView, 1);
    bottomRow->addWidget(activityCard, 7);

    auto* configCard = new M3Card(content, 20);
    configCard->setFixedHeight(238);
    auto* configLayout = new QVBoxLayout(configCard);
    configLayout->setContentsMargins(16, 11, 16, 10);
    configLayout->setSpacing(4);

    auto* configHeader = new QHBoxLayout;
    configHeader->setContentsMargins(0, 0, 0, 0);
    configHeader->setSpacing(7);
    auto* configIcon = new M3IconLabel(QStringLiteral("layers"), configCard);
    configIcon->setFixedBox(24, 24);
    configIcon->setGlyphSize(15);
    configIcon->setBadge(true);
    configHeader->addWidget(configIcon, 0, Qt::AlignVCenter);
    auto* configTitle = new QLabel(QStringLiteral("配置"), configCard);
    configTitle->setProperty("role", "cardTitle");
    configHeader->addWidget(configTitle);
    configHeader->addStretch();
    auto* configArrow = new M3IconButton(QStringLiteral("arrow_forward"), configCard);
    configArrow->setDiameter(28);
    configArrow->setGlyphSize(15);
    configArrow->setToolTip(QStringLiteral("打开配置页面"));
    connect(configArrow, &QAbstractButton::clicked, this,
            [this] { emit navigateRequested(2); });
    configHeader->addWidget(configArrow);
    configLayout->addLayout(configHeader);

    m_configName = new QLabel(QStringLiteral("未选择配置"), configCard);
    m_configName->setFont(Fonts::text(18, QFont::DemiBold));
    m_configName->setWordWrap(true);
    configLayout->addWidget(m_configName);

    auto* configMeta = new QHBoxLayout;
    configMeta->setContentsMargins(0, 0, 0, 0);
    configMeta->setSpacing(7);
    m_configTypePill = new M3Pill(QStringLiteral("—"), configCard);
    m_configTypePill->setTone(M3Pill::Tone::Neutral);
    configMeta->addWidget(m_configTypePill);
    m_configSource = new QLabel(configCard);
    m_configSource->setProperty("role", "caption");
    m_configSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configMeta->addWidget(m_configSource, 1);
    configLayout->addLayout(configMeta);

    auto* configDetails = new QGridLayout;
    configDetails->setContentsMargins(0, 1, 0, 0);
    configDetails->setHorizontalSpacing(8);
    configDetails->setVerticalSpacing(2);
    auto addConfigDetail = [&](const QString& label, QLabel** value) {
        const int row = configDetails->rowCount();
        auto* caption = new QLabel(label, configCard);
        caption->setProperty("role", "micro");
        configDetails->addWidget(caption, row, 0);
        *value = new QLabel(QStringLiteral("—"), configCard);
        (*value)->setProperty("role", "caption");
        (*value)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        configDetails->addWidget(*value, row, 1);
    };
    addConfigDetail(QStringLiteral("到期"), &m_configExpiry);
    addConfigDetail(QStringLiteral("自动更新"), &m_configInterval);
    addConfigDetail(QStringLiteral("配置文件"), &m_configFile);
    configDetails->setColumnStretch(1, 1);
    configLayout->addLayout(configDetails);

    m_configUpdated = new QLabel(configCard);
    m_configUpdated->setProperty("role", "micro");
    configLayout->addWidget(m_configUpdated);

    m_configUsage = new QLabel(configCard);
    m_configUsage->setProperty("role", "micro");
    configLayout->addWidget(m_configUsage);
    m_configBar = new M3ProgressBar(configCard);
    m_configBar->setBarHeight(4);
    configLayout->addWidget(m_configBar);
    configLayout->addStretch(1);

    auto* configHint = new QLabel(QStringLiteral("点击右上角进入配置管理"), configCard);
    configHint->setProperty("role", "micro");
    configLayout->addWidget(configHint);
    bottomRow->addWidget(configCard, 4);

    layout->addLayout(bottomRow);
    layout->addStretch(1);

    connect(m_modeControl, &M3SegmentedControl::currentChanged, this,
            [this](int index, const QString&) {
                static const QStringList modes = {QStringLiteral("rule"), QStringLiteral("global"),
                                                  QStringLiteral("direct")};
                m_controller->setMode(modes.value(index, QStringLiteral("rule")));
            });
    connect(m_sysProxySwitch, &QAbstractButton::clicked, this, [this](bool checked) {
        m_controller->setSystemProxy(checked);
    });
    connect(m_tunSwitch, &QAbstractButton::clicked, this, [this](bool checked) {
        m_controller->setTunEnabled(checked);
    });
    connect(m_allowLanSwitch, &QAbstractButton::clicked, this, [this](bool checked) {
        m_controller->setAllowLanEnabled(checked);
    });
    connect(m_exitRefresh, &QAbstractButton::clicked, this, [this] {
        m_controller->networkProbe()->refreshExitInfo();
    });
    connect(m_siteRefresh, &QAbstractButton::clicked, this, [this] {
        m_controller->networkProbe()->refreshSiteLatencies();
    });
}

void DashboardPage::wire()
{
    connect(m_controller, &ClashController::stateChanged, this, [this] {
        if (m_controller->state() == cq::CoreState::Running)
            m_lastSampleSecond = -1;
        updateState();
        updateStats();
    });
    connect(m_controller, &ClashController::statsChanged, this,
            [this](const cq::SessionStats&) {
                updateStats();
            });
    connect(m_controller, &ClashController::connectionsChanged, this, [this] {
        updateStats();
        updateActivity();
    });
    connect(m_controller, &ClashController::settingsChanged, this, &DashboardPage::updateSettings);
    connect(m_controller, &ClashController::activeProfileChanged, this,
            [this] {
                updateSettings();
                updateConfigCard();
            });
    connect(m_controller->profileStore(), &ProfileStore::profileUpdated, this,
            [this](const QString&) { updateConfigCard(); });
    connect(m_controller, &ClashController::modeChanged, this,
            [this](const QString&) { updateSettings(); });
    connect(m_controller, &ClashController::systemProxyChanged, this,
            [this](bool) { updateSettings(); });
    connect(m_controller, &ClashController::tunChanged, this,
            [this](bool) { updateSettings(); });
    connect(m_controller, &ClashController::coreVersionChanged, this,
            [this](const QString&) { updateSettings(); updateState(); });
    connect(m_controller->networkProbe(), &NetworkProbe::exitInfoChanged, this,
            &DashboardPage::updateNetworkExit);
    connect(m_controller->networkProbe(), &NetworkProbe::siteLatenciesChanged, this,
            &DashboardPage::updateSiteLatencies);
    connect(m_controller, &ClashController::memoryUsageChanged, this,
            [this](const cq::MemoryUsage&) { updateMemoryUsage(); });
}

void DashboardPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_initialScrollReset)
        return;
    m_initialScrollReset = true;
    QTimer::singleShot(0, this, [this] {
        if (m_scroll)
            m_scroll->verticalScrollBar()->setValue(0);
    });
}

void DashboardPage::refresh()
{
    updateSettings();
    updateState();
    updateStats();
    updateMemoryUsage();
    updateActivity();
    updateConfigCard();
}

void DashboardPage::updateState()
{
    const cq::CoreState state = m_controller->state();
    if (state != cq::CoreState::Running)
        m_chart->clear();

    updateNetworkExit();
    updateSiteLatencies();
}

void DashboardPage::updateNetworkExit()
{
    const cq::ExitInfo& info = m_controller->networkProbe()->exitInfo();
    const bool running = m_controller->state() == cq::CoreState::Running;

    QString ip = QStringLiteral("—");
    QString region = QStringLiteral("连接内核后显示出口 IP 与地区");
    QString organization;
    QString pill = QStringLiteral("未连接");
    bool canRefresh = false;

    if (running && info.loading) {
        ip = info.ip.isEmpty() ? QStringLiteral("正在获取…") : info.ip;
        region = info.ip.isEmpty() ? QStringLiteral("正在通过当前节点查询出口信息…")
                                   : QStringLiteral("正在刷新出口信息…");
        organization = info.organization;
        pill = QStringLiteral("探测中");
    } else if (running && info.valid) {
        ip = info.ip;
        pill = info.countryCode;
        if (!info.countryName.isEmpty() && info.countryName != info.countryCode)
            pill += QStringLiteral(" · ") + info.countryName;
        if (pill.isEmpty())
            pill = QStringLiteral("未知地区");
        organization = info.organization;
        canRefresh = true;

        QStringList place;
        if (!info.city.isEmpty())
            place.append(info.city);
        if (!info.region.isEmpty() && info.region != info.city)
            place.append(info.region);
        region = place.isEmpty() ? QStringLiteral("位置信息不可用") : place.join(QStringLiteral(" · "));
    } else if (running) {
        ip = QStringLiteral("获取失败");
        region = info.error.isEmpty() ? QStringLiteral("点击刷新重试") : info.error;
        pill = QStringLiteral("不可用");
        canRefresh = true;
    }

    m_exitIp->setText(ip);
    m_exitRegion->setText(region);
    m_exitOrg->setText(organization);
    m_exitOrg->setVisible(!organization.isEmpty());
    m_exitCountryPill->setText(pill);
    m_exitCountryPill->setTone(!running   ? M3Pill::Tone::Neutral
                               : info.loading ? M3Pill::Tone::Warning
                               : info.valid   ? M3Pill::Tone::Info
                                              : M3Pill::Tone::Error);
    m_exitRefresh->setEnabled(canRefresh && !m_controller->isDemo());

    QString tooltip = QStringLiteral("出口 IP：%1\n%2").arg(ip, region);
    if (!organization.isEmpty())
        tooltip += QStringLiteral("\n%1").arg(organization);
    if (info.updatedAt.isValid())
        tooltip += QStringLiteral("\n更新于 %1").arg(
            info.updatedAt.toString(QStringLiteral("HH:mm:ss")));
    m_exitIp->setToolTip(tooltip);
    m_exitRegion->setToolTip(tooltip);
    m_exitCountryPill->setToolTip(tooltip);
}

void DashboardPage::updateSiteLatencies()
{
    const QVector<cq::SiteLatency>& sites = m_controller->networkProbe()->siteLatencies();
    bool pending = false;
    QDateTime latest;
    for (const cq::SiteLatency& site : sites) {
        if (SiteLatencyTile* tile = m_siteTiles.value(site.id))
            tile->setSite(site);
        if (site.pending)
            pending = true;
        if (site.testedAt.isValid() && (!latest.isValid() || site.testedAt > latest))
            latest = site.testedAt;
    }

    const bool running = m_controller->state() == cq::CoreState::Running;
    QString status;
    if (!running)
        status = QStringLiteral("连接内核后自动测试");
    else if (pending)
        status = QStringLiteral("正在测试当前出口的连通性…");
    else if (latest.isValid())
        status = QStringLiteral("更新于 %1 · 每 60 秒自动刷新")
                     .arg(latest.toString(QStringLiteral("HH:mm:ss")));
    else
        status = QStringLiteral("等待首次测试");

    m_siteRefresh->setToolTip(QStringLiteral("重新测试网站延迟\n%1").arg(status));
    m_siteRefresh->setEnabled(running && !m_controller->isDemo() && !pending);
}

void DashboardPage::updateMemoryUsage()
{
    const cq::MemoryUsage usage = m_controller->memoryUsage();
    m_appMemory->setText(usage.appValid ? Formatters::bytes(usage.appWorkingSet)
                                        : QStringLiteral("—"));
    m_kernelMemory->setText(usage.kernelValid ? Formatters::bytes(usage.kernelWorkingSet)
                                              : QStringLiteral("—"));
    if (usage.appValid && usage.kernelValid) {
        m_memoryChart->addSample(usage.appWorkingSet, usage.kernelWorkingSet);
    }
}

void DashboardPage::updateSettings()
{
    int index = 0;
    if (m_controller->mode() == QStringLiteral("global"))
        index = 1;
    else if (m_controller->mode() == QStringLiteral("direct"))
        index = 2;
    m_modeControl->setCurrentIndex(index);

    const bool sysProxy = m_controller->systemProxyEnabled();
    m_sysProxySwitch->setChecked(sysProxy);
    m_sysProxyRow->setSubtitle(
        sysProxy ? QStringLiteral("已接管 Windows 系统代理")
                 : QStringLiteral("通过 127.0.0.1:%1 转发").arg(m_controller->settings().mixedPort));

    m_tunSwitch->setChecked(m_controller->tunEnabled());
    m_tunRow->setSubtitle(m_controller->tunEnabled()
                              ? QStringLiteral("已启用 · 通过内核 API 热应用")
                              : QStringLiteral("虚拟网卡全局透明代理"));

    m_allowLanSwitch->setChecked(m_controller->settings().allowLan);
    m_allowLanRow->setSubtitle(
        m_controller->settings().allowLan
            ? QStringLiteral("已启用 · 同一局域网中的设备可访问")
            : QStringLiteral("允许同一网络中的设备通过本机代理上网"));
    updateConfigCard();
    updateState();
}

void DashboardPage::updateStats()
{
    const cq::SessionStats stats = m_controller->stats();
    if (m_downloadIcon)
        m_downloadIcon->setColor(pal().chartDownload);
    if (m_uploadIcon)
        m_uploadIcon->setColor(pal().chartUpload);
    m_downloadSpeed->setText(Formatters::rate(stats.downloadSpeed));
    m_uploadSpeed->setText(Formatters::rate(stats.uploadSpeed));
    m_sessionTotals->setText(QStringLiteral("↓ %1   ↑ %2")
                                 .arg(Formatters::bytes(stats.downloadTotal),
                                      Formatters::bytes(stats.uploadTotal)));
    m_sessionTime->setText(Formatters::clockDuration(stats.connectedSeconds));

    if (m_controller->state() == cq::CoreState::Running) {
        if (stats.connectedSeconds != m_lastSampleSecond) {
            m_chart->addSample(stats.downloadSpeed, stats.uploadSpeed);
            m_lastSampleSecond = stats.connectedSeconds;
        }
    } else {
        m_lastSampleSecond = -1;
    }
}

void DashboardPage::updateActivity()
{
    const auto& connections = m_controller->connections();
    m_connectionModel->setConnections(connections);
    const int count = static_cast<int>(connections.size());
    m_connectionCountPill->setText(QString::number(count));
    m_connectionCountPill->setTone(count > 0 ? M3Pill::Tone::Success
                                            : M3Pill::Tone::Neutral);
}

void DashboardPage::updateConfigCard()
{
    const auto profile = m_controller->profileStore()->activeProfile();
    if (!profile) {
        m_configName->setText(QStringLiteral("未选择配置"));
        m_configTypePill->setText(QStringLiteral("—"));
        m_configTypePill->setTone(M3Pill::Tone::Neutral);
        m_configSource->setText(QStringLiteral("前往配置页面导入订阅或 YAML"));
        m_configExpiry->setText(QStringLiteral("—"));
        m_configInterval->setText(QStringLiteral("—"));
        m_configFile->setText(QStringLiteral("—"));
        m_configUpdated->setText(QStringLiteral("更新于 —"));
        m_configUsage->setVisible(false);
        m_configBar->setVisible(false);
        return;
    }

    m_configName->setText(profile->name);
    if (profile->remote()) {
        m_configTypePill->setText(QStringLiteral("订阅"));
        m_configTypePill->setTone(M3Pill::Tone::Info);
    } else if (profile->type == QStringLiteral("builtin")) {
        m_configTypePill->setText(QStringLiteral("内置"));
        m_configTypePill->setTone(M3Pill::Tone::Neutral);
    } else {
        m_configTypePill->setText(QStringLiteral("本地"));
        m_configTypePill->setTone(M3Pill::Tone::Neutral);
    }

    if (profile->remote()) {
        const QUrl url(profile->url);
        m_configSource->setText(url.host().isEmpty() ? QStringLiteral("远程订阅")
                                                     : url.host());
    } else if (profile->type == QStringLiteral("builtin")) {
        m_configSource->setText(QStringLiteral("随应用内置的配置"));
    } else {
        m_configSource->setText(QStringLiteral("本地 YAML 文件"));
    }
    m_configSource->setToolTip(profile->url.isEmpty() ? m_configSource->text()
                                                       : profile->url);

    m_configExpiry->setText(profile->expireAt > 0 ? Formatters::expiry(profile->expireAt)
                                                  : QStringLiteral("—"));
    m_configInterval->setText(
        profile->updateIntervalHours > 0
            ? QStringLiteral("每 %1 小时").arg(profile->updateIntervalHours)
            : QStringLiteral("未开启"));
    m_configFile->setText(profile->fileName.isEmpty() ? QStringLiteral("—")
                                                      : profile->fileName);
    m_configFile->setToolTip(m_configFile->text());

    m_configUpdated->setText(
        profile->updatedAt.isValid()
            ? QStringLiteral("更新于 %1").arg(Formatters::relativeTime(profile->updatedAt))
            : QStringLiteral("更新于 —"));

    if (profile->total > 0) {
        const qint64 used = profile->upload + profile->download;
        m_configUsage->setText(QStringLiteral("已用 %1 / %2")
                                   .arg(Formatters::bytes(used),
                                        Formatters::bytes(profile->total)));
        m_configBar->setValue(100.0 * static_cast<double>(used) /
                              static_cast<double>(profile->total));
        m_configUsage->setVisible(true);
        m_configBar->setVisible(true);
    } else {
        m_configUsage->setVisible(false);
        m_configBar->setVisible(false);
    }
}
