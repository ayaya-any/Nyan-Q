#include "ui/pages/ProxiesPage.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/Formatters.h"
#include "ui/PageHeader.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/ProxyNodeCard.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

ProxiesPage::ProxiesPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void ProxiesPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(12);

    m_header = new PageHeader(QStringLiteral("代理"),
                              QStringLiteral("选择节点，流量会立即切换。"), this);
    m_testButton = new M3Button(QStringLiteral("全部测速"), M3Button::Variant::Tonal, this);
    m_testButton->setIconText(QStringLiteral("speed"));
    m_testButton->setEnabled(false);
    m_header->addAction(m_testButton);
    root->addWidget(m_header);

    m_chipScroll = new QScrollArea(this);
    m_chipScroll->setWidgetResizable(true);
    m_chipScroll->setFrameShape(QFrame::NoFrame);
    m_chipScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chipScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_chipScroll->setFixedHeight(53);
    m_chipScroll->setAttribute(Qt::WA_TranslucentBackground, true);
    m_chipScroll->viewport()->setAutoFillBackground(false);
    m_chipScroll->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    m_chipScroll->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"
                       "QScrollArea > QWidget > QWidget { background: transparent; }"
                       "QScrollBar:horizontal { background: transparent; }"));
    m_chipHost = new QWidget(m_chipScroll);
    m_chipHost->setAutoFillBackground(false);
    m_chipHost->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_chipLayout = new QHBoxLayout(m_chipHost);
    m_chipLayout->setContentsMargins(0, 3, 0, 3);
    m_chipLayout->setSpacing(8);
    m_chipLayout->addStretch();
    m_chipScroll->setWidget(m_chipHost);
    root->addWidget(m_chipScroll);

    m_nodesCard = new M3Card(this, 20);
    auto* cardLayout = new QVBoxLayout(m_nodesCard);
    cardLayout->setContentsMargins(18, 15, 18, 16);
    cardLayout->setSpacing(10);

    m_toolbar = new QWidget(m_nodesCard);
    auto* toolbarLayout = new QHBoxLayout(m_toolbar);
    toolbarLayout->setContentsMargins(2, 0, 2, 0);
    toolbarLayout->setSpacing(9);
    m_groupTitle = new QLabel(QStringLiteral("节点列表"), m_toolbar);
    m_groupTitle->setProperty("role", "section");
    toolbarLayout->addWidget(m_groupTitle);
    m_typePill = new M3Pill(QString(), m_toolbar);
    m_typePill->setTone(M3Pill::Tone::Info);
    toolbarLayout->addWidget(m_typePill, 0, Qt::AlignVCenter);
    m_currentLabel = new QLabel(m_toolbar);
    m_currentLabel->setProperty("role", "caption");
    toolbarLayout->addWidget(m_currentLabel, 0, Qt::AlignVCenter);
    toolbarLayout->addStretch();
    m_countLabel = new QLabel(m_toolbar);
    m_countLabel->setProperty("role", "caption");
    toolbarLayout->addWidget(m_countLabel, 0, Qt::AlignVCenter);
    cardLayout->addWidget(m_toolbar);

    auto* divider = new M3Divider(m_nodesCard);
    cardLayout->addWidget(divider);

    m_nodeScroll = new QScrollArea(m_nodesCard);
    m_nodeScroll->setWidgetResizable(true);
    m_nodeScroll->setFrameShape(QFrame::NoFrame);
    m_nodeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeHost = new QWidget(m_nodeScroll);
    m_nodeFlow = new FlowLayout(m_nodeHost, 2, 12, 12);
    m_nodeScroll->setWidget(m_nodeHost);
    cardLayout->addWidget(m_nodeScroll, 1);

    m_emptyState = new M3EmptyState(
        QStringLiteral("swap_horiz"), QStringLiteral("还没有可用的节点"),
        QStringLiteral("Mihomo 内核就绪后，代理组与节点会自动出现在这里。"), m_nodesCard);
    cardLayout->addWidget(m_emptyState, 1);
    m_emptyState->hide();

    root->addWidget(m_nodesCard, 1);

    connect(m_testButton, &QAbstractButton::clicked, this, [this] {
        if (!m_currentGroup.isEmpty())
            m_controller->testGroup(m_currentGroup);
    });
}

