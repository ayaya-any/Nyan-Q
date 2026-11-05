#include "ui/MainWindow.h"

#include "app/AppPaths.h"
#include "app/Elevation.h"
#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/Formatters.h"
#include "ui/Sidebar.h"
#include "ui/TitleBar.h"
#include "ui/editor/ConfigEditorView.h"
#include "ui/pages/ConnectionsPage.h"
#include "ui/pages/DashboardPage.h"
#include "ui/pages/LogsPage.h"
#include "ui/pages/ProfilesPage.h"
#include "ui/pages/ProxiesPage.h"
#include "ui/pages/RulesPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class ResizeHandle final : public QWidget {
public:
    explicit ResizeHandle(Qt::Edges edges, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_edges(edges)
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setMouseTracking(true);
        if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
            setCursor(Qt::SizeFDiagCursor);
        else if (edges == (Qt::RightEdge | Qt::TopEdge) ||
                 edges == (Qt::LeftEdge | Qt::BottomEdge))
            setCursor(Qt::SizeBDiagCursor);
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::SizeVerCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (auto* handle = window()->windowHandle()) {
                if (handle->startSystemResize(m_edges)) {
                    event->accept();
                    return;
                }
            }
        }
        QWidget::mousePressEvent(event);
    }

private:
    Qt::Edges m_edges;
};

class RootBackdrop : public QWidget {
public:
    explicit RootBackdrop(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), pal().surface);
    }
};

class ContentBackdrop : public QWidget {
public:
    explicit ContentBackdrop(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const QRectF r = rect();
        const qreal radius = 20;
        path.moveTo(r.left(), r.top() + radius);
        path.quadTo(r.topLeft(), r.topLeft() + QPointF(radius, 0));
        path.lineTo(r.topRight());
        path.lineTo(r.bottomRight());
        path.lineTo(r.bottomLeft());
        path.closeSubpath();
        painter.fillPath(path, pal().background);

        painter.setPen(QPen(pal().outlineVariant, 1));
        painter.drawLine(QPointF(r.left() + radius, r.top() + 0.5),
                         QPointF(r.right(), r.top() + 0.5));
    }
};

QString stateLabel(cq::CoreState state)
{
    return cq::coreStateLabel(state);
}

}  // namespace

MainWindow::MainWindow(ClashController* controller, QWidget* parent)
    : QMainWindow(parent)
    , m_controller(controller)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(1100, 720);
    resize(1360, 860);
    setWindowTitle(QStringLiteral("Nyan-Q"));
    setWindowIcon(Theme::instance().appIcon());

    buildUi();
    wire();
    setupShortcuts();
    setupTray();
    setupResizeHandles();

    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::applyTheme);
    restoreWindowGeometry();
    Theme::instance().setThemeMode(m_controller->settings().theme);
    applyTheme();
    updateWindowStatus();
    updateTrayState();
    updateTrayTooltip();
}

MainWindow::~MainWindow()
{
    saveWindowGeometry();
}

