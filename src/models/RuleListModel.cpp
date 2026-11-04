#include "models/RuleListModel.h"

#include "app/Fonts.h"
#include "app/Theme.h"
#include "core/Formatters.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

RuleListModel::RuleListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int RuleListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rules.size();
}

QVariant RuleListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size())
        return {};
    const cq::RuleInfo& rule = m_rules.at(index.row());
    switch (role) {
    case TypeRole:
        return rule.type;
    case PayloadRole:
        return rule.payload;
    case ProxyRole:
        return rule.proxy;
    case Qt::DisplayRole:
        return QStringLiteral("%1 %2 → %3").arg(rule.type, rule.payload, rule.proxy);
    case Qt::ToolTipRole:
        return QStringLiteral("%1,%2,%3").arg(rule.type, rule.payload, rule.proxy);
    default:
        return {};
    }
}

QHash<int, QByteArray> RuleListModel::roleNames() const
{
    return {{TypeRole, "type"}, {PayloadRole, "payload"}, {ProxyRole, "proxy"}};
}

void RuleListModel::setRules(const QVector<cq::RuleInfo>& rules)
{
    if (m_rules.size() == rules.size()) {
        bool same = true;
        for (int i = 0; i < rules.size(); ++i) {
            if (m_rules.at(i).type != rules.at(i).type ||
                m_rules.at(i).payload != rules.at(i).payload ||
                m_rules.at(i).proxy != rules.at(i).proxy) {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }
    beginResetModel();
    m_rules = rules;
    endResetModel();
}

RuleFilterProxyModel::RuleFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void RuleFilterProxyModel::setFilterText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (m_filter == trimmed)
        return;
    m_filter = trimmed;
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool RuleFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_filter.isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    return index.data(RuleListModel::TypeRole).toString().contains(m_filter, Qt::CaseInsensitive) ||
           index.data(RuleListModel::PayloadRole)
               .toString()
               .contains(m_filter, Qt::CaseInsensitive) ||
           index.data(RuleListModel::ProxyRole).toString().contains(m_filter, Qt::CaseInsensitive);
}

RuleDelegate::RuleDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void RuleDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
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

    const QString type = index.data(RuleListModel::TypeRole).toString();
    const QString payload = index.data(RuleListModel::PayloadRole).toString();
    const QString proxy = index.data(RuleListModel::ProxyRole).toString();

    const QFont typeFont = Fonts::text(10, QFont::DemiBold);
    const QFontMetricsF typeMetrics(typeFont);
    const qreal typeWidth = qMin<qreal>(typeMetrics.horizontalAdvance(type) + 18, 138);
    QRectF typeRect(option.rect.left() + 16, option.rect.center().y() - 11, typeWidth, 22);
    painter->setPen(Qt::NoPen);
    painter->setBrush(p.surfaceContainerHigh);
    painter->drawRoundedRect(typeRect, 7, 7);
    painter->setFont(typeFont);
    painter->setPen(p.onSurfaceVariant);
    painter->drawText(typeRect, Qt::AlignCenter,
                     typeMetrics.elidedText(type, Qt::ElideRight, static_cast<int>(typeWidth - 12)));

    const QFont proxyFont = Fonts::text(10, QFont::DemiBold);
    const QFontMetricsF proxyMetrics(proxyFont);
    const qreal labelWidth = qMin<qreal>(proxyMetrics.horizontalAdvance(proxy) + 22, 190);
    QRectF proxyRect(option.rect.right() - 16 - labelWidth, option.rect.center().y() - 11,
                     labelWidth, 22);
    const bool direct = proxy.compare(QStringLiteral("DIRECT"), Qt::CaseInsensitive) == 0;
    painter->setPen(Qt::NoPen);
    painter->setBrush(direct ? p.surfaceContainerHighest : Theme::alpha(p.secondary, 28));
    painter->drawRoundedRect(proxyRect, 7, 7);
    painter->setFont(proxyFont);
    painter->setPen(direct ? p.onSurfaceVariant : p.secondary);
    painter->drawText(proxyRect, Qt::AlignCenter,
                      proxyMetrics.elidedText(proxy, Qt::ElideRight,
                                              static_cast<int>(labelWidth - 14)));

    const qreal payloadX = typeRect.right() + 14;
    const qreal payloadWidth = proxyRect.left() - payloadX - 16;
    const QFont payloadFont = Fonts::text(12, QFont::Medium);
    painter->setFont(payloadFont);
    painter->setPen(p.onSurface);
    const QFontMetricsF payloadMetrics(payloadFont);
    painter->drawText(QRectF(payloadX, option.rect.top(), payloadWidth, option.rect.height()),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      payloadMetrics.elidedText(payload, Qt::ElideMiddle,
                                                static_cast<int>(payloadWidth)));

    painter->setPen(QPen(p.outlineVariant, 1));
    painter->drawLine(QPointF(option.rect.left() + 18, option.rect.bottom() - 0.5),
                      QPointF(option.rect.right() - 18, option.rect.bottom() - 0.5));
    painter->restore();
}

QSize RuleDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return QSize(option.rect.width(), 52);
}
