#pragma once

#include "lang/LanguageTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QSyntaxHighlighter;
class QTextDocument;

namespace nq::lang {

// A language plugin owns everything language-specific: diagnostics,
// completion and syntax highlighting. The editor only talks to this interface,
// so adding another language is a provider plus one registry entry.
class LanguageProvider {
public:
    virtual ~LanguageProvider() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList fileExtensions() const = 0;

    virtual QVector<Diagnostic> diagnose(const QString& text,
                                         const QString& filePath) const = 0;
    virtual QVector<CompletionItem> completions(const CompletionRequest& request) const = 0;

    // Whole-document formatting is optional. Plugins that return true here
    // must return a formatted document or an actionable error from format().
    virtual bool supportsFormatting() const { return false; }
    virtual FormatResult format(const QString& text, const QString& filePath) const
    {
        Q_UNUSED(text)
        Q_UNUSED(filePath)
        FormatResult result;
        result.error = QStringLiteral("当前语言不支持格式化");
        return result;
    }

    virtual QSyntaxHighlighter* createHighlighter(QTextDocument* document,
                                                  const LanguageTheme& theme) const = 0;

    virtual QString lineCommentPrefix() const = 0;
};

}  // namespace nq::lang
