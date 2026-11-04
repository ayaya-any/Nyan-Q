#include "ui/pages/ConnectionsPage.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/ClashController.h"
#include "core/Formatters.h"
#include "models/ConnectionListModel.h"
#include "ui/PageHeader.h"
#include "ui/dialogs/ConfirmDialog.h"
#include "ui/widgets/MaterialWidgets.h"
#include "ui/widgets/SearchField.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

ConnectionsPage::ConnectionsPage(ClashController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    wire();
    refresh();
}

void ConnectionsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 18, 32, 22);
    root->setSpacing(14);

    m_header = new PageHeader(QStringLiteral("连接"),
                              QStringLiteral("实时查看经过 Mihomo 的活动连接。"), this);
    m_closeAll = new M3Button(QStringLiteral("全部断开"), M3Button::Variant::Text, this);
    m_closeAll->setIconText(QStringLiteral("block"));
    m_closeAll->setAccent(Qt::transparent, pal().error);
    m_closeAll->setEnabled(false);
    m_header->addAction(m_closeAll);
    root->addWidget(m_header);

    auto* card = new M3Card(this, 20);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 12);
    cardLayout->setSpacing(10);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    m_search = new SearchField(QStringLiteral("搜索主机、规则或链路"), card);
    m_search->setFixedWidth(260);
    toolbar->addWidget(m_search);

    m_livePill = new M3Pill(QStringLiteral("每 2 秒刷新"), card);
    m_livePill->setTone(M3Pill::Tone::Neutral);
    m_livePill->setDot(true);
    toolbar->addWidget(m_livePill);

    m_activePill = new M3Pill(QStringLiteral("0 个连接"), card);
    m_activePill->setTone(M3Pill::Tone::Info);
    toolbar->addWidget(m_activePill);
    m_downloadPill = new M3Pill(QStringLiteral("↓ 0 B"), card);
    m_downloadPill->setTone(M3Pill::Tone::Neutral);
    toolbar->addWidget(m_downloadPill);
    m_uploadPill = new M3Pill(QStringLiteral("↑ 0 B"), card);
    m_uploadPill->setTone(M3Pill::Tone::Neutral);
    toolbar->addWidget(m_uploadPill);

    toolbar->addStretch();
    m_resultLabel = new QLabel(card);
    m_resultLabel->setProperty("role", "micro");
    toolbar->addWidget(m_resultLabel);
    cardLayout->addLayout(toolbar);

    auto* divider = new M3Divider(card);
    cardLayout->addWidget(divider);

    m_model = new ConnectionListModel(this);
    m_filter = new ConnectionFilterProxyModel(this);
    m_filter->setSourceModel(m_model);

    m_view = new ConnectionListView(card);
    m_view->setModel(m_filter);
    m_view->setItemDelegate(new ConnectionDelegate(m_view));
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    cardLayout->addWidget(m_view, 1);

    m_empty = new M3EmptyState(
        QStringLiteral("lan"), QStringLiteral("内核未连接"),
        QStringLiteral("启动 Mihomo 后，经过代理的网络连接会实时显示在这里。"), card);
    cardLayout->addWidget(m_empty, 1);
    m_empty->hide();

    root->addWidget(card, 1);

    connect(m_search, &SearchField::textChanged, this, [this](const QString& text) {
        m_filter->setFilterText(text);
        updateViewVisibility();
        updateSummary();
    });
    connect(m_view, &ConnectionListView::closeRequested, this, [this](const QString& id) {
        m_controller->closeConnection(id);
    });
    connect(m_closeAll, &QAbstractButton::clicked, this, [this] {
        if (m_controller->connections().isEmpty())
            return;
        if (ConfirmDialog::ask(window(), QStringLiteral("断开全部连接"),
                               QStringLiteral("所有正在通过代理的下载、视频与网页连接将被中断。"),
                               QStringLiteral("全部断开"), true)) {
            m_controller->closeAllConnections();
        }
    });
}

void ConnectionsPage::wire()
{
    connect(m_controller, &ClashController::connectionsChanged, this,
            [this](const QVector<cq::ConnectionInfo>& connections) {
                const int scroll = m_view->verticalScrollBar()->value();
                m_model->setConnections(connections);
                QTimer::singleShot(0, this, [this, scroll] {
                    m_view->verticalScrollBar()->setValue(scroll);
                });
                updateSummary();
                updateViewVisibility();
            });
    connect(m_controller, &ClashController::statsChanged, this,
            [this](const cq::SessionStats&) { updateSummary(); });
    connect(m_controller, &ClashController::stateChanged, this, [this](cq::CoreState) {
        updateSummary();
        updateViewVisibility();
    });
}

void ConnectionsPage::refresh()
{
    m_model->setConnections(m_controller->connections());
    updateSummary();
    updateViewVisibility();
}

void ConnectionsPage::updateSummary()
{
    const cq::SessionStats stats = m_controller->stats();
    m_activePill->setText(QStringLiteral("%1 个连接").arg(m_model->rowCount()));
    m_downloadPill->setText(QStringLiteral("↓ %1").arg(Formatters::bytes(stats.downloadTotal)));
    m_uploadPill->setText(QStringLiteral("↑ %1").arg(Formatters::bytes(stats.uploadTotal)));
    m_closeAll->setEnabled(m_model->rowCount() > 0);

    const int visible = m_filter->rowCount();
    const int total = m_model->rowCount();
    if (m_search->text().trimmed().isEmpty())
        m_resultLabel->setText(QStringLiteral("共 %1 条").arg(total));
    else
        m_resultLabel->setText(QStringLiteral("匹配 %1 / %2").arg(visible).arg(total));

    const bool running =
        m_controller->state() == cq::CoreState::Running || m_controller->isDemo();
    m_livePill->setText(running ? QStringLiteral("实时同步") : QStringLiteral("内核未连接"));
    m_livePill->setTone(running ? M3Pill::Tone::Success : M3Pill::Tone::Neutral);
    m_header->setSubtitle(QStringLiteral("实时查看经过 Mihomo 的活动连接 · 下行 %1 · 上行 %2")
                              .arg(Formatters::rate(stats.downloadSpeed),
                                   Formatters::rate(stats.uploadSpeed)));
}

void ConnectionsPage::updateViewVisibility()
{
    const bool hasAny = m_model->rowCount() > 0;
    const bool hasVisible = m_filter->rowCount() > 0;
    const bool running = m_controller->state() == cq::CoreState::Running;

    if (!hasAny || !hasVisible) {
        m_view->hide();
        m_empty->show();
        if (!running && !m_controller->isDemo()) {
            m_empty->setTitle(QStringLiteral("等待内核就绪"));
            m_empty->setSubtitle(QStringLiteral("Mihomo 启动后，经过代理的网络连接会实时显示在这里。"));
        } else if (hasAny && !hasVisible) {
            m_empty->setTitle(QStringLiteral("没有匹配的连接"));
            m_empty->setSubtitle(QStringLiteral("尝试更换搜索关键词，或清空搜索框查看全部连接。"));
        } else {
            m_empty->setTitle(QStringLiteral("暂无活动连接"));
            m_empty->setSubtitle(QStringLiteral("当应用开始访问网络时，新的连接会出现在这里。"));
        }
    } else {
        m_empty->hide();
        m_view->show();
    }
}
