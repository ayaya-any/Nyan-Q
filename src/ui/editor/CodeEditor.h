#pragma once

#include "lang/LanguageTypes.h"

#include <QPlainTextEdit>

class QListWidget;
class QSyntaxHighlighter;
class QTimer;
class QTextCursor;
class QTextDocument;
class LineNumberArea;

namespace nq::lang {
class LanguageProvider;
}

class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setLanguageProvider(const nq::lang::LanguageProvider* provider);
    const nq::lang::LanguageProvider* languageProvider() const { return m_provider; }
    void setDocumentPath(const QString& path) { m_documentPath = path; }

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int currentLine() const;
    int currentColumn() const;

    void setDiagnostics(const QVector<nq::lang::Diagnostic>& diagnostics);
    const QVector<nq::lang::Diagnostic>& diagnostics() const { return m_diagnostics; }

signals:
    void cursorInfoChanged(int line, int column);
    void searchRequested();
    void replaceRequested();
    void diagnosticsChanged(int errors, int warnings);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateLineNumberAreaWidth(int newBlockCount = 0);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void updateEditorPalette();
    void rebuildHighlighter();
    void runDiagnostics();
    void requestCompletions();
    void showCompletions(const QVector<nq::lang::CompletionItem>& completions);
    void hideCompletions();
    void acceptCompletion();
    void moveCurrentLine(int delta);
    void copyCurrentLine(int delta);
    void deleteCurrentLine();
    void insertLine(bool below);
    void toggleComment();
    void indentSelection(bool outdent);
    void autoIndentNewline();
    QTextCursor cursorForDiagnostic(const nq::lang::Diagnostic& diagnostic) const;

    LineNumberArea* m_lineNumberArea = nullptr;
    QListWidget* m_completionPopup = nullptr;
    QSyntaxHighlighter* m_highlighter = nullptr;
    QTimer* m_diagnosticTimer = nullptr;
    const nq::lang::LanguageProvider* m_provider = nullptr;
    QString m_documentPath;
    QVector<nq::lang::Diagnostic> m_diagnostics;
};
