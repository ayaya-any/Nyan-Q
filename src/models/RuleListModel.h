#pragma once

#include "core/Types.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class RuleListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        PayloadRole,
        ProxyRole,
    };

    explicit RuleListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRules(const QVector<cq::RuleInfo>& rules);
    const QVector<cq::RuleInfo>& rules() const { return m_rules; }

private:
    QVector<cq::RuleInfo> m_rules;
};

class RuleFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit RuleFilterProxyModel(QObject* parent = nullptr);

    void setFilterText(const QString& text);
    QString filterText() const { return m_filter; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_filter;
};

class RuleDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RuleDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
