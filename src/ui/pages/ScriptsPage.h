#pragma once

#include <QWidget>

class ClashController;
class M3Button;
class M3Pill;
class PageHeader;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class ScriptEditorView;

class ScriptsPage final : public QWidget {
    Q_OBJECT

public:
    explicit ScriptsPage(ClashController* controller, QWidget* parent = nullptr);

    void refresh();
    void openScriptEditor(const QString& id) { openEditor(id); }
    void createScriptForProfile(const QString& profileId);

private:
    void buildUi();
    void wire();
    void rebuild();
    void createScript();
    void importFile();
    void importUrl();
    void openSettings();
    void previewAll();
    void openEditor(const QString& id);
    void closeEditor();
    void clearEditor();
    void deleteScript(const QString& id);
    void checkUpdate(const QString& id);
    void openBindings(const QString& id);
    void openHistory(const QString& id);
    void openLogs(const QString& id);
    void testScript(const QString& id);
    void updateBusyState();
    void setFeedback(const QString& message, bool error = false);

    ClashController* m_controller = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_listPage = nullptr;
    QWidget* m_editorPage = nullptr;
    QVBoxLayout* m_editorLayout = nullptr;
    ScriptEditorView* m_editor = nullptr;
    PageHeader* m_header = nullptr;
    M3Button* m_newButton = nullptr;
    M3Button* m_importFileButton = nullptr;
    M3Button* m_importUrlButton = nullptr;
    M3Pill* m_busyPill = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_feedback = nullptr;
};
