#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

class ClashController;
class CodeEditor;
class M3Button;
class M3Pill;
class M3Switch;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

class ScriptEditorView final : public QWidget {
    Q_OBJECT

public:
    explicit ScriptEditorView(ClashController* controller, const QString& scriptId,
                              QWidget* parent = nullptr);
    ~ScriptEditorView() override;

    bool confirmClose();
    bool isModified() const;
    QString scriptId() const { return m_scriptId; }

signals:
    void closeRequested();
    void saved();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void buildUi();
    void wire();
    void loadContent();
    void saveContent();
    void commitName();
    void testScript();
    void openBindings();
    void openHistory();
    void openLogs();
    void checkUpdate();
    void refreshMeta();
    void updateModifiedState();
    void setFeedback(const QString& message, bool error = false);
    void showFindBar();
    void hideFindBar();
    void findNext(bool backward);
    void applyExpectedHash();

    ClashController* m_controller = nullptr;
    QString m_scriptId;
    QByteArray m_originalText;
    bool m_loading = false;

    CodeEditor* m_editor = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_expectedHash = nullptr;
    QSpinBox* m_interval = nullptr;
    M3Switch* m_enabledSwitch = nullptr;
    M3Switch* m_autoUpdateSwitch = nullptr;
    M3Pill* m_sourcePill = nullptr;
    M3Pill* m_trustPill = nullptr;
    M3Pill* m_modifiedPill = nullptr;
    QLabel* m_urlLabel = nullptr;
    QLabel* m_hashLabel = nullptr;
    QLabel* m_updatedLabel = nullptr;
    QLabel* m_checkedLabel = nullptr;
    QLabel* m_feedback = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    M3Button* m_saveButton = nullptr;
    M3Button* m_updateButton = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findEdit = nullptr;
};
