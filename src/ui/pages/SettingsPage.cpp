#include "ui/pages/SettingsPage.h"

#include "app/AppPaths.h"
#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/Formatters.h"
#include "ui/PageHeader.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace {
M3Card* makeSection(QWidget* parent, QVBoxLayout* sections, const QString& title,
                    const QString& subtitle)
{
    auto* card = new M3Card(parent, 20);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 14);
    layout->setSpacing(6);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setProperty("role", "section");
    layout->addWidget(titleLabel);
    if (!subtitle.isEmpty()) {
        auto* subtitleLabel = new QLabel(subtitle, card);
        subtitleLabel->setProperty("role", "caption");
        subtitleLabel->setWordWrap(true);
        layout->addWidget(subtitleLabel);
    }
    layout->addSpacing(4);
    auto* divider = new M3Divider(card);
    layout->addWidget(divider);
    sections->addWidget(card);
    return card;
}

M3SettingRow* addRow(QVBoxLayout* layout, QWidget* parent, const QString& title,
                     const QString& subtitle, const QString& glyph, QWidget* trailing,
                     bool dividerBefore)
{
    if (dividerBefore)
        layout->addWidget(new M3Divider(parent));
    auto* row = new M3SettingRow(title, subtitle, parent);
    row->setLeadingIcon(glyph);
    row->setTrailing(trailing);
    layout->addWidget(row);
    return row;
}

M3Switch* makeSwitch(QWidget* parent)
{
    auto* toggle = new M3Switch(parent);
    return toggle;
}
}  // namespace

SettingsPage::SettingsPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();

}

void SettingsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("设置"),
                              QStringLiteral("调整外观、网络端口与内核行为。"), this);
    m_dirtyPill = new M3Pill(QStringLiteral("未保存"), this);
    m_dirtyPill->setTone(M3Pill::Tone::Warning);
    m_dirtyPill->setDot(true);
    m_dirtyPill->hide();
    m_header->addAction(m_dirtyPill);
    m_save = new M3Button(QStringLiteral("保存并应用"), M3Button::Variant::Filled, this);
    m_save->setIconText(QStringLiteral("check"));
    m_save->setEnabled(false);
    m_header->addAction(m_save);
    root->addWidget(m_header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(m_scroll, 1);

    m_content = new QWidget(m_scroll);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_scroll->setWidget(m_content);
    m_sections = new QVBoxLayout(m_content);
    m_sections->setContentsMargins(0, 2, 4, 8);
    m_sections->setSpacing(14);

    // Appearance
    M3Card* appearance = makeSection(
        m_content, m_sections, QStringLiteral("外观"),
        QStringLiteral("主题会立即应用到整个界面。"));
    auto* appearanceLayout = qobject_cast<QVBoxLayout*>(appearance->layout());
    m_theme = new M3SegmentedControl(
        {QStringLiteral("深色"), QStringLiteral("浅色"), QStringLiteral("跟随系统")}, appearance);
    m_theme->setCompact(true);
    m_theme->setFixedWidth(330);
    addRow(appearanceLayout, appearance, QStringLiteral("主题模式"),
           QStringLiteral("深色模式适合夜间与长时间使用"), QStringLiteral("palette"), m_theme,
           false);

    // Network
    M3Card* network = makeSection(
        m_content, m_sections, QStringLiteral("网络"),
        QStringLiteral("网络项通过 Mihomo API 热应用；仅控制器端口、API 密钥和内核路径"
                       "变更需要重启。请确保端口没有被其他程序占用。"));
    auto* networkLayout = qobject_cast<QVBoxLayout*>(network->layout());

    m_mixedPort = new QLineEdit(network);
    m_mixedPort->setValidator(new QIntValidator(1, 65535, m_mixedPort));
    m_mixedPort->setFixedWidth(116);
    m_mixedPort->setAlignment(Qt::AlignCenter);
    addRow(networkLayout, network, QStringLiteral("混合代理端口"),
           QStringLiteral("HTTP / SOCKS5 共用端口"), QStringLiteral("lan"), m_mixedPort, false);

    m_controllerPort = new QLineEdit(network);
    m_controllerPort->setValidator(new QIntValidator(1, 65535, m_controllerPort));
    m_controllerPort->setFixedWidth(116);
    m_controllerPort->setAlignment(Qt::AlignCenter);
    addRow(networkLayout, network, QStringLiteral("外部控制器端口"),
           QStringLiteral("本机 RESTful API，仅监听 127.0.0.1"), QStringLiteral("settings_ethernet"),
           m_controllerPort, true);

    m_allowLan = makeSwitch(network);
    addRow(networkLayout, network, QStringLiteral("允许局域网连接"),
           QStringLiteral("让同一网络下的设备通过本机代理上网"), QStringLiteral("wifi"),
           m_allowLan, true);

    m_ipv6 = makeSwitch(network);
    addRow(networkLayout, network, QStringLiteral("启用 IPv6"),
           QStringLiteral("允许 Mihomo 建立 IPv6 连接"), QStringLiteral("public"), m_ipv6, true);

    m_tun = makeSwitch(network);
    addRow(networkLayout, network, QStringLiteral("TUN 模式"),
           m_controller->isElevated()
               ? QStringLiteral("已以管理员身份运行，可创建虚拟网卡")
               : QStringLiteral("虚拟网卡全局透明代理，通常需要以管理员身份运行"),
           QStringLiteral("security"), m_tun, true);

    // Startup and behavior
    M3Card* behavior = makeSection(
        m_content, m_sections, QStringLiteral("启动与行为"),
        QStringLiteral("控制 Clash-Q 的启动、托盘与窗口行为。Mihomo 随应用启动和退出。"));
    auto* behaviorLayout = qobject_cast<QVBoxLayout*>(behavior->layout());

    m_autoLaunch = makeSwitch(behavior);
    addRow(behaviorLayout, behavior, QStringLiteral("开机自动启动"),
           QStringLiteral("通过 Windows 当前用户启动项添加 Clash-Q"), QStringLiteral("login"),
           m_autoLaunch, true);

    m_startMinimized = makeSwitch(behavior);
    addRow(behaviorLayout, behavior, QStringLiteral("启动时最小化"),
           QStringLiteral("启动后只显示托盘的 Clash-Q 图标"), QStringLiteral("minimize"),
           m_startMinimized, true);

    m_closeToTray = makeSwitch(behavior);
    addRow(behaviorLayout, behavior, QStringLiteral("关闭时最小化到托盘"),
           QStringLiteral("关闭主窗口时继续在后台运行内核"), QStringLiteral("notifications"),
           m_closeToTray, true);

    // Subscription
    M3Card* subscription = makeSection(
        m_content, m_sections, QStringLiteral("订阅"),
        QStringLiteral("自动更新会在后台下载当前远程订阅，并热重载配置。"));
    auto* subscriptionLayout = qobject_cast<QVBoxLayout*>(subscription->layout());

    m_updateInterval = new M3SegmentedControl(
        {QStringLiteral("关闭"), QStringLiteral("6 小时"), QStringLiteral("12 小时"),
         QStringLiteral("24 小时")},
        subscription);
    m_updateInterval->setCompact(true);
    m_updateInterval->setFixedWidth(390);
    addRow(subscriptionLayout, subscription, QStringLiteral("自动更新订阅"),
           QStringLiteral("仅对当前激活的远程订阅生效"), QStringLiteral("update"),
           m_updateInterval, false);

    m_delayUrl = new QLineEdit(subscription);
    m_delayUrl->setMinimumWidth(310);
    m_delayUrl->setPlaceholderText(QStringLiteral("http://www.gstatic.com/generate_204"));
    addRow(subscriptionLayout, subscription, QStringLiteral("延迟测试地址"),
           QStringLiteral("测速时请求的 204 地址，应返回空响应"), QStringLiteral("speed"),
           m_delayUrl, true);

    // Core
    M3Card* core = makeSection(
        m_content, m_sections, QStringLiteral("内核"),
        QStringLiteral("Clash-Q 通过本地 REST API 控制 Mihomo，不修改内核二进制。"));
    auto* coreLayout = qobject_cast<QVBoxLayout*>(core->layout());

    m_coreStatusPill = new M3Pill(QStringLiteral("未连接"), core);
    m_coreStatusPill->setTone(M3Pill::Tone::Neutral);
    m_coreStatusPill->setDot(true);
    addRow(coreLayout, core, QStringLiteral("当前内核"),
           QStringLiteral("Mihomo Meta · 随 Clash-Q 启动和退出"), QStringLiteral("dns"),
           m_coreStatusPill, false);

    m_restartCore = new M3Button(QStringLiteral("重启"), M3Button::Variant::Tonal, core);
    m_restartCore->setIconText(QStringLiteral("restart_alt"));
    m_restartCore->setCompact(true);
    addRow(coreLayout, core, QStringLiteral("重启内核"),
           QStringLiteral("重新启动 Mihomo 并重新加载当前配置"), QStringLiteral("restart_alt"),
           m_restartCore, false);

    auto* pathHost = new QWidget(core);
    auto* pathLayout = new QHBoxLayout(pathHost);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    m_corePath = new QLineEdit(pathHost);
    m_corePath->setMinimumWidth(280);
    m_corePath->setPlaceholderText(QStringLiteral("自动查找程序目录下的 mihomo.exe"));
    pathLayout->addWidget(m_corePath);
    auto* browse = new M3IconButton(QStringLiteral("folder_open"), pathHost);
    browse->setDiameter(34);
    browse->setGlyphSize(17);
    browse->setToolTip(QStringLiteral("选择 mihomo.exe"));
    pathLayout->addWidget(browse);
    m_corePathRow = addRow(coreLayout, core, QStringLiteral("Mihomo 可执行文件"),
                           QStringLiteral("留空时自动使用程序目录中的 mihomo.exe"),
                           QStringLiteral("terminal"), pathHost, true);
    connect(browse, &QAbstractButton::clicked, this, &SettingsPage::browseCore);
    connect(m_restartCore, &QAbstractButton::clicked, this, [this] {
        if (m_controller)
            m_controller->restart();
    });

    m_logLevel = new M3SegmentedControl(
        {QStringLiteral("信息"), QStringLiteral("警告"), QStringLiteral("错误"),
         QStringLiteral("调试")},
        core);
    m_logLevel->setCompact(true);
    m_logLevel->setFixedWidth(390);
    addRow(coreLayout, core, QStringLiteral("日志级别"),
           QStringLiteral("调试日志会明显增加输出量"), QStringLiteral("receipt_long"),
           m_logLevel, true);

    auto* dataButton = new M3Button(QStringLiteral("打开"), M3Button::Variant::Tonal, core);
    dataButton->setIconText(QStringLiteral("folder_open"));
    dataButton->setCompact(true);
    m_dataRow = addRow(coreLayout, core, QStringLiteral("数据目录"),
                       QDir::toNativeSeparators(AppPaths::dataDir()), QStringLiteral("folder"),
                       dataButton, true);
    connect(dataButton, &QAbstractButton::clicked, this, &SettingsPage::openDataDirectory);

    // About
    M3Card* about = makeSection(m_content, m_sections, QStringLiteral("关于"), {});
    auto* aboutLayout = qobject_cast<QVBoxLayout*>(about->layout());
    auto* aboutRow = new QWidget(about);
    auto* aboutRowLayout = new QHBoxLayout(aboutRow);
    aboutRowLayout->setContentsMargins(4, 8, 4, 8);
    aboutRowLayout->setSpacing(14);
    auto* logo = new QLabel(aboutRow);
    logo->setPixmap(Theme::instance().appIcon().pixmap(48, 48));
    logo->setFixedSize(48, 48);
    aboutRowLayout->addWidget(logo);
    auto* aboutText = new QVBoxLayout;
    aboutText->setContentsMargins(0, 0, 0, 0);
    aboutText->setSpacing(3);
    auto* name = new QLabel(QStringLiteral("Clash-Q %1").arg(AppPaths::appVersion()), aboutRow);
    name->setProperty("role", "cardTitle");
    aboutText->addWidget(name);
    auto* tech = new QLabel(QStringLiteral("Material Design 3 · Qt 6.12.0 · C++20 · Mihomo"),
                            aboutRow);
    tech->setProperty("role", "caption");
    aboutText->addWidget(tech);
    auto* copyright =
        new QLabel(QStringLiteral("界面与控制器源代码开源构建，内核以独立进程运行。"), aboutRow);
    copyright->setProperty("role", "micro");
    aboutText->addWidget(copyright);
    aboutRowLayout->addLayout(aboutText, 1);
    m_aboutKernelPill = new M3Pill(QStringLiteral("内核未运行"), aboutRow);
    m_aboutKernelPill->setTone(M3Pill::Tone::Neutral);
    aboutRowLayout->addWidget(m_aboutKernelPill, 0, Qt::AlignVCenter);
    aboutLayout->addWidget(aboutRow);
    m_sections->addStretch(1);

    connect(m_save, &QAbstractButton::clicked, this, &SettingsPage::apply);

    auto connectSwitch = [this](M3Switch* toggle) {
        connect(toggle, &QAbstractButton::clicked, this, [this](bool) { markDirty(); });
    };
    connectSwitch(m_allowLan);
    connectSwitch(m_ipv6);
    connectSwitch(m_tun);
    connectSwitch(m_autoLaunch);
    connectSwitch(m_startMinimized);
    connectSwitch(m_closeToTray);

    for (QLineEdit* edit : {m_mixedPort, m_controllerPort, m_delayUrl, m_corePath}) {
        connect(edit, &QLineEdit::textChanged, this, [this](const QString&) {
            if (!m_syncing)
                markDirty();
        });
    }
    connect(m_theme, &M3SegmentedControl::currentChanged, this,
            [this](int, const QString&) { markDirty(); });
    connect(m_updateInterval, &M3SegmentedControl::currentChanged, this,
            [this](int, const QString&) { markDirty(); });
    connect(m_logLevel, &M3SegmentedControl::currentChanged, this,
            [this](int, const QString&) { markDirty(); });
}

