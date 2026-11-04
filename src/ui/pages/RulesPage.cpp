#include "ui/pages/RulesPage.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "models/RuleListModel.h"
#include "ui/PageHeader.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/SearchField.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

RulesPage::RulesPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void RulesPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("规则"),
                              QStringLiteral("查看当前配置生效的分流规则。"), this);
    root->addWidget(m_header);

    auto* card = new M3Card(this, 20);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 12);
    cardLayout->setSpacing(10);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(10);
    m_search = new SearchField(QStringLiteral("搜索规则类型、匹配内容或策略"), card);
    m_search->setFixedWidth(330);
    toolbar->addWidget(m_search);
    m_countPill = new M3Pill(QStringLiteral("0 条规则"), card);
    m_countPill->setTone(M3Pill::Tone::Neutral);
    toolbar->addWidget(m_countPill);
    toolbar->addStretch();
    m_hintLabel = new QLabel(QStringLiteral("规则按从上到下的顺序匹配"), card);
    m_hintLabel->setProperty("role", "micro");
    toolbar->addWidget(m_hintLabel);
    cardLayout->addLayout(toolbar);

    auto* divider = new M3Divider(card);
    cardLayout->addWidget(divider);

    m_model = new RuleListModel(this);
    m_filter = new RuleFilterProxyModel(this);
    m_filter->setSourceModel(m_model);

    m_view = new QListView(card);
    m_view->setModel(m_filter);
    m_view->setItemDelegate(new RuleDelegate(m_view));
    m_view->setSelectionMode(QAbstractItemView::NoSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setUniformItemSizes(true);
    m_view->setMouseTracking(true);
    m_view->viewport()->setMouseTracking(true);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cardLayout->addWidget(m_view, 1);

    m_empty = new M3EmptyState(
        QStringLiteral("rule"), QStringLiteral("内核未连接"),
        QStringLiteral("启动 Mihomo 后，当前配置中的分流规则会加载到这里。"), card);
    cardLayout->addWidget(m_empty, 1);
    m_empty->hide();
    root->addWidget(card, 1);

    connect(m_search, &SearchField::textChanged, this, [this](const QString& text) {
        m_filter->setFilterText(text);
        updateSummary();
        updateViewVisibility();
    });
}

void RulesPage::wire()
{
    connect(m_controller, &ClashController::rulesChanged, this,
            [this](const QVector<cq::RuleInfo>& rules) {
                const int scroll = m_view->verticalScrollBar()->value();
                m_model->setRules(rules);
                QTimer::singleShot(0, this, [this, scroll] {
                    m_view->verticalScrollBar()->setValue(scroll);
                });
                updateSummary();
                updateViewVisibility();
            });
    connect(m_controller, &ClashController::stateChanged, this, [this](cq::CoreState) {
        updateSummary();
        updateViewVisibility();
    });
}

void RulesPage::refresh()
{
    m_model->setRules(m_controller->rules());
    updateSummary();
    updateViewVisibility();
}

void RulesPage::updateSummary()
{
    const int total = m_model->rowCount();
    const int visible = m_filter->rowCount();
    m_countPill->setText(m_search->text().trimmed().isEmpty()
                             ? QStringLiteral("%1 条规则").arg(total)
                             : QStringLiteral("%1 / %2 条").arg(visible).arg(total));
    const bool running = m_controller->state() == cq::CoreState::Running;
    m_header->setSubtitle(running
                              ? QStringLiteral("当前配置共 %1 条规则，已由 Mihomo 加载并生效。").arg(total)
                              : QStringLiteral("启动 Mihomo 后即可查看当前配置的分流规则。"));
}

void RulesPage::updateViewVisibility()
{
    const bool hasAny = m_model->rowCount() > 0;
    const bool hasVisible = m_filter->rowCount() > 0;
    const bool running = m_controller->state() == cq::CoreState::Running;

    if (!hasAny || !hasVisible) {
        m_view->hide();
        m_empty->show();
        if (!running) {
            m_empty->setTitle(QStringLiteral("等待内核就绪"));
            m_empty->setSubtitle(QStringLiteral("Mihomo 启动后，当前配置中的分流规则会加载到这里。"));
        } else if (!hasVisible) {
            m_empty->setTitle(QStringLiteral("没有匹配的规则"));
            m_empty->setSubtitle(QStringLiteral("尝试更换搜索关键词，或清空搜索框查看全部规则。"));
        } else {
            m_empty->setTitle(QStringLiteral("没有规则"));
            m_empty->setSubtitle(QStringLiteral("当前订阅没有提供可识别的分流规则。"));
        }
    } else {
        m_empty->hide();
        m_view->show();
    }
}
