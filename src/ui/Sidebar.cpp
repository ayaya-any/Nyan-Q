#include "ui/Sidebar.h"

#include "app/AppPaths.h"
#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"
#include "ui/widgets/MaterialWidgets.h"

#include <QButtonGroup>
#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QResizeEvent>
#include <QVBoxLayout>

class SidebarNavItem : public QAbstractButton {
public:
    SidebarNavItem(const QString& glyph, const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent)
        , m_glyph(glyph)
    {
        setText(text);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedHeight(44);
        setMinimumWidth(176);
        connect(this, &QAbstractButton::toggled, this, [this] { update(); });
    }

    QSize sizeHint() const override { return QSize(196, 44); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const Palette& p = pal();

        const QRectF bounds = rect().adjusted(0, 1, 0, -1);
        QColor background = Qt::transparent;
        QColor foreground = p.onSurfaceVariant;
        if (isChecked()) {
            background = p.primaryContainer;
            foreground = p.onPrimaryContainer;
        } else if (m_hovered) {
            background = p.surfaceContainerHigh;
            foreground = p.onSurface;
        }
        if (!isEnabled())
            foreground.setAlpha(90);

        if (background.alpha() > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(background);
            painter.drawRoundedRect(bounds, 12, 12);
        }

        painter.setFont(Fonts::icon(19));
        painter.setPen(foreground);
        painter.drawText(QRectF(13, 0, 24, height()), Qt::AlignCenter, m_glyph);

        QFont font = Fonts::text(13, isChecked() ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        painter.setPen(foreground);
        painter.drawText(QRectF(47, 0, width() - 58, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text());

        if (hasFocus()) {
            painter.setPen(QPen(Theme::alpha(p.primary, 160), 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 11, 11);
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString m_glyph;
    bool m_hovered = false;
};

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(232);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 16);
    root->setSpacing(4);

    auto* section = new QLabel(QStringLiteral("工作区"), this);
    section->setProperty("role", "micro");
    section->setContentsMargins(10, 2, 0, 5);
    root->addWidget(section);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    const QList<QPair<QString, QString>> nav = {
        {QStringLiteral("dashboard"), QStringLiteral("仪表盘")},
        {QStringLiteral("swap_horiz"), QStringLiteral("代理")},
        {QStringLiteral("layers"), QStringLiteral("配置")},
        {QStringLiteral("device_hub"), QStringLiteral("连接")},
        {QStringLiteral("rule"), QStringLiteral("规则")},
        {QStringLiteral("terminal"), QStringLiteral("日志")},
    };
    for (int i = 0; i < nav.size(); ++i) {
        QAbstractButton* item = addNavItem(m_navGroup, i, nav.at(i).first, nav.at(i).second);
        root->addWidget(item);
    }

    root->addStretch(1);

    auto* settings = addNavItem(m_navGroup, 6, QStringLiteral("settings"), QStringLiteral("设置"));
    root->addWidget(settings);
    root->addSpacing(10);

    auto* divider = new M3Divider(this);
    root->addWidget(divider);
    root->addSpacing(10);

    m_profileCard = new M3Card(this, 16);
    m_profileCard->setCursor(Qt::PointingHandCursor);
    m_profileCard->setToolTip(QStringLiteral("管理订阅配置"));
    m_profileCard->setMinimumHeight(82);
    m_profileCard->installEventFilter(this);
    auto* cardLayout = new QVBoxLayout(m_profileCard);
    cardLayout->setContentsMargins(13, 11, 13, 11);
    cardLayout->setSpacing(5);

    auto* stateRow = new QHBoxLayout;
    stateRow->setContentsMargins(0, 0, 0, 0);
    stateRow->setSpacing(6);
    m_stateDot = new M3IconLabel(QStringLiteral("circle"), m_profileCard);
    m_stateDot->setFixedBox(9, 9);
    m_stateDot->setGlyphSize(8);
    m_stateDot->setColor(pal().onSurfaceVariant);
    stateRow->addWidget(m_stateDot, 0, Qt::AlignVCenter);
    m_stateLabel = new QLabel(QStringLiteral("当前配置"), m_profileCard);
    m_stateLabel->setProperty("role", "micro");
    stateRow->addWidget(m_stateLabel);
    stateRow->addStretch();
    cardLayout->addLayout(stateRow);

    m_profileName = new QLabel(QStringLiteral("载入中…"), m_profileCard);
    m_profileName->setProperty("role", "bodyStrong");
    m_profileName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_profileName->setToolTip(QStringLiteral("载入中…"));
    m_profileName->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cardLayout->addWidget(m_profileName);

    m_sessionLabel = new QLabel(QStringLiteral("尚未连接"), m_profileCard);
    m_sessionLabel->setProperty("role", "micro");
    m_sessionLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cardLayout->addWidget(m_sessionLabel);

    for (QWidget* child : m_profileCard->findChildren<QWidget*>())
        child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    root->addWidget(m_profileCard);

    m_versionLabel = new QLabel(this);
    m_versionLabel->setProperty("role", "micro");
    m_versionLabel->setAlignment(Qt::AlignCenter);
    m_versionLabel->setText(QStringLiteral("Clash-Q %1").arg(AppPaths::appVersion()));
    root->addWidget(m_versionLabel);
    root->addSpacing(2);

    connect(m_navGroup, &QButtonGroup::idClicked, this, [this](int id) {
        emit pageSelected(id);
    });
    if (auto* first = m_navGroup->button(0))
        first->setChecked(true);
}

QAbstractButton* Sidebar::addNavItem(QButtonGroup* group, int index, const QString& glyph,
                                     const QString& text)
{
    auto* item = new SidebarNavItem(glyph, text, this);
    item->setAccessibleName(text);
    group->addButton(item, index);
    m_navItems.append(item);
    return item;
}

void Sidebar::setCurrentPage(int index)
{
    if (QAbstractButton* button = m_navGroup->button(index))
        button->setChecked(true);
}

int Sidebar::currentPage() const
{
    return m_navGroup->checkedId();
}

QSize Sidebar::sizeHint() const
{
    return QSize(232, 720);
}

void Sidebar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), pal().surface);
    painter.setPen(QPen(pal().outlineVariant, 1));
    painter.drawLine(width() - 1, 0, width() - 1, height());
}