void SettingsPage::wire()
{
    connect(m_controller, &ClashController::settingsChanged, this, [this] {
        if (!m_dirty)
            refreshFromSettings();
        updateCoreInfo();
    });
    connect(m_controller, &ClashController::coreVersionChanged, this,
            [this](const QString&) { updateCoreInfo(); });
    connect(m_controller, &ClashController::stateChanged, this,
            [this](cq::CoreState) { updateCoreInfo(); });
}

void SettingsPage::refresh()
{
    refreshFromSettings();
    updateCoreInfo();
    updateDirtyUi();
}

void SettingsPage::refreshFromSettings()
{
    if (!m_controller)
        return;
    m_syncing = true;
    const cq::AppSettings& settings = m_controller->settings();

    int themeIndex = settings.theme == QStringLiteral("light") ? 1
                     : settings.theme == QStringLiteral("system") ? 2
                                                                  : 0;
    m_theme->setCurrentIndex(themeIndex);
    m_mixedPort->setText(QString::number(settings.mixedPort));
    m_controllerPort->setText(QString::number(settings.controllerPort));
    m_allowLan->setChecked(settings.allowLan);
    m_ipv6->setChecked(settings.ipv6);
    m_tun->setChecked(settings.tunEnabled);

    m_autoLaunch->setChecked(settings.autoLaunch);
    m_startMinimized->setChecked(settings.startMinimized);
    m_closeToTray->setChecked(settings.closeToTray);

    int intervalIndex = 0;
    if (settings.subscriptionIntervalHours >= 24)
        intervalIndex = 3;
    else if (settings.subscriptionIntervalHours >= 12)
        intervalIndex = 2;
    else if (settings.subscriptionIntervalHours > 0)
        intervalIndex = 1;
    m_updateInterval->setCurrentIndex(intervalIndex);
    m_delayUrl->setText(settings.delayTestUrl);
    m_corePath->setText(settings.corePath);

    int logIndex = 0;
    if (settings.logLevel == QStringLiteral("warning"))
        logIndex = 1;
    else if (settings.logLevel == QStringLiteral("error"))
        logIndex = 2;
    else if (settings.logLevel == QStringLiteral("debug"))
        logIndex = 3;
    m_logLevel->setCurrentIndex(logIndex);

    m_dataRow->setSubtitle(QDir::toNativeSeparators(AppPaths::dataDir()));
    m_syncing = false;
}

