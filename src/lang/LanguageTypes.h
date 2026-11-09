#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace nq::lang {

enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    int line = 0;    // 0-based
    int column = 0;  // 0-based
    int length = 1;
    QString message;
    QString source;
    QString code;
};

enum class CompletionKind { Text, Keyword, Property, Value, Function, Snippet };

struct CompletionItem {
    QString label;
    QString insertText;
    QString detail;
    QString documentation;
    CompletionKind kind = CompletionKind::Text;
    // Lower values are shown first.
    int priority = 100;
    // Relative cursor position after insertion; -1 leaves the cursor at the end.
    int cursorOffset = -1;
};

struct CompletionRequest {
    QString text;
    int position = 0;  // cursor position in text
    QString filePath;
};

struct LanguageTheme {
    QColor keyword;
    QColor string;
    QColor number;
    QColor comment;
    QColor punctuation;
    QColor function;
    bool dark = true;
};

}  // namespace nq::lang