void Sidebar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_profileName)
        m_profileName->setToolTip(m_profileName->text());
}

void Sidebar::setCoreState(cq::CoreState state)
{
    m_state = state;
    const Palette& p = pal();
    QColor color = p.onSurfaceVariant;
    QString stateText = QStringLiteral("当前配置");
    switch (state) {
    case cq::CoreState::Running:
        color = p.success;
        stateText = QStringLiteral("已保护");
        break;
    case cq::CoreState::Starting:
        color = p.warning;
        stateText = QStringLiteral("正在连接");
        break;
    case cq::CoreState::Stopping:
        color = p.warning;
        stateText = QStringLiteral("正在断开");
        break;
    case cq::CoreState::Failed:
        color = p.error;
        stateText = QStringLiteral("连接异常");
        break;
    case cq::CoreState::Stopped:
        break;
    }
    m_stateDot->setColor(color);
    m_stateLabel->setText(stateText);
    updateProfileCard();
}

void Sidebar::setCoreVersion(const QString& version)
{
    m_coreVersion = version;
    if (version.isEmpty()) {
        m_versionLabel->setText(QStringLiteral("Clash-Q %1").arg(AppPaths::appVersion()));
    } else {
        m_versionLabel->setText(
            QStringLiteral("Clash-Q %1 · %2").arg(AppPaths::appVersion(), version));
    }
}

void Sidebar::setActiveProfileName(const QString& name)
{
    m_profileName->setText(name);
    m_profileName->setToolTip(name);
    updateProfileCard();
}

void Sidebar::setStats(const cq::SessionStats& stats)
{
    m_stats = stats;
    updateProfileCard();
}

void Sidebar::updateProfileCard()
{
    if (!m_sessionLabel)
        return;
    if (m_state == cq::CoreState::Running) {
        m_sessionLabel->setText(QStringLiteral("已连接 · %1")
                                    .arg(Formatters::clockDuration(m_stats.connectedSeconds)));
    } else if (m_state == cq::CoreState::Starting) {
        m_sessionLabel->setText(QStringLiteral("正在启动 Mihomo 内核"));
    } else if (m_state == cq::CoreState::Failed) {
        m_sessionLabel->setText(QStringLiteral("请查看日志排查原因"));
    } else {
        m_sessionLabel->setText(QStringLiteral("尚未连接"));
    }
}

bool Sidebar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_profileCard && event->type() == QEvent::MouseButtonRelease) {
        emit pageSelected(2);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