void ProxiesPage::wire()
{
    connect(m_controller, &ClashController::proxyGroupsChanged, this,
            [this](const QVector<cq::ProxyGroup>&) { syncGroups(); });
    connect(m_controller, &ClashController::stateChanged, this, [this](cq::CoreState) {
        updateToolbar();
        if (m_controller->proxyGroups().isEmpty())
            syncGroups();
    });
    connect(m_controller, &ClashController::delayUpdated, this,
            [this](const QString& node, int delay) {
                if (ProxyNodeCard* card = m_cards.value(node, nullptr))
                    card->setDelay(delay);
            });
}

void ProxiesPage::refresh()
{
    m_groups = m_controller->proxyGroups();
    syncGroups();
}

const cq::ProxyGroup* ProxiesPage::currentGroup() const
{
    for (const cq::ProxyGroup& group : m_groups) {
        if (group.name == m_currentGroup)
            return &group;
    }
    return nullptr;
}

void ProxiesPage::rebuildChips()
{
    if (m_chipGroup) {
        delete m_chipGroup;
        m_chipGroup = nullptr;
    }
    while (QLayoutItem* item = m_chipLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_chipNames.clear();

    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);

    for (int i = 0; i < m_groups.size(); ++i) {
        const cq::ProxyGroup& group = m_groups.at(i);
        auto* chip = new M3FilterChip(group.name, m_chipHost);
        m_chipLayout->addWidget(chip);
        m_chipGroup->addButton(chip, i);
        m_chipNames.append(group.name);
        if (group.name == m_currentGroup)
            chip->setChecked(true);
    }
    m_chipLayout->addStretch();
    connect(m_chipGroup, &QButtonGroup::idClicked, this, [this](int id) {
        const QString name = m_chipNames.value(id);
        if (!name.isEmpty())
            selectGroup(name);
    });
}

void ProxiesPage::selectGroup(const QString& name)
{
    if (m_currentGroup == name)
        return;
    m_currentGroup = name;
    if (m_chipGroup) {
        if (QAbstractButton* button = m_chipGroup->button(m_chipNames.indexOf(name)))
            button->setChecked(true);
    }
    rebuildNodes();
    updateToolbar();
}

void ProxiesPage::syncGroups()
{
    m_groups = m_controller->proxyGroups();

    const QString previous = m_currentGroup;
    const QStringList newNames = [this] {
        QStringList names;
        for (const cq::ProxyGroup& group : m_groups)
            names.append(group.name);
        return names;
    }();
    bool namesChanged = newNames != m_chipNames;
    if (namesChanged)
        rebuildChips();

    if (m_currentGroup.isEmpty() || !newNames.contains(m_currentGroup))
        m_currentGroup = newNames.isEmpty() ? QString() : newNames.first();

    if (m_currentGroup != previous)
        rebuildNodes();
    else if (m_currentGroup.isEmpty())
        rebuildNodes();
    else {
        const cq::ProxyGroup* group = currentGroup();
        if (!group) {
            rebuildNodes();
        } else {
            QStringList keys;
            for (const cq::ProxyNode& node : group->nodes)
                keys.append(node.name + QLatin1Char('|') + node.type);
            if (keys != m_renderedNodeKeys || m_renderedGroup != m_currentGroup)
                rebuildNodes();
            else
                updateNodeCards();
        }
    }

    if (m_chipGroup) {
        const int index = m_chipNames.indexOf(m_currentGroup);
        if (index >= 0) {
            if (QAbstractButton* button = m_chipGroup->button(index)) {
                if (!button->isChecked())
                    button->setChecked(true);
            }
        }
    }
    if (namesChanged && m_currentGroup.isEmpty()) {
        m_renderedGroup.clear();
        m_renderedNodeKeys.clear();
    }
    m_chipScroll->setVisible(!m_groups.isEmpty());
    updateToolbar();
}