void MainWindow::buildUi()
{
    auto* root = new RootBackdrop(this);
    setCentralWidget(root);

    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_titleBar = new TitleBar(root);
    rootLayout->addWidget(m_titleBar);

    m_bodyStack = new QStackedWidget(root);
    m_bodyStack->setAttribute(Qt::WA_StyledBackground, false);
    rootLayout->addWidget(m_bodyStack, 1);

    auto* body = new QWidget(m_bodyStack);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_sidebar = new Sidebar(body);
    bodyLayout->addWidget(m_sidebar);

    auto* content = new ContentBackdrop(body);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_stack = new QStackedWidget(content);
    m_stack->setAttribute(Qt::WA_StyledBackground, false);
    contentLayout->addWidget(m_stack);
    bodyLayout->addWidget(content, 1);
    m_bodyStack->addWidget(body);

    m_dashboard = new DashboardPage(m_controller, m_stack);
    m_proxies = new ProxiesPage(m_controller, m_stack);
    m_profiles = new ProfilesPage(m_controller, m_stack);
    m_connections = new ConnectionsPage(m_controller, m_stack);
    m_rules = new RulesPage(m_controller, m_stack);
    m_logs = new LogsPage(m_controller, m_stack);
    m_settings = new SettingsPage(m_controller, m_stack);

    m_stack->addWidget(m_dashboard);
    m_stack->addWidget(m_proxies);
    m_stack->addWidget(m_profiles);
    m_stack->addWidget(m_connections);
    m_stack->addWidget(m_rules);
    m_stack->addWidget(m_logs);
    m_stack->addWidget(m_settings);
    m_stack->setCurrentIndex(0);

    m_snackbar = new M3Snackbar(this);

    connect(m_sidebar, &Sidebar::pageSelected, this, &MainWindow::setCurrentPage);
    connect(m_dashboard, &DashboardPage::navigateRequested, this, &MainWindow::setCurrentPage);
    connect(m_proxies, &ProxiesPage::navigateRequested, this, &MainWindow::setCurrentPage);
    connect(m_profiles, &ProfilesPage::editConfigRequested, this,
            &MainWindow::openConfigEditor);
    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &MainWindow::toggleMaximize);
    connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);
}

void MainWindow::wire()
{
    connect(m_controller, &ClashController::stateChanged, this, [this](cq::CoreState) {
        updateWindowStatus();
        updateTrayState();
        updateTrayTooltip();
    });
    connect(m_controller, &ClashController::statsChanged, this,
            [this](const cq::SessionStats& stats) {
                m_sidebar->setStats(stats);
                updateTrayTooltip();
            });
    connect(m_controller, &ClashController::activeProfileChanged, this, [this] {
        m_sidebar->setActiveProfileName(m_controller->activeProfileName());
        updateWindowStatus();
    });
    connect(m_controller, &ClashController::coreVersionChanged, this,
            [this](const QString&) {
                updateWindowStatus();
                updateTrayState();
            });
    connect(m_controller, &ClashController::modeChanged, this, [this](const QString&) {
        updateTrayState();
    });
    connect(m_controller, &ClashController::systemProxyChanged, this,
            [this](bool) { updateTrayState(); });
    connect(m_controller, &ClashController::tunChanged, this,
            [this](bool) { updateTrayState(); });
    connect(m_controller, &ClashController::tunElevationRequired, this,
            &MainWindow::requestTunElevation, Qt::QueuedConnection);
    connect(m_controller, &ClashController::settingsChanged, this, [this] {
        if (Theme::instance().themeMode() != m_controller->settings().theme)
            Theme::instance().setThemeMode(m_controller->settings().theme);
        updateWindowStatus();
        updateTrayState();
    });
    connect(m_controller, &ClashController::notification, this,
            [this](const QString& message, bool error) {
                m_snackbar->showMessage(message, error);
                if (error && !isVisible() && m_tray)
                    m_tray->showMessage(QStringLiteral("Nyan-Q"), message.left(180),
                                        QSystemTrayIcon::Warning, 3500);
            });
}

void MainWindow::setCurrentPage(int index)
{
    if (m_editorView)
        return;
    if (index < 0 || index >= m_stack->count())
        return;
    m_stack->setCurrentIndex(index);
    m_sidebar->setCurrentPage(index);
}

int MainWindow::currentPage() const
{
    return m_stack ? m_stack->currentIndex() : 0;
}

void MainWindow::openConfigEditor(const QString& profileId)
{
    if (!m_controller || !m_controller->profileStore() || !m_bodyStack)
        return;
    const auto profile = m_controller->profileStore()->find(profileId);
    if (!profile)
        return;

    if (m_editorView) {
        if (m_editorView->profileId() == profileId)
            return;
        if (!ensureEditorClosable())
            return;
        closeConfigEditor();
    }

    m_editorReturnPage = m_stack->currentIndex();
    m_editorView = new ConfigEditorView(m_controller, *profile, m_bodyStack);
    connect(m_editorView, &ConfigEditorView::closeRequested, this,
            &MainWindow::closeConfigEditor);
    connect(m_editorView, &ConfigEditorView::saved, this, [this] {
        if (m_profiles)
            m_profiles->refresh();
    });
    connect(m_editorView, &ConfigEditorView::profileChanged, this, [this] {
        if (m_profiles)
            m_profiles->refresh();
        m_sidebar->setActiveProfileName(m_controller->activeProfileName());
        updateWindowStatus();
    });
    m_bodyStack->addWidget(m_editorView);
    m_bodyStack->setCurrentWidget(m_editorView);
}

