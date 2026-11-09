#pragma once

#include "lang/LanguageProvider.h"

namespace nq::lang {

class YamlLanguageProvider final : public LanguageProvider {
public:
    QString id() const override { return QStringLiteral("yaml"); }
    QString displayName() const override { return QStringLiteral("YAML"); }
    QStringList fileExtensions() const override
    {
        return {QStringLiteral("yaml"), QStringLiteral("yml")};
    }

    QVector<Diagnostic> diagnose(const QString& text, const QString& filePath) const override;
    QVector<CompletionItem> completions(const CompletionRequest& request) const override;
    QSyntaxHighlighter* createHighlighter(QTextDocument* document,
                                          const LanguageTheme& theme) const override;
    QString lineCommentPrefix() const override { return QStringLiteral("# "); }
};

}  // namespace nq::lang
