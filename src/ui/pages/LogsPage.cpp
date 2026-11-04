#include "ui/pages/LogsPage.h"

#include "app/AppPaths.h"
#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "ui/PageHeader.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

LogsPage::LogsPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void LogsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("日志"),
                              QStringLiteral("Mihomo 核心与 Nyan-Q 的运行记录。"), this);
    root->addWidget(m_header);

    auto* card = new M3Card(this, 20);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 14);
    cardLayout->setSpacing(10);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("运行日志"), card);
    title->setProperty("role", "cardTitle");
    toolbar->addWidget(title);
    m_levelFilter = new M3SegmentedControl(
        {QStringLiteral("全部"), QStringLiteral("信息"), QStringLiteral("警告"),
         QStringLiteral("错误")},
        card);
    m_levelFilter->setCompact(true);
    toolbar->addWidget(m_levelFilter);
    toolbar->addStretch();

    auto* autoLabel = new QLabel(QStringLiteral("自动滚动"), card);
    autoLabel->setProperty("role", "caption");
    toolbar->addWidget(autoLabel);
    m_autoScroll = new M3Switch(card);
    m_autoScroll->setChecked(true);
    toolbar->addWidget(m_autoScroll);

    m_countLabel = new QLabel(card);
    m_countLabel->setProperty("role", "micro");
    toolbar->addWidget(m_countLabel);

    auto* clear = new M3IconButton(QStringLiteral("delete_sweep"), card);
    clear->setDiameter(32);
    clear->setGlyphSize(17);
    clear->setToolTip(QStringLiteral("清空当前日志视图"));
    toolbar->addWidget(clear);

    auto* openFolder = new M3IconButton(QStringLiteral("folder_open"), card);
    openFolder->setDiameter(32);
    openFolder->setGlyphSize(17);
    openFolder->setToolTip(QStringLiteral("打开日志文件"));
    toolbar->addWidget(openFolder);
    cardLayout->addLayout(toolbar);

    auto* divider = new M3Divider(card);
    cardLayout->addWidget(divider);

    m_view = new QPlainTextEdit(card);
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(Fonts::mono(12));
    m_view->setPlaceholderText(QStringLiteral("日志将在这里实时显示…"));
    m_view->setMaximumBlockCount(4000);
    cardLayout->addWidget(m_view, 1);

    root->addWidget(card, 1);

    connect(m_levelFilter, &M3SegmentedControl::currentChanged, this,
            [this](int, const QString&) { rebuild(); });
    connect(clear, &QAbstractButton::clicked, this, [this] {
        m_entries.clear();
        m_view->clear();
        updateCount();
    });
    connect(openFolder, &QAbstractButton::clicked, this, [this] {
        const QString path = AppPaths::logFile();
        if (QFileInfo::exists(path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir()));
    });
}

void LogsPage::wire()
{
    connect(m_controller, &ClashController::logAdded, this, [this](const cq::LogEntry& entry) {
        m_entries.append(entry);
        if (m_entries.size() > 3000)
            m_entries.remove(0, m_entries.size() - 3000);
        if (accepts(entry.level))
            appendEntry(entry);
        updateCount();
    });
    connect(&Theme::instance(), &Theme::changed, this, &LogsPage::rebuild);
    connect(m_controller, &ClashController::stateChanged, this, [this](cq::CoreState state) {
        m_header->setSubtitle(
            state == cq::CoreState::Running
                ? QStringLiteral("Mihomo 核心正在运行 · 日志通过标准输出实时捕获")
                : QStringLiteral("Mihomo 核心与应用运行记录 · 最多保留 3000 条"));
    });
}

void LogsPage::refresh()
{
    m_entries = m_controller->logs();
    rebuild();
}

bool LogsPage::accepts(cq::LogLevel level) const
{
    const int filter = m_levelFilter ? m_levelFilter->currentIndex() : 0;
    switch (filter) {
    case 1:
        return level >= cq::LogLevel::Info;
    case 2:
        return level >= cq::LogLevel::Warning;
    case 3:
        return level == cq::LogLevel::Error;
    default:
        return true;
    }
}

void LogsPage::appendEntry(const cq::LogEntry& entry)
{
    if (!m_view)
        return;
    const Palette& p = pal();
    QTextCursor cursor(m_view->document());
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat timeFormat;
    timeFormat.setForeground(Theme::alpha(p.onSurfaceVariant, 165));
    timeFormat.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    cursor.insertText(entry.time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  "),
                      timeFormat);

    QString tag;
    QColor color = p.onSurfaceVariant;
    switch (entry.level) {
    case cq::LogLevel::Debug:
        tag = QStringLiteral("DEBUG");
        color = Theme::alpha(p.onSurfaceVariant, 190);
        break;
    case cq::LogLevel::Info:
        tag = QStringLiteral("INFO ");
        color = p.primary;
        break;
    case cq::LogLevel::Warning:
        tag = QStringLiteral("WARN ");
        color = p.warning;
        break;
    case cq::LogLevel::Error:
        tag = QStringLiteral("ERROR");
        color = p.error;
        break;
    }
    QTextCharFormat levelFormat;
    levelFormat.setForeground(color);
    levelFormat.setFontWeight(QFont::DemiBold);
    cursor.insertText(tag + QStringLiteral("  "), levelFormat);

    QTextCharFormat messageFormat;
    messageFormat.setForeground(entry.level == cq::LogLevel::Error ? p.error : p.onSurface);
    cursor.insertText(entry.message + QLatin1Char('\n'), messageFormat);

    if (!m_bulkUpdating && m_autoScroll->isChecked()) {
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    }
    if (!m_bulkUpdating)
        updateCount();
}

void LogsPage::rebuild()
{
    if (!m_view)
        return;
    m_bulkUpdating = true;
    m_view->clear();
    for (const cq::LogEntry& entry : m_entries) {
        if (accepts(entry.level))
            appendEntry(entry);
    }
    m_bulkUpdating = false;
    if (m_autoScroll->isChecked())
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    updateCount();
}

void LogsPage::updateCount()
{
    int visible = 0;
    for (const cq::LogEntry& entry : m_entries) {
        if (accepts(entry.level))
            ++visible;
    }
    m_countLabel->setText(QStringLiteral("%1 / %2 条").arg(visible).arg(m_entries.size()));
}
