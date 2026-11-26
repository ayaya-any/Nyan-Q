#pragma once

#include "lang/LanguageTypes.h"

namespace nq::lang {

// Formatting is intentionally done inside the language plugins (no external
// process): YAML re-indents block structures while preserving comments and
// block scalars, JavaScript re-indents code while leaving strings, template
// literals and comments untouched.
class DocumentFormatter final {
public:
    static FormatResult formatYaml(const QString& text);
    static FormatResult formatJavaScript(const QString& text);
};

}  // namespace nq::lang