void MainWindow::closeConfigEditor()
{
    if (!m_editorView)
        return;
    m_bodyStack->setCurrentIndex(0);
    m_editorView->deleteLater();
    m_editorView = nullptr;
    m_stack->setCurrentIndex(m_editorReturnPage);
    m_sidebar->setCurrentPage(m_editorReturnPage);
}

bool MainWindow::ensureEditorClosable()
{
    return !m_editorView || m_editorView->confirmClose();
}

void MainWindow::setupShortcuts()
{
    for (int i = 0; i < 7; ++i) {
        auto* shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this);
        connect(shortcut, &QShortcut::activated, this, [this, i] { setCurrentPage(i); });
    }
    auto* settingsShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this);
    connect(settingsShortcut, &QShortcut::activated, this, [this] { setCurrentPage(6); });

    auto* quitShortcut = new QShortcut(QKeySequence::Quit, this);
    connect(quitShortcut, &QShortcut::activated, this, [this] {
        m_forceQuit = true;
        close();
    });
}

void MainWindow::setupTray()
{
    if (m_controller->isDemo() || !QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(Theme::instance().trayIcon(QStringLiteral("default")), this);
    m_tray->setToolTip(QStringLiteral("Nyan-Q"));
    m_trayMenu = new QMenu(this);

    auto* showAction = m_trayMenu->addAction(QStringLiteral("打开 Nyan-Q"));
    m_trayMenu->addSeparator();

    auto* modeMenu = m_trayMenu->addMenu(QStringLiteral("代理模式"));
    m_trayModeGroup = new QActionGroup(this);
    m_trayModeGroup->setExclusive(true);
    m_trayRuleMode = modeMenu->addAction(QStringLiteral("规则模式"));
    m_trayGlobalMode = modeMenu->addAction(QStringLiteral("全局模式"));
    m_trayDirectMode = modeMenu->addAction(QStringLiteral("直连模式"));
    for (QAction* action : {m_trayRuleMode, m_trayGlobalMode, m_trayDirectMode}) {
        action->setCheckable(true);
        m_trayModeGroup->addAction(action);
    }
    m_trayMenu->addSeparator();

    m_traySystemProxy = m_trayMenu->addAction(QStringLiteral("系统代理"));
    m_traySystemProxy->setCheckable(true);
    m_trayTun = m_trayMenu->addAction(QStringLiteral("TUN 模式"));
    m_trayTun->setCheckable(true);

    m_trayMenu->addSeparator();
    auto* quitAction = m_trayMenu->addAction(QStringLiteral("退出 Nyan-Q"));
    m_tray->setContextMenu(m_trayMenu);

    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    connect(m_trayRuleMode, &QAction::triggered, this,
            [this] { m_controller->setMode(QStringLiteral("rule")); });
    connect(m_trayGlobalMode, &QAction::triggered, this,
            [this] { m_controller->setMode(QStringLiteral("global")); });
    connect(m_trayDirectMode, &QAction::triggered, this,
            [this] { m_controller->setMode(QStringLiteral("direct")); });
    connect(m_traySystemProxy, &QAction::triggered, this, [this](bool checked) {
        m_controller->setSystemProxy(checked);
    });
    connect(m_trayTun, &QAction::triggered, this, [this](bool checked) {
        m_controller->setTunEnabled(checked);
    });
    connect(quitAction, &QAction::triggered, this, [this] {
        m_forceQuit = true;
        close();
    });
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    showFromTray();
            });
    m_tray->show();
}

bool MainWindow::hasTrayIcon() const
{
    return m_tray && m_tray->isVisible();
}

