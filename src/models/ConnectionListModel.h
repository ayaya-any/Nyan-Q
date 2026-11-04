#pragma once

#include "core/Types.h"

#include <QAbstractListModel>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class ConnectionListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        HostRole,
        NetworkRole,
        TypeRole,
        RuleRole,
        ChainRole,
        ProcessRole,
        UploadRole,
        DownloadRole,
        StartRole,
    };

    explicit ConnectionListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setConnections(const QVector<cq::ConnectionInfo>& connections);
    const QVector<cq::ConnectionInfo>& connections() const { return m_connections; }

private:
    QVector<cq::ConnectionInfo> m_connections;
};

class ConnectionFilterProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ConnectionFilterProxyModel(QObject* parent = nullptr);

    void setFilterText(const QString& text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_filter;
};

class ConnectionDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ConnectionDelegate(QObject* parent = nullptr);
    void setCompact(bool compact) { m_compact = compact; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    bool m_compact = false;
};

class ConnectionListView final : public QListView {
    Q_OBJECT

public:
    explicit ConnectionListView(QWidget* parent = nullptr);

signals:
    void closeRequested(const QString& id);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
