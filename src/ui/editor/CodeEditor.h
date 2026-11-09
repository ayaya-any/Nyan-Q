#pragma once

#include "lsp/LspTypes.h"

#include <QPlainTextEdit>

class QListWidget;
class QSyntaxHighlighter;
class QTextCursor;
class QTextDocument;
class LineNumberArea;

class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT

public:
    enum class SyntaxMode { Yaml, JavaScript };

    explicit CodeEditor(QWidget* parent = nullptr);

    void setSyntaxMode(SyntaxMode mode);
    SyntaxMode syntaxMode() const { return m_syntaxMode; }

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int currentLine() const;
    int currentColumn() const;

    void showCompletions(const QVector<LspCompletion>& completions);
    void setDiagnostics(const QVector<LspDiagnostic>& diagnostics);
    void applyLspEdits(const QVector<LspTextEdit>& edits);

signals:
    void cursorInfoChanged(int line, int column);
    void searchRequested();
    void replaceRequested();
    void formatRequested();
    void completionRequested(int line, int character);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateLineNumberAreaWidth(int newBlockCount = 0);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void updateEditorPalette();
    void hideCompletions();
    void acceptCompletion();
    void moveCurrentLine(int delta);
    void copyCurrentLine(int delta);
    void deleteCurrentLine();
    void insertLine(bool below);
    void toggleComment();
    void indentSelection(bool outdent);
    void autoIndentNewline();
    static QTextCursor cursorForPosition(QTextDocument* document, const LspPosition& position);

    LineNumberArea* m_lineNumberArea = nullptr;
    QListWidget* m_completionPopup = nullptr;
    QSyntaxHighlighter* m_highlighter = nullptr;
    QVector<LspDiagnostic> m_diagnostics;
    SyntaxMode m_syntaxMode = SyntaxMode::Yaml;
};