void MainWindow::showFromTray()
{
    if (!isVisible())
        show();
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
    m_snackbar->reposition();
}

void MainWindow::applyTheme()
{
    const Palette& p = pal();
    QPalette appPalette;
    appPalette.setColor(QPalette::Window, p.surface);
    appPalette.setColor(QPalette::WindowText, p.onSurface);
    appPalette.setColor(QPalette::Base, p.surfaceContainer);
    appPalette.setColor(QPalette::AlternateBase, p.surfaceContainerHigh);
    appPalette.setColor(QPalette::Text, p.onSurface);
    appPalette.setColor(QPalette::PlaceholderText, Theme::alpha(p.onSurfaceVariant, 170));
    appPalette.setColor(QPalette::Button, p.surfaceContainerHigh);
    appPalette.setColor(QPalette::ButtonText, p.onSurface);
    appPalette.setColor(QPalette::BrightText, p.error);
    appPalette.setColor(QPalette::Highlight, p.primary);
    appPalette.setColor(QPalette::HighlightedText, p.onPrimary);
    appPalette.setColor(QPalette::ToolTipBase, p.surfaceContainerHighest);
    appPalette.setColor(QPalette::ToolTipText, p.onSurface);
    appPalette.setColor(QPalette::Light, p.surfaceContainerHighest);
    appPalette.setColor(QPalette::Midlight, p.surfaceContainerHigh);
    appPalette.setColor(QPalette::Dark, p.outline);
    appPalette.setColor(QPalette::Mid, p.outlineVariant);
    appPalette.setColor(QPalette::Shadow, QColor(0, 0, 0, 150));
    appPalette.setColor(QPalette::Disabled, QPalette::WindowText, Theme::alpha(p.onSurface, 100));
    appPalette.setColor(QPalette::Disabled, QPalette::Text, Theme::alpha(p.onSurface, 100));
    appPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Theme::alpha(p.onSurface, 100));
    qApp->setPalette(appPalette);
    qApp->setStyleSheet(Theme::instance().styleSheet());

    if (m_sidebar)
        m_sidebar->setCoreState(m_controller->state());
    for (QWidget* widget : QApplication::allWidgets())
        widget->update();

#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL dark = p.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
    const int cornerPreference = 2;                        // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
#endif
}

void MainWindow::updateWindowStatus()
{
    if (!m_controller)
        return;
    const cq::CoreState state = m_controller->state();
    m_sidebar->setCoreState(state);
    m_sidebar->setCoreVersion(m_controller->coreVersion());
    m_sidebar->setActiveProfileName(m_controller->activeProfileName());
    m_sidebar->setStats(m_controller->stats());
    setWindowTitle(QStringLiteral("Nyan-Q · %1").arg(stateLabel(state)));
    updateTrayState();
}

void MainWindow::updateTrayState()
{
    if (!m_tray)
        return;
    if (m_traySystemProxy)
        m_traySystemProxy->setChecked(m_controller->systemProxyEnabled());
    if (m_trayTun)
        m_trayTun->setChecked(m_controller->tunEnabled());

    if (m_trayRuleMode)
        m_trayRuleMode->setChecked(m_controller->mode() == QStringLiteral("rule"));
    if (m_trayGlobalMode)
        m_trayGlobalMode->setChecked(m_controller->mode() == QStringLiteral("global"));
    if (m_trayDirectMode)
        m_trayDirectMode->setChecked(m_controller->mode() == QStringLiteral("direct"));
    QString trayName = QStringLiteral("default");
    if (m_controller->state() == cq::CoreState::Running) {
        const QString mode = m_controller->mode();
        if (m_controller->tunEnabled()) {
            trayName = QStringLiteral("tun");
        } else if (m_controller->systemProxyEnabled() &&
                   (mode == QStringLiteral("rule") || mode == QStringLiteral("global") ||
                    mode == QStringLiteral("direct"))) {
            trayName = mode;
        }
    }
    m_tray->setIcon(Theme::instance().trayIcon(trayName));
    updateTrayTooltip();
}