void SettingsPage::markDirty()
{
    if (m_syncing)
        return;
    if (!m_dirty) {
        m_dirty = true;
        updateDirtyUi();
    }
}

void SettingsPage::updateDirtyUi()
{
    m_dirtyPill->setVisible(m_dirty);
    m_save->setEnabled(m_dirty);
    m_save->setText(m_dirty ? QStringLiteral("保存并应用") : QStringLiteral("已应用"));
}

void SettingsPage::apply()
{
    if (!m_controller)
        return;

    cq::AppSettings next = m_controller->settings();
    next.mixedPort = m_mixedPort->text().toInt();
    next.controllerPort = m_controllerPort->text().toInt();
    next.allowLan = m_allowLan->isChecked();
    next.ipv6 = m_ipv6->isChecked();
    next.tunEnabled = m_tun->isChecked();
    next.autoLaunch = m_autoLaunch->isChecked();
    next.startMinimized = m_startMinimized->isChecked();
    next.closeToTray = m_closeToTray->isChecked();
    next.delayTestUrl = m_delayUrl->text().trimmed();
    next.corePath = m_corePath->text().trimmed();
    next.theme = m_theme->currentIndex() == 1   ? QStringLiteral("light")
                 : m_theme->currentIndex() == 2 ? QStringLiteral("system")
                                                : QStringLiteral("dark");
    static const QStringList levels = {QStringLiteral("info"), QStringLiteral("warning"),
                                      QStringLiteral("error"), QStringLiteral("debug")};
    next.logLevel = levels.value(m_logLevel->currentIndex(), QStringLiteral("info"));
    static const QList<int> intervals = {0, 6, 12, 24};
    next.subscriptionIntervalHours = intervals.value(m_updateInterval->currentIndex(), 0);
    // The mode is controlled from the dashboard; preserve its current value.
    next.mode = m_controller->settings().mode;

    if (m_controller->updateSettings(next, true)) {
        m_dirty = false;
        refreshFromSettings();
        updateDirtyUi();
    }
}

