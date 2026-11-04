#pragma once

#include "core/Types.h"

#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ClashController;
class FlowLayout;
class M3Button;
class M3Card;
class M3EmptyState;
class M3FilterChip;
class M3Pill;
class PageHeader;
class ProxyNodeCard;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;

class ProxiesPage : public QWidget {
    Q_OBJECT

public:
    explicit ProxiesPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();

signals:
    void navigateRequested(int index);

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateNodeHostHeight();
    void buildUi();
    void wire();
    void syncGroups();
    void rebuildChips();
    void rebuildNodes();
    void updateToolbar();
    void selectGroup(const QString& name);
    void updateNodeCards();
    const cq::ProxyGroup* currentGroup() const;

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    M3Button* m_testButton = nullptr;
    QScrollArea* m_chipScroll = nullptr;
    QWidget* m_chipHost = nullptr;
    QHBoxLayout* m_chipLayout = nullptr;
    QButtonGroup* m_chipGroup = nullptr;
    M3Card* m_nodesCard = nullptr;
    QWidget* m_toolbar = nullptr;
    M3Pill* m_typePill = nullptr;
    QLabel* m_currentLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_groupTitle = nullptr;
    QScrollArea* m_nodeScroll = nullptr;
    QWidget* m_nodeHost = nullptr;
    FlowLayout* m_nodeFlow = nullptr;
    M3EmptyState* m_emptyState = nullptr;
    QHash<QString, ProxyNodeCard*> m_cards;
    QVector<cq::ProxyGroup> m_groups;
    QString m_currentGroup;
    QString m_renderedGroup;
    QStringList m_renderedNodeKeys;
    QStringList m_chipNames;
};
