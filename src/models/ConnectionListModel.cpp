#include "models/ConnectionListModel.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>

ConnectionListModel::ConnectionListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ConnectionListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_connections.size();
}

QVariant ConnectionListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_connections.size())
        return {};
    const cq::ConnectionInfo& connection = m_connections.at(index.row());
    switch (role) {
    case IdRole:
        return connection.id;
    case HostRole:
        return connection.host;
    case NetworkRole:
        return connection.network;
    case TypeRole:
        return connection.type;
    case RuleRole:
        return connection.rule;
    case ChainRole:
        return connection.chain;
    case ProcessRole:
        return connection.process;
    case UploadRole:
        return connection.upload;
    case DownloadRole:
        return connection.download;
    case StartRole:
        return connection.startedAt;
    case Qt::DisplayRole:
        return connection.host;
    case Qt::ToolTipRole:
        return QStringLiteral("%1\n%2 · %3\n%4")
            .arg(connection.host, connection.network, connection.type, connection.chain);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConnectionListModel::roleNames() const
{
    return {
        {IdRole, "id"},       {HostRole, "host"},         {NetworkRole, "network"},
        {TypeRole, "type"},   {RuleRole, "rule"},         {ChainRole, "chain"},
        {UploadRole, "upload"}, {DownloadRole, "download"}, {StartRole, "start"},
    };
}

void ConnectionListModel::setConnections(const QVector<cq::ConnectionInfo>& connections)
{
    bool sameOrder = m_connections.size() == connections.size();
    if (sameOrder) {
        for (int i = 0; i < connections.size(); ++i) {
            if (m_connections.at(i).id != connections.at(i).id) {
                sameOrder = false;
                break;
            }
        }
    }

    if (!sameOrder) {
        beginResetModel();
        m_connections = connections;
        endResetModel();
        return;
    }

    m_connections = connections;
    if (!m_connections.isEmpty())
        emit dataChanged(index(0), index(m_connections.size() - 1));
}

ConnectionFilterProxyModel::ConnectionFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void ConnectionFilterProxyModel::setFilterText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_filter)
        return;
    m_filter = trimmed;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ConnectionFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                  const QModelIndex& sourceParent) const
{
    if (m_filter.isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    const QStringList values = {
        index.data(ConnectionListModel::HostRole).toString(),
        index.data(ConnectionListModel::NetworkRole).toString(),
        index.data(ConnectionListModel::TypeRole).toString(),
        index.data(ConnectionListModel::RuleRole).toString(),
        index.data(ConnectionListModel::ChainRole).toString(),
        index.data(ConnectionListModel::ProcessRole).toString(),
    };
    for (const QString& value : values) {
        if (value.contains(m_filter, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

ConnectionDelegate::ConnectionDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ConnectionDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    const Palette& p = pal();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    const QRectF row = option.rect.adjusted(8, 2, -8, -2);
    if (option.state & QStyle::State_MouseOver) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(p.surfaceContainerHigh);
        painter->drawRoundedRect(row, 10, 10);
    }

    const QString host = index.data(ConnectionListModel::HostRole).toString();
    const QString network = index.data(ConnectionListModel::NetworkRole).toString();
    const QString type = index.data(ConnectionListModel::TypeRole).toString();
    const QString rule = index.data(ConnectionListModel::RuleRole).toString();
    const QString chain = index.data(ConnectionListModel::ChainRole).toString();
    const qint64 upload = index.data(ConnectionListModel::UploadRole).toLongLong();
    const qint64 download = index.data(ConnectionListModel::DownloadRole).toLongLong();

    const qreal left = option.rect.left() + 18;
    const qreal right = option.rect.right() - 18;
    const qreal closeX = right - 10;
    const qreal top = option.rect.top() + 13;
    const qreal bottom = option.rect.bottom() - 13;

    const QFont valueFont = Fonts::text(10, QFont::DemiBold);
    const QFontMetricsF valueMetrics(valueFont);
    const QString downText = Formatters::bytes(download, 1);
    const QString upText = Formatters::bytes(upload, 1);
    const qreal downWidth = valueMetrics.horizontalAdvance(downText);
    const qreal upWidth = valueMetrics.horizontalAdvance(upText);
    const qreal trafficWidth = downWidth + upWidth + 44;
    const qreal trafficLeft = closeX - trafficWidth - 12;

    const QFont hostFont = Fonts::text(13, QFont::DemiBold);
    const QFontMetricsF hostMetrics(hostFont);
    painter->setFont(hostFont);
    painter->setPen(p.onSurface);
    const qreal hostWidth = qMax<qreal>(60, trafficLeft - left - 12);
    painter->drawText(QRectF(left, top, hostWidth, 19), Qt::AlignVCenter | Qt::AlignLeft,
                      hostMetrics.elidedText(host, Qt::ElideMiddle, static_cast<int>(hostWidth)));

    painter->setFont(valueFont);
    qreal x = trafficLeft + 2;
    painter->setFont(Fonts::icon(10));
    painter->setPen(p.chartDownload);
    painter->drawText(QRectF(x, top + 2, 12, 14), Qt::AlignCenter,
                      QStringLiteral("arrow_downward"));
    x += 12;
    painter->setFont(valueFont);
    painter->setPen(p.onSurfaceVariant);
    painter->drawText(QRectF(x, top, downWidth + 2, 19), Qt::AlignVCenter | Qt::AlignLeft,
                      downText);
    x += downWidth + 11;
    painter->setFont(Fonts::icon(10));
    painter->setPen(p.chartUpload);
    painter->drawText(QRectF(x, top + 2, 12, 14), Qt::AlignCenter,
                      QStringLiteral("arrow_upward"));
    x += 12;
    painter->setFont(valueFont);
    painter->setPen(p.onSurfaceVariant);
    painter->drawText(QRectF(x, top, upWidth + 2, 19), Qt::AlignVCenter | Qt::AlignLeft,
                      upText);

    painter->setFont(Fonts::icon(16));
    painter->setPen(Theme::alpha(p.onSurfaceVariant, 165));
    painter->drawText(QRectF(closeX, option.rect.center().y() - 10, 20, 20), Qt::AlignCenter,
                      QStringLiteral("close"));

    const QFont typeFont = Fonts::text(9, QFont::DemiBold);
    const QFontMetricsF typeMetrics(typeFont);
    QString typeText = network.toUpper();
    if (!type.isEmpty() && type.compare(network, Qt::CaseInsensitive) != 0)
        typeText += QStringLiteral(" · ") + type.toUpper();
    const qreal typeWidth = typeMetrics.horizontalAdvance(typeText) + 16;
    const QRectF typeRect(left, bottom - 9, typeWidth, 18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::alpha(p.primary, 24));
    painter->drawRoundedRect(typeRect, 6, 6);
    painter->setFont(typeFont);
    painter->setPen(p.primary);
    painter->drawText(typeRect, Qt::AlignCenter, typeText);

    const qreal chainX = typeRect.right() + 10;
    const qreal chainWidth = qMax<qreal>(40, trafficLeft - chainX - 12);
    QString route = rule;
    if (!chain.isEmpty())
        route += QStringLiteral("  ·  ") + chain;
    const QFont routeFont = Fonts::text(10);
    const QFontMetricsF routeMetrics(routeFont);
    painter->setFont(routeFont);
    painter->setPen(p.onSurfaceVariant);
    painter->drawText(QRectF(chainX, bottom - 10, chainWidth, 19),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      routeMetrics.elidedText(route, Qt::ElideMiddle,
                                              static_cast<int>(chainWidth)));

    painter->setPen(QPen(p.outlineVariant, 1));
    painter->drawLine(QPointF(option.rect.left() + 18, option.rect.bottom() - 0.5),
                      QPointF(option.rect.right() - 18, option.rect.bottom() - 0.5));
    painter->restore();
}

QSize ConnectionDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return QSize(option.rect.width(), m_compact ? 58 : 64);
}

ConnectionListView::ConnectionListView(QWidget* parent)
    : QListView(parent)
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSelectionMode(QAbstractItemView::NoSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setUniformItemSizes(true);
}

void ConnectionListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        if (index.isValid() && pos.x() >= viewport()->width() - 54) {
            emit closeRequested(index.data(ConnectionListModel::IdRole).toString());
            event->accept();
            return;
        }
    }
    QListView::mouseReleaseEvent(event);
}