void ProxiesPage::rebuildNodes()
{
    while (QLayoutItem* item = m_nodeFlow->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_cards.clear();
    m_renderedNodeKeys.clear();
    m_renderedGroup = m_currentGroup;

    const cq::ProxyGroup* group = currentGroup();
    if (!group) {
        m_nodeScroll->setVisible(false);
        m_emptyState->setVisible(true);
        m_nodesCard->layout()->invalidate();
        return;
    }

    m_emptyState->setVisible(false);
    m_nodeScroll->setVisible(true);

    for (const cq::ProxyNode& node : group->nodes) {
        auto* card = new ProxyNodeCard(node, node.name == group->now, m_nodeHost);
        m_cards.insert(node.name, card);
        connect(card, &ProxyNodeCard::selected, this, [this, groupName = group->name](const QString& name) {
            m_controller->selectProxy(groupName, name);
        });
        connect(card, &ProxyNodeCard::testRequested, this, [this](const QString& name) {
            m_controller->testProxy(name);
        });
        m_nodeFlow->addWidget(card);
        m_renderedNodeKeys.append(node.name + QLatin1Char('|') + node.type);
    }

    m_nodeFlow->invalidate();
    updateNodeHostHeight();
    m_nodeScroll->verticalScrollBar()->setValue(0);
}

void ProxiesPage::updateNodeHostHeight()
{
    if (!m_nodeFlow || !m_nodeHost || !m_nodeScroll)
        return;
    const int width = qMax(1, m_nodeScroll->viewport()->width());
    m_nodeHost->setMinimumHeight(m_nodeFlow->heightForWidth(width));
    m_nodeHost->updateGeometry();
}

void ProxiesPage::updateNodeCards()
{
    const cq::ProxyGroup* group = currentGroup();
    if (!group)
        return;
    for (const cq::ProxyNode& node : group->nodes) {
        if (ProxyNodeCard* card = m_cards.value(node.name, nullptr))
            card->setNode(node, node.name == group->now);
    }
}

void ProxiesPage::updateToolbar()
{
    const cq::ProxyGroup* group = currentGroup();
    const bool hasGroup = group != nullptr;
    m_testButton->setEnabled(hasGroup);
    m_toolbar->setVisible(hasGroup);
    if (!hasGroup) {
        m_nodeScroll->setVisible(false);
        m_emptyState->setVisible(true);
        m_header->setSubtitle(QStringLiteral("Mihomo 内核就绪后即可选择代理节点。"));
        return;
    }

    m_emptyState->setVisible(false);
    m_nodeScroll->setVisible(true);
    m_groupTitle->setText(group->name);
    m_typePill->setText(cq::proxyTypeDisplay(group->type));
    m_typePill->setTone(group->type.contains(QStringLiteral("Selector")) ? M3Pill::Tone::Info
                                                                        : M3Pill::Tone::Neutral);
    m_countLabel->setText(QStringLiteral("共 %1 个节点").arg(group->nodes.size()));

    const QString current = group->now.isEmpty() ? QStringLiteral("自动选择") : group->now;
    QFontMetricsF metrics(Fonts::text(12));
    const QString elided = metrics.elidedText(current, Qt::ElideRight, 280);
    m_currentLabel->setText(QStringLiteral("当前 · %1").arg(elided));
    m_currentLabel->setToolTip(current);

    m_header->setSubtitle(QStringLiteral("%1 个代理组 · %2 个可用节点 · 点击节点立即切换")
                              .arg(m_groups.size())
                              .arg(group->nodes.size()));
}

bool ProxiesPage::event(QEvent* event)
{
    if (event->type() == QEvent::Show)
        updateNodeHostHeight();
    return QWidget::event(event);
}

void ProxiesPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateNodeHostHeight();
}
