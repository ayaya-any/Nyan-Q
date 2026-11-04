#pragma once

#include "core/Types.h"

#include <QWidget>

class ClashController;
class M3Button;
class M3EmptyState;
class M3Pill;
class PageHeader;
class QLabel;
class QListView;
class QSortFilterProxyModel;
class RuleFilterProxyModel;
class RuleListModel;
class SearchField;

class RulesPage : public QWidget {
    Q_OBJECT

public:
    explicit RulesPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();

private:
    void buildUi();
    void wire();
    void updateSummary();
    void updateViewVisibility();

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    SearchField* m_search = nullptr;
    M3Pill* m_countPill = nullptr;
    QLabel* m_hintLabel = nullptr;
    RuleListModel* m_model = nullptr;
    RuleFilterProxyModel* m_filter = nullptr;
    QListView* m_view = nullptr;
    M3EmptyState* m_empty = nullptr;
};
