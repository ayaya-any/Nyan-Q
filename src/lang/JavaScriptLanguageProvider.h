#pragma once

#include "lang/LanguageProvider.h"

namespace nq::lang {

class JavaScriptLanguageProvider final : public LanguageProvider {
public:
    QString id() const override { return QStringLiteral("javascript"); }
    QString displayName() const override { return QStringLiteral("JavaScript"); }
    QStringList fileExtensions() const override
    {
        return {QStringLiteral("js"), QStringLiteral("mjs"), QStringLiteral("cjs")};
    }

    QVector<Diagnostic> diagnose(const QString& text, const QString& filePath) const override;
    QVector<CompletionItem> completions(const CompletionRequest& request) const override;
    QSyntaxHighlighter* createHighlighter(QTextDocument* document,
                                          const LanguageTheme& theme) const override;
    QString lineCommentPrefix() const override { return QStringLiteral("// "); }
};

}  // namespace nq::lang
