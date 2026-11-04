#pragma once

#include "core/Types.h"

#include <QWidget>

class ClashController;
class M3Button;
class M3Pill;
class PageHeader;
class QLabel;
class QScrollArea;
class QVBoxLayout;

class ProfilesPage : public QWidget {
    Q_OBJECT

public:
    explicit ProfilesPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();
    void openActiveConfigEditor();

signals:
    void editConfigRequested(const QString& profileId);

private:
    void buildUi();
    void wire();
    void rebuild();
    void updateBusyState();
    void importUrl();
    void importFile();
    void editProfile(const cq::ProfileMeta& profile);
    void requestDelete(const cq::ProfileMeta& profile);

    ClashController* m_controller = nullptr;
    PageHeader* m_header = nullptr;
    M3Button* m_importUrlButton = nullptr;
    M3Button* m_importFileButton = nullptr;
    M3Pill* m_busyPill = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_countLabel = nullptr;
};