void SettingsPage::browseCore()
{
    const QString path = QFileDialog::getOpenFileName(
        window(), QStringLiteral("选择 Mihomo 可执行文件"), QCoreApplication::applicationDirPath(),
        QStringLiteral("Mihomo (mihomo.exe);;可执行文件 (*.exe);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        m_corePath->setText(path);
        markDirty();
    }
}

void SettingsPage::openDataDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir()));
}

void SettingsPage::updateCoreInfo()
{
    const cq::CoreState state = m_controller->state();
    QString text;
    M3Pill::Tone tone = M3Pill::Tone::Neutral;
    switch (state) {
    case cq::CoreState::Running:
        text = QStringLiteral("运行中");
        tone = M3Pill::Tone::Success;
        break;
    case cq::CoreState::Starting:
        text = QStringLiteral("启动中");
        tone = M3Pill::Tone::Warning;
        break;
    case cq::CoreState::Stopping:
        text = QStringLiteral("停止中");
        tone = M3Pill::Tone::Warning;
        break;
    case cq::CoreState::Failed:
        text = QStringLiteral("异常");
        tone = M3Pill::Tone::Error;
        break;
    case cq::CoreState::Stopped:
        text = QStringLiteral("已停止");
        break;
    }
    m_coreStatusPill->setText(text);
    m_coreStatusPill->setTone(tone);
    if (m_restartCore) {
        m_restartCore->setEnabled(state != cq::CoreState::Starting &&
                                  state != cq::CoreState::Stopping);
    }
    m_aboutKernelPill->setText(m_controller->coreVersion().isEmpty()
                                   ? QStringLiteral("内核未运行")
                                   : QStringLiteral("Mihomo %1").arg(m_controller->coreVersion()));
    m_aboutKernelPill->setTone(tone);

    const QString resolved = AppPaths::resolveCorePath(m_corePath->text().trimmed());
    if (m_corePathRow) {
        const QString subtitle = resolved.isEmpty()
                                     ? QStringLiteral("未找到 Mihomo，请选择可执行文件")
                                     : QStringLiteral("已检测到 · %1").arg(
                                           QDir::toNativeSeparators(resolved));
        m_corePathRow->setSubtitle(subtitle);
    }
}