void MainWindow::updateTrayTooltip()
{
    if (!m_tray)
        return;
    const cq::SessionStats stats = m_controller->stats();
    QString tooltip = QStringLiteral("Nyan-Q · %1").arg(stateLabel(m_controller->state()));
    if (m_controller->state() == cq::CoreState::Running) {
        tooltip += QStringLiteral("\n%1  ·  下载 %2  上传 %3")
                       .arg(m_controller->activeProfileName(),
                            Formatters::rate(stats.downloadSpeed),
                            Formatters::rate(stats.uploadSpeed));
    }
    m_tray->setToolTip(tooltip);
}

void MainWindow::requestTunElevation()
{
    if (m_elevationPending)
        return;
    if (!ensureEditorClosable())
        return;

    m_elevationPending = true;

    QStringList arguments{QStringLiteral("--relaunch-elevated"),
                          QStringLiteral("--enable-tun"),
                          QStringLiteral("--page"),
                          QString::number(currentPage()),
                          QStringLiteral("--data-dir"),
                          QDir::toNativeSeparators(AppPaths::dataDir())};
    if (!isVisible() || isMinimized())
        arguments.append(QStringLiteral("--start-hidden"));

    QString error;
    const Elevation::RelaunchResult result =
        Elevation::relaunchAsAdministrator(arguments, &error);
    if (result == Elevation::RelaunchResult::Started) {
        // The elevated instance waits for this instance's single-instance
        // lock, then starts the core with TUN enabled.
        m_forceQuit = true;
        close();
        return;
    }

    m_elevationPending = false;
    if (result == Elevation::RelaunchResult::Canceled) {
        reportTunElevationIssue(QStringLiteral("已取消管理员授权，TUN 模式未开启。"),
                                false);
    } else {
        reportTunElevationIssue(
            error.isEmpty() ? QStringLiteral("无法以管理员身份重新启动 Nyan-Q。")
                            : error,
            true);
    }
}

void MainWindow::reportTunElevationIssue(const QString& message, bool warning)
{
    if (m_tray && m_tray->isVisible() && !isVisible()) {
        m_tray->showMessage(QStringLiteral("Nyan-Q"), message,
                            warning ? QSystemTrayIcon::Warning
                                    : QSystemTrayIcon::Information,
                            4000);
        return;
    }
    if (warning)
        QMessageBox::warning(this, QStringLiteral("TUN 模式"), message);
    else
        QMessageBox::information(this, QStringLiteral("TUN 模式"), message);
}

void MainWindow::toggleMaximize()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_forceQuit && !m_controller->isDemo() &&
        m_controller->settings().closeToTray && hasTrayIcon()) {
        event->ignore();
        hide();
        if (!m_trayHintShown) {
            m_tray->showMessage(QStringLiteral("Nyan-Q"),
                                QStringLiteral("主窗口已隐藏，Nyan-Q 会继续在后台保护网络。"),
                                QSystemTrayIcon::Information, 3000);
            m_trayHintShown = true;
        }
        return;
    }

    if (!ensureEditorClosable()) {
        event->ignore();
        return;
    }

    saveWindowGeometry();
    event->accept();
    if (m_tray)
        m_tray->hide();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        m_titleBar->setMaximized(isMaximized());
        layoutResizeHandles();
        if (isVisible())
            m_snackbar->reposition();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    layoutResizeHandles();
    if (m_snackbar)
        m_snackbar->reposition();
}

void MainWindow::setupResizeHandles()
{
    const Qt::Edges edges[8] = {
        Qt::LeftEdge,
        Qt::RightEdge,
        Qt::TopEdge,
        Qt::BottomEdge,
        Qt::LeftEdge | Qt::TopEdge,
        Qt::RightEdge | Qt::TopEdge,
        Qt::LeftEdge | Qt::BottomEdge,
        Qt::RightEdge | Qt::BottomEdge,
    };
    for (int i = 0; i < 8; ++i) {
        m_resizeHandles[i] = new ResizeHandle(edges[i], this);
        m_resizeHandles[i]->show();
        m_resizeHandles[i]->raise();
    }
    layoutResizeHandles();
}

