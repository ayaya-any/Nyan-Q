#pragma once

#include "core/Types.h"

#include <QWidget>

class ClashController;
class CodeEditor;
class M3Button;
class M3IconButton;
class M3Pill;
class QLabel;
class QLineEdit;

class ConfigEditorView final : public QWidget {
    Q_OBJECT

public:
    explicit ConfigEditorView(ClashController* controller,
                              const cq::ProfileMeta& profile, QWidget* parent = nullptr);
    ~ConfigEditorView() override = default;

    bool confirmClose();
    bool isModified() const;
    QString profileId() const { return m_profile.id; }

signals:
    void closeRequested();
    void saved();
    void profileChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void buildUi();
    void wire();
    void loadContent();
    void saveContent();
    void revertContent();
    void commitName();
    void updateModifiedState();
    void updateCursorStatus(int line, int column);
    void showFindBar(bool replace);
    void hideFindBar();
    void findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void setFeedback(const QString& text, bool error = false);

    ClashController* m_controller = nullptr;
    cq::ProfileMeta m_profile;
    QString m_filePath;

    CodeEditor* m_editor = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    M3Button* m_saveButton = nullptr;
    M3Button* m_revertButton = nullptr;
    M3Pill* m_modifiedPill = nullptr;
    QLabel* m_fileName = nullptr;
    QLabel* m_feedback = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_issueLabel = nullptr;
    QString m_originalText;
    bool m_readError = false;
};
