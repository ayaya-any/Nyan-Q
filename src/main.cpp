#include "app/AppPaths.h"
#include "app/Fonts.h"
#include "app/MenuStyler.h"
#include "app/RoundedPopupFilter.h"
#include "app/ScrollBarStyler.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "ui/MainWindow.h"
#include "ui/editor/CodeEditor.h"
#include "ui/pages/ProfilesPage.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHostAddress>
#include <QLocalServer>
#include <QPainter>
#include <QLocalSocket>
#include <QLockFile>
#include <QSettings>
#include <QStyleFactory>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace {
constexpr int kPageDashboard = 0;
constexpr int kPageProxies = 1;
constexpr int kPageProfiles = 2;
constexpr int kPageConnections = 3;
constexpr int kPageRules = 4;
constexpr int kPageLogs = 5;
constexpr int kPageSettings = 6;

int parsePage(const QString& raw)
{
    bool ok = false;
    const int number = raw.toInt(&ok);
    if (ok && number >= 0 && number <= 6)
        return number;
    const QString lower = raw.trimmed().toLower();
    if (lower == QStringLiteral("dashboard") || lower == QStringLiteral("home"))
        return kPageDashboard;
    if (lower == QStringLiteral("proxies") || lower == QStringLiteral("proxy"))
        return kPageProxies;
    if (lower == QStringLiteral("profiles") || lower == QStringLiteral("profile"))
        return kPageProfiles;
    if (lower == QStringLiteral("connections") || lower == QStringLiteral("connection"))
        return kPageConnections;
    if (lower == QStringLiteral("rules") || lower == QStringLiteral("rule"))
        return kPageRules;
    if (lower == QStringLiteral("logs") || lower == QStringLiteral("log"))
        return kPageLogs;
    if (lower == QStringLiteral("settings") || lower == QStringLiteral("setting"))
        return kPageSettings;
    return -1;
}

QString singleInstanceName()
{
    const QByteArray hash = QCryptographicHash::hash(AppPaths::dataDir().toUtf8(),
                                                     QCryptographicHash::Sha1)
                                .toHex()
                                .left(16);
    return QStringLiteral("Nyan-Q-") + QString::fromLatin1(hash);
}

bool notifyExistingInstance(const QString& name)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(350))
        return false;
    socket.write("show");
    socket.flush();
    socket.waitForBytesWritten(250);
    return true;
}

int findFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}
}  // namespace