void MainWindow::layoutResizeHandles()
{
    if (m_resizeHandles[0] == nullptr)
        return;
    const bool hidden = isMaximized() || isFullScreen() || !isVisible();
    const int border = 7;
    const int w = width();
    const int h = height();
    const int corner = 12;
    const QRect geometries[8] = {
        QRect(0, corner, border, qMax(0, h - 2 * corner)),
        QRect(w - border, corner, border, qMax(0, h - 2 * corner)),
        QRect(corner, 0, qMax(0, w - 2 * corner), border),
        QRect(corner, h - border, qMax(0, w - 2 * corner), border),
        QRect(0, 0, corner, corner),
        QRect(w - corner, 0, corner, corner),
        QRect(0, h - corner, corner, corner),
        QRect(w - corner, h - corner, corner, corner),
    };
    for (int i = 0; i < 8; ++i) {
        m_resizeHandles[i]->setGeometry(geometries[i]);
        m_resizeHandles[i]->setVisible(!hidden);
        if (!hidden)
            m_resizeHandles[i]->raise();
    }
}

void MainWindow::enableResizeFrame()
{
#ifdef Q_OS_WIN
    if (m_resizeFrameEnabled)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if ((style & WS_THICKFRAME) == 0) {
        style |= WS_THICKFRAME;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
    }
    m_resizeFrameEnabled = true;
#endif
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    enableResizeFrame();
    layoutResizeHandles();
    if (!m_centered)
        m_centered = true;
    if (m_snackbar)
        m_snackbar->raise();
    if (!m_themeRefreshedOnShow) {
        m_themeRefreshedOnShow = true;
        // The frameless title bar can be polished before the first theme
        // switch during construction; refresh once it is actually visible.
        QTimer::singleShot(0, this, [this] {
            applyTheme();
            if (m_titleBar)
                m_titleBar->update();
        });
    }
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
            RECT windowRect;
            if (GetWindowRect(msg->hwnd, &windowRect)) {
                const qreal dpr = windowHandle() ? windowHandle()->devicePixelRatio() : 1.0;
                const int border = qMax(4, qRound(6 * dpr));
                const LONG x = GET_X_LPARAM(msg->lParam);
                const LONG y = GET_Y_LPARAM(msg->lParam);
                const bool left = x >= windowRect.left && x < windowRect.left + border;
                const bool right = x < windowRect.right && x >= windowRect.right - border;
                const bool top = y >= windowRect.top && y < windowRect.top + border;
                const bool bottom = y < windowRect.bottom && y >= windowRect.bottom - border;

                int hit = 0;
                if (top && left)
                    hit = HTTOPLEFT;
                else if (top && right)
                    hit = HTTOPRIGHT;
                else if (bottom && left)
                    hit = HTBOTTOMLEFT;
                else if (bottom && right)
                    hit = HTBOTTOMRIGHT;
                else if (left)
                    hit = HTLEFT;
                else if (right)
                    hit = HTRIGHT;
                else if (top)
                    hit = HTTOP;
                else if (bottom)
                    hit = HTBOTTOM;
                if (hit != 0) {
                    *result = hit;
                    return true;
                }
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

void MainWindow::restoreWindowGeometry()
{
    if (m_controller->isDemo())
        return;
    QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    bool restored = false;
    if (!geometry.isEmpty()) {
        restored = restoreGeometry(geometry);
        if (restored) {
            bool visible = false;
            for (QScreen* screen : QApplication::screens()) {
                if (screen->availableGeometry().intersects(frameGeometry())) {
                    visible = true;
                    break;
                }
            }
            if (!visible)
                restored = false;
        }
    }

    if (!restored) {
        QScreen* screen = QApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            const QSize target(qMin(1360, available.width() - 60),
                               qMin(860, available.height() - 80));
            resize(target);
            move(available.center() - QPoint(target.width() / 2, target.height() / 2));
        }
    }
    m_centered = true;
}

void MainWindow::saveWindowGeometry()
{
    if (m_controller->isDemo())
        return;
    QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.sync();
}
