#pragma once

#include "core/Types.h"

#include <QWidget>

class ClashController;
class ConnectionFilterProxyModel;
class ConnectionListModel;
class ConnectionListView;
class M3Button;
class M3EmptyState;
class M3Pill;
class PageHeader;
class QLabel;
class SearchField;

class ConnectionsPage : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionsPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();

private:
    void buildUi();
    void wire();
    void updateSummary();
    void updateViewVisibility();

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    M3Button* m_closeAll = nullptr;
    M3Pill* m_livePill = nullptr;
    M3Pill* m_activePill = nullptr;
    M3Pill* m_downloadPill = nullptr;
    M3Pill* m_uploadPill = nullptr;
    SearchField* m_search = nullptr;
    QLabel* m_resultLabel = nullptr;
    ConnectionListModel* m_model = nullptr;
    ConnectionFilterProxyModel* m_filter = nullptr;
    ConnectionListView* m_view = nullptr;
    M3EmptyState* m_empty = nullptr;
};