int main(int argc, char* argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Nyan-Q"));
    QApplication::setApplicationDisplayName(QStringLiteral("Nyan-Q"));
    QApplication::setApplicationVersion(QStringLiteral(NYANQ_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Nyan-Q"));
    QApplication::setOrganizationDomain(QStringLiteral("nyan-q.local"));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    auto* popupShapeFilter = new RoundedPopupFilter(&app);
    app.installEventFilter(popupShapeFilter);
    ScrollBarStyler::install();
    MenuStyler::install();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Nyan-Q · Material 风格的 Mihomo 桌面客户端"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption demoOption(QStringLiteral("demo"),
                                  QStringLiteral("启用内置演示数据，不启动真实内核。"));
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("以演示模式启动，截图到指定 PNG 文件后退出。"), QStringLiteral("path"));
    QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("启动时显示的页面：dashboard/proxies/profiles/connections/rules/logs/settings 或 0-6。"),
        QStringLiteral("page"));
    QCommandLineOption autostartOption(QStringLiteral("autostart"),
                                       QStringLiteral("由系统启动项调用（保留参数）。"));
    QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("启动真实内核，验证 API / 配置生成后自动退出（开发用途）。"));
    QCommandLineOption themeOption(
        QStringLiteral("theme"),
        QStringLiteral("演示/截图模式的主题：dark、light 或 system。"), QStringLiteral("theme"));
    QCommandLineOption editorScreenshotOption(
        QStringLiteral("screenshot-editor"),
        QStringLiteral("以演示模式打开当前配置编辑器并截图到 PNG 后退出（开发用途）。"),
        QStringLiteral("path"));
    QCommandLineOption enableTunOption(
        QStringLiteral("enable-tun"),
        QStringLiteral("内部使用：提权重启后自动开启 TUN。"));
    enableTunOption.setFlags(QCommandLineOption::HiddenFromHelp);
    QCommandLineOption relaunchElevatedOption(
        QStringLiteral("relaunch-elevated"),
        QStringLiteral("内部使用：由 UAC 提权重启的实例。"));
    relaunchElevatedOption.setFlags(QCommandLineOption::HiddenFromHelp);
    QCommandLineOption startHiddenOption(
        QStringLiteral("start-hidden"),
        QStringLiteral("内部使用：启动后隐藏主窗口。"));
    startHiddenOption.setFlags(QCommandLineOption::HiddenFromHelp);
    QCommandLineOption dataDirOption(
        QStringLiteral("data-dir"),
        QStringLiteral("内部使用：提权重启时沿用原数据目录。"), QStringLiteral("path"));
    dataDirOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(demoOption);
    parser.addOption(screenshotOption);
    parser.addOption(editorScreenshotOption);
    parser.addOption(pageOption);
    parser.addOption(autostartOption);
    parser.addOption(smokeOption);
    parser.addOption(themeOption);
    parser.addOption(enableTunOption);
    parser.addOption(relaunchElevatedOption);
    parser.addOption(startHiddenOption);
    parser.addOption(dataDirOption);
    parser.process(app);

    const bool screenshot = parser.isSet(screenshotOption);
    const bool editorScreenshot = parser.isSet(editorScreenshotOption);
    const bool smokeTest = parser.isSet(smokeOption);
    const bool demo = parser.isSet(demoOption) || screenshot || editorScreenshot;
    const bool enableTun = parser.isSet(enableTunOption);
    const bool relaunchElevated = parser.isSet(relaunchElevatedOption);
    const bool startHiddenRequested = parser.isSet(startHiddenOption);
    const QString screenshotPath = parser.value(screenshotOption);
    const QString editorScreenshotPath = parser.value(editorScreenshotOption);
    Q_UNUSED(autostartOption)

    if (!demo && !smokeTest && parser.isSet(dataDirOption)) {
        const QString dataDir = parser.value(dataDirOption).trimmed();
        if (!dataDir.isEmpty())
            AppPaths::setDataDirOverride(dataDir);
    }

    std::unique_ptr<QTemporaryDir> transientDir;
    if (demo) {
        const QString pattern = QDir::tempPath() + QStringLiteral("/Nyan-Q-preview-XXXXXX");
        transientDir = std::make_unique<QTemporaryDir>(pattern);
        if (transientDir->isValid())
            AppPaths::setDataDirOverride(transientDir->path());
        AppPaths::ensureDirectories();
    } else if (smokeTest) {
        transientDir = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + QStringLiteral("/Nyan-Q-smoke-XXXXXX"));
        if (transientDir->isValid())
            AppPaths::setDataDirOverride(transientDir->path());
        AppPaths::ensureDirectories();
        int mixedPort = findFreePort();
        int controllerPort = findFreePort();
        if (mixedPort == controllerPort)
            controllerPort = findFreePort();
        if (mixedPort <= 0)
            mixedPort = 17890;
        if (controllerPort <= 0 || controllerPort == mixedPort)
            controllerPort = 19090;
        QSettings ini(AppPaths::settingsFile(), QSettings::IniFormat);
        ini.setValue(QStringLiteral("network/mixedPort"), mixedPort);
        ini.setValue(QStringLiteral("network/controllerPort"), controllerPort);
        ini.setValue(QStringLiteral("network/secret"), QStringLiteral("nyanq-smoke-secret"));
        ini.setValue(QStringLiteral("core/logLevel"), QStringLiteral("info"));
        ini.setValue(QStringLiteral("behavior/autoLaunch"), false);
        ini.setValue(QStringLiteral("behavior/closeToTray"), false);
        ini.sync();
    } else {
        AppPaths::ensureDirectories();
    }

    if (demo && parser.isSet(themeOption)) {
        const QString theme = parser.value(themeOption).toLower();
        if (theme == QStringLiteral("dark") || theme == QStringLiteral("light") ||
            theme == QStringLiteral("system")) {
            QSettings ini(AppPaths::settingsFile(), QSettings::IniFormat);
            ini.setValue(QStringLiteral("appearance/theme"), theme);
            ini.sync();
        }
    }

    QLockFile* instanceLock = nullptr;
    QString instanceName;
    if (!demo && !screenshot && !smokeTest) {
        instanceName = singleInstanceName();
        auto* lock = new QLockFile(AppPaths::dataDir() + QStringLiteral("/instance.lock"));
        lock->setStaleLockTime(30000);
        const int lockTimeout = relaunchElevated ? 15000 : 350;
        if (!lock->tryLock(lockTimeout)) {
            notifyExistingInstance(instanceName);
            delete lock;
            return 0;
        }
        instanceLock = lock;
    }

    Fonts::load();
    QFont appFont = Fonts::text(14);
    QApplication::setFont(appFont);
    QApplication::setWindowIcon(Theme::instance().appIcon());

    ClashController controller;
    controller.initialize(demo, smokeTest);

    if (enableTun && !demo && !smokeTest) {
        if (controller.isElevated()) {
            if (!controller.tunEnabled())
                controller.setTunEnabled(true);
        } else {
            QTextStream(stderr)
                << "TUN was requested but the process is not elevated; ignoring.\n";
        }
    }

    if (smokeTest) {
        QTextStream out(stdout);
        auto dumpLogs = [&] {
            out << "---- controller logs ----" << Qt::endl;
            for (const cq::LogEntry& entry : controller.logs())
                out << entry.time.toString(Qt::ISODate) << " " << static_cast<int>(entry.level)
                    << " " << entry.message << Qt::endl;
            out.flush();
        };

        bool apiError = false;
        bool hotReloadSaveOk = false;
        bool settingsHotApplyOk = false;
        QObject::connect(&controller, &ClashController::notification, &app,
                         [&](const QString& message, bool error) {
                             if (error)
                                 apiError = true;
                             out << (error ? "SMOKE_NOTICE_ERROR " : "SMOKE_NOTICE ")
                                 << message << Qt::endl;
                             out.flush();
                         });

        auto* watchdog = new QTimer(&app);
        watchdog->setSingleShot(true);
        watchdog->setInterval(32000);
        QObject::connect(watchdog, &QTimer::timeout, &app, [&] {
            out << "SMOKE_FAIL timeout state=" << static_cast<int>(controller.state())
                << " error=" << controller.lastError() << Qt::endl;
            dumpLogs();
            controller.stop();
            app.exit(1);
        });
        watchdog->start();

        bool reported = false;
        QObject::connect(&controller, &ClashController::stateChanged, &app,
                         [&](cq::CoreState state) {
                             if (reported)
                                 return;
                             if (state == cq::CoreState::Running) {
                                 reported = true;
                                 watchdog->stop();
                                 const qint64 pidBeforeReload = controller.coreProcessId();
                                 QTimer::singleShot(500, &app, [&] {
                                     const auto groups = controller.proxyGroups();
                                     if (!groups.isEmpty()) {
                                         const auto& group = groups.first();
                                         QString node = group.nodes.isEmpty()
                                                            ? QString()
                                                            : group.nodes.first().name;
                                         for (const auto& candidate : group.nodes) {
                                             if (candidate.name == QStringLiteral("DIRECT")) {
                                                 node = candidate.name;
                                                 break;
                                             }
                                         }
                                         if (!node.isEmpty())
                                             controller.selectProxy(group.name, node);
                                     }
                                     controller.setMode(QStringLiteral("global"));
                                 });
                                 QTimer::singleShot(1600, &app, [&] {
                                     const auto active = controller.profileStore()->activeProfile();
                                     if (!active) {
                                         out << "SMOKE_HOT_RELOAD no active profile" << Qt::endl;
                                         return;
                                     }
                                     QByteArray content =
                                         controller.profileStore()->configContent(active->id);
                                     content.replace("  - MATCH,节点选择",
                                                     "  - DOMAIN-SUFFIX,smoke.invalid,DIRECT\n"
                                                     "  - MATCH,节点选择");
                                     QString error;
                                     hotReloadSaveOk =
                                         controller.profileStore()->saveConfigContent(
                                             active->id, content, &error);
                                     out << "SMOKE_HOT_RELOAD save=" << hotReloadSaveOk
                                         << " pid=" << controller.coreProcessId()
                                         << " error=" << error << Qt::endl;
                                     out.flush();
                                 });
                                 QTimer::singleShot(2600, &app, [&] {
                                     int port = findFreePort();
                                     if (port <= 0)
                                         port = controller.settings().mixedPort + 1;
                                     cq::AppSettings next = controller.settings();
                                     next.mixedPort = port;
                                     settingsHotApplyOk = controller.updateSettings(next, true);
                                     out << "SMOKE_HOT_SETTINGS mixedPort=" << port
                                         << " applied=" << settingsHotApplyOk
                                         << " pid=" << controller.coreProcessId() << Qt::endl;
                                     out.flush();
                                 });
                                 QTimer::singleShot(4000, &app, [&, pidBeforeReload] {
                                     const auto groups = controller.proxyGroups();
                                     const auto rules = controller.rules();
                                     const qint64 pidAfterReload = controller.coreProcessId();
                                     const bool processStable = pidBeforeReload > 0 &&
                                                                pidAfterReload == pidBeforeReload;
                                     const bool ruleApplied =
                                         std::any_of(rules.cbegin(), rules.cend(),
                                                     [](const cq::RuleInfo& rule) {
                                                         return rule.payload ==
                                                                QStringLiteral("smoke.invalid");
                                                     });
                                     const bool ok = !controller.coreVersion().isEmpty() &&
                                                     !groups.isEmpty() && !rules.isEmpty() &&
                                                     !apiError && processStable &&
                                                     hotReloadSaveOk && settingsHotApplyOk &&
                                                     ruleApplied;
                                     out << (ok ? "SMOKE_OK" : "SMOKE_FAIL")
                                         << " version=" << controller.coreVersion()
                                         << " groups=" << groups.size()
                                         << " rules=" << rules.size()
                                         << " connections=" << controller.connections().size()
                                         << " mode=" << controller.mode()
                                         << " apiError=" << apiError
                                         << " pidBefore=" << pidBeforeReload
                                         << " pidAfter=" << pidAfterReload
                                         << " processStable=" << processStable
                                         << " settingsHotApply=" << settingsHotApplyOk
                                         << " ruleApplied=" << ruleApplied << Qt::endl;
                                     for (const auto& group : groups)
                                         out << "GROUP " << group.name << " nodes="
                                             << group.nodes.size() << " now=" << group.now
                                             << Qt::endl;
                                     const cq::MemoryUsage memory = controller.memoryUsage();
                                     out << "SMOKE_MEMORY app=" << memory.appWorkingSet
                                         << " kernel=" << memory.kernelWorkingSet
                                         << " appValid=" << memory.appValid
                                         << " kernelValid=" << memory.kernelValid << Qt::endl;
                                     out.flush();
                                     controller.stop();
                                     QTimer::singleShot(1600, &app, [&, ok] {
                                         bool shutdownLogged = false;
                                         bool forced = false;
                                         for (const cq::LogEntry& entry : controller.logs()) {
                                             if (entry.message.contains(
                                                     QStringLiteral("Mihomo shutting down")))
                                                 shutdownLogged = true;
                                             if (entry.message.contains(
                                                     QStringLiteral("强制终止")) ||
                                                 entry.message.contains(
                                                     QStringLiteral("优雅退出超时")))
                                                 forced = true;
                                         }
                                         const bool gracefulShutdown = shutdownLogged && !forced;
                                         out << (gracefulShutdown ? "SMOKE_SHUTDOWN_OK"
                                                                  : "SMOKE_SHUTDOWN_FAIL")
                                             << " gracefulLog=" << shutdownLogged
                                             << " forced=" << forced << Qt::endl;
                                         out.flush();
                                         dumpLogs();
                                         app.exit(ok && gracefulShutdown ? 0 : 2);
                                     });
                                 });
                             } else if (state == cq::CoreState::Failed) {
                                 reported = true;
                                 watchdog->stop();
                                 out << "SMOKE_FAIL state=failed error="
                                     << controller.lastError() << Qt::endl;
                                 dumpLogs();
                                 app.exit(1);
                             }
                         });
        controller.start();
        return app.exec();
    }

    MainWindow window(&controller);
    const int requestedPage = parser.isSet(pageOption) ? parsePage(parser.value(pageOption)) : -1;
    window.setCurrentPage(requestedPage >= 0 ? requestedPage : kPageDashboard);

    QLocalServer localServer;
    if (instanceLock) {
        QLocalServer::removeServer(instanceName);
        localServer.listen(instanceName);
        QObject::connect(&localServer, &QLocalServer::newConnection, &window, [&window, &localServer] {
            while (QLocalSocket* socket = localServer.nextPendingConnection()) {
                socket->disconnectFromServer();
                socket->deleteLater();
            }
            window.showFromTray();
        });
    }

    if (editorScreenshot) {
        window.setCurrentPage(kPageProfiles);
        window.show();
        QTimer::singleShot(500, &window, [&] {
            auto* profiles = window.findChild<ProfilesPage*>();
            if (!profiles) {
                QTextStream(stdout) << "SCREENSHOT_FAILED profiles page not found\n";
                QCoreApplication::exit(2);
                return;
            }
            profiles->openActiveConfigEditor();
            QTimer::singleShot(1800, &app, [&] {
                auto saveCapture = [&] {
                    const QPixmap capture = window.grab();
                    const QFileInfo info(editorScreenshotPath);
                    if (!info.absolutePath().isEmpty())
                        QDir().mkpath(info.absolutePath());
                    const bool ok = !capture.isNull() &&
                                    capture.save(editorScreenshotPath, "PNG");
                    QTextStream out(stdout);
                    out << (ok ? "SCREENSHOT_OK " : "SCREENSHOT_FAILED ")
                        << editorScreenshotPath << Qt::endl;
                    QTimer::singleShot(0, &app, [&app, ok] {
                        QCoreApplication::exit(ok ? 0 : 2);
                    });
                };
                auto* editor = window.findChild<CodeEditor*>();
                if (!editor) {
                    saveCapture();
                    return;
                }
                QString text = editor->toPlainText();
                text.replace(QStringLiteral("log-level: info"),
                             QStringLiteral("log-level: debug"));
                editor->setPlainText(text);
                editor->moveCursor(QTextCursor::Start);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

                saveCapture();
            });
        });
        return app.exec();
    }

    if (screenshot) {
        window.show();
        const int delay = demo ? 1500 : 900;
        QTimer::singleShot(delay, &app, [&window, screenshotPath] {
            const QFileInfo info(screenshotPath);
            if (!info.absolutePath().isEmpty())
                QDir().mkpath(info.absolutePath());
            const bool ok = window.grab().save(screenshotPath, "PNG");
            QTextStream out(stdout);
            out << (ok ? "SCREENSHOT_OK " : "SCREENSHOT_FAILED ") << screenshotPath << Qt::endl;
            QCoreApplication::exit(ok ? 0 : 2);
        });
        return app.exec();
    }

    const bool startHidden = !demo && window.hasTrayIcon() &&
                             (controller.settings().startMinimized ||
                              startHiddenRequested);
    if (!startHidden)
        window.show();

    if (!demo) {
        const bool needsStartupElevation =
            !smokeTest && !relaunchElevated && controller.tunEnabled() &&
            !controller.isElevated();
        if (needsStartupElevation) {
            // Ask for elevation before starting the core, so a canceled UAC
            // can fall through to a normal non-TUN start.
            QTimer::singleShot(200, &window, [&window, &controller] {
                if (window.promptForTunAtStartup())
                    controller.start();
            });
        } else {
            QTimer::singleShot(400, &controller, &ClashController::start);
        }
    }

    const int result = app.exec();
    if (instanceLock) {
        instanceLock->unlock();
        delete instanceLock;
    }
    return result;
}
