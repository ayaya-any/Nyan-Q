#include "lang/JavaScriptLanguageProvider.h"

#include "quickjs.h"

#include <QRegularExpression>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace nq::lang {
namespace {

QString jsString(JSContext* ctx, JSValueConst value)
{
    size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, value);
    if (!text)
        return {};
    const QString result = QString::fromUtf8(text, static_cast<int>(length));
    JS_FreeCString(ctx, text);
    return result;
}

QString propertyString(JSContext* ctx, JSValueConst object, const char* name)
{
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return {};
    }
    const QString text = jsString(ctx, value);
    JS_FreeValue(ctx, value);
    return text;
}

struct JsError {
    QString name;
    QString message;
    QString stack;
    int line = 0;
    int column = 0;
};

void parseLocation(const QString& text, int* line, int* column)
{
    static const QRegularExpression pattern(QStringLiteral(":(\\d+):(\\d+)"));
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return;
    const int parsedLine = match.captured(1).toInt();
    const int parsedColumn = match.captured(2).toInt();
    if (parsedLine > 0)
        *line = parsedLine;
    if (parsedColumn > 0)
        *column = parsedColumn;
}

JsError describeValue(JSContext* ctx, JSValueConst value)
{
    JsError error;
    if (JS_IsObject(value)) {
        error.name = propertyString(ctx, value, "name");
        error.message = propertyString(ctx, value, "message");
        error.stack = propertyString(ctx, value, "stack");
        if (error.message.isEmpty()) {
            const QString text = jsString(ctx, value);
            if (!text.isEmpty() && text != QStringLiteral("[object Object]"))
                error.message = text;
        }
    } else if (!JS_IsUndefined(value) && !JS_IsNull(value)) {
        error.message = jsString(ctx, value);
    }
    if (error.name.isEmpty())
        error.name = QStringLiteral("SyntaxError");
    if (error.message.isEmpty())
        error.message = QStringLiteral("JavaScript 语法错误");
    parseLocation(error.stack, &error.line, &error.column);
    if (error.line == 0)
        parseLocation(error.message, &error.line, &error.column);
    return error;
}

JsError takeException(JSContext* ctx)
{
    JSValue exception = JS_GetException(ctx);
    const JsError error = describeValue(ctx, exception);
    JS_FreeValue(ctx, exception);
    return error;
}

QStringList configKeys()
{
    return {
        QStringLiteral("mixed-port"), QStringLiteral("port"),
        QStringLiteral("socks-port"), QStringLiteral("allow-lan"),
        QStringLiteral("bind-address"), QStringLiteral("mode"),
        QStringLiteral("log-level"), QStringLiteral("ipv6"),
        QStringLiteral("external-controller"), QStringLiteral("secret"),
        QStringLiteral("unified-delay"), QStringLiteral("tcp-concurrent"),
        QStringLiteral("find-process-mode"), QStringLiteral("proxies"),
        QStringLiteral("proxy-groups"), QStringLiteral("proxy-providers"),
        QStringLiteral("rule-providers"), QStringLiteral("rules"),
        QStringLiteral("dns"), QStringLiteral("tun"), QStringLiteral("sniffer"),
        QStringLiteral("hosts"), QStringLiteral("profile"),
        QStringLiteral("experimental"), QStringLiteral("ntp"),
        QStringLiteral("listeners"), QStringLiteral("authentication"),
    };
}

QStringList dnsKeys()
{
    return {
        QStringLiteral("enable"), QStringLiteral("ipv6"), QStringLiteral("listen"),
        QStringLiteral("enhanced-mode"), QStringLiteral("fake-ip-range"),
        QStringLiteral("fake-ip-filter"), QStringLiteral("default-nameserver"),
        QStringLiteral("nameserver"), QStringLiteral("fallback"),
        QStringLiteral("fallback-filter"), QStringLiteral("nameserver-policy"),
        QStringLiteral("proxy-server-nameserver"), QStringLiteral("respect-rules"),
        QStringLiteral("use-hosts"), QStringLiteral("use-system-hosts"),
    };
}

QVector<CompletionItem> propertyItems(const QStringList& keys, const QString& prefix,
                                      int priority, bool identifierOnly = false)
{
    QVector<CompletionItem> items;
    for (const QString& key : keys) {
        if (identifierOnly && key.contains(QLatin1Char('-')))
            continue;
        if (!prefix.isEmpty() && !key.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        CompletionItem item;
        item.label = key;
        item.insertText = key;
        item.detail = QStringLiteral("Mihomo 配置字段");
        item.kind = CompletionKind::Property;
        item.priority = priority;
        items.append(item);
    }
    return items;
}

CompletionItem itemFor(const QString& label, const QString& insert, const QString& detail,
                       CompletionKind kind, int priority, int cursorOffset = -1)
{
    CompletionItem item;
    item.label = label;
    item.insertText = insert;
    item.detail = detail;
    item.kind = kind;
    item.priority = priority;
    item.cursorOffset = cursorOffset;
    return item;
}

class JavaScriptHighlighter final : public QSyntaxHighlighter {
public:
    JavaScriptHighlighter(QTextDocument* document, const LanguageTheme& theme)
        : QSyntaxHighlighter(document)
        , m_commentColor(theme.comment)
    {
        m_keyword.setForeground(theme.keyword);
        m_keyword.setFontWeight(QFont::DemiBold);
        m_string.setForeground(theme.string);
        m_number.setForeground(theme.number);
        m_comment.setForeground(theme.comment);
        m_comment.setFontItalic(true);
        m_punctuation.setForeground(theme.punctuation);
        m_function.setForeground(theme.function);
    }

protected:
    void highlightBlock(const QString& text) override
    {
        static const QRegularExpression keywordPattern(
            QStringLiteral("\\b(?:function|const|let|var|return|if|else|for|while|do|switch|"
                           "case|break|continue|new|typeof|instanceof|in|of|class|extends|"
                           "super|this|try|catch|finally|throw|async|await|yield|delete|void|"
                           "true|false|null|undefined)\\b"));
        static const QRegularExpression numberPattern(
            QStringLiteral("\\b(?:0[xX][0-9a-fA-F]+|\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)\\b"));
        static const QRegularExpression functionPattern(
            QStringLiteral("\\b[A-Za-z_$][A-Za-z0-9_$]*(?=\\s*\\()"));
        static const QRegularExpression punctuationPattern(
            QStringLiteral("[{}\\[\\]();,]"));
        static const QRegularExpression stringPattern(
            QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'|`(?:[^`\\\\]|\\\\.)*`"));

        m_comment.setForeground(m_commentColor);

        int state = previousBlockState();
        int index = 0;
        if (state == 1) {
            const int end = text.indexOf(QStringLiteral("*/"));
            if (end < 0) {
                setFormat(0, text.size(), m_comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(0, end + 2, m_comment);
            index = end + 2;
            state = 0;
        }

        QVector<bool> masked(text.size(), false);
        auto strings = stringPattern.globalMatch(text, index);
        while (strings.hasNext()) {
            const auto match = strings.next();
            setFormat(match.capturedStart(), match.capturedLength(), m_string);
            for (int i = match.capturedStart();
                 i < match.capturedStart() + match.capturedLength() && i < masked.size(); ++i)
                masked[i] = true;
        }

        const auto apply = [&](const QRegularExpression& pattern, const QTextCharFormat& format) {
            auto matches = pattern.globalMatch(text, index);
            while (matches.hasNext()) {
                const auto match = matches.next();
                if (match.capturedStart() < masked.size() && masked.at(match.capturedStart()))
                    continue;
                setFormat(match.capturedStart(), match.capturedLength(), format);
            }
        };
        apply(keywordPattern, m_keyword);
        apply(numberPattern, m_number);
        apply(functionPattern, m_function);
        apply(punctuationPattern, m_punctuation);

        bool single = false;
        bool doubleQuote = false;
        bool templateLiteral = false;
        for (int i = index; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('\\')) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('\'') && !doubleQuote && !templateLiteral)
                single = !single;
            else if (ch == QLatin1Char('"') && !single && !templateLiteral)
                doubleQuote = !doubleQuote;
            else if (ch == QLatin1Char('`') && !single && !doubleQuote)
                templateLiteral = !templateLiteral;
            if (single || doubleQuote || templateLiteral)
                continue;
            if (ch == QLatin1Char('/') && i + 1 < text.size()) {
                if (text.at(i + 1) == QLatin1Char('/')) {
                    setFormat(i, text.size() - i, m_comment);
                    break;
                }
                if (text.at(i + 1) == QLatin1Char('*')) {
                    const int end = text.indexOf(QStringLiteral("*/"), i + 2);
                    if (end < 0) {
                        setFormat(i, text.size() - i, m_comment);
                        setCurrentBlockState(1);
                    } else {
                        setFormat(i, end + 2 - i, m_comment);
                        i = end + 1;
                    }
                }
            }
        }
    }

private:
    QColor m_commentColor;
    QTextCharFormat m_keyword;
    QTextCharFormat m_string;
    QTextCharFormat m_number;
    QTextCharFormat m_comment;
    QTextCharFormat m_punctuation;
    QTextCharFormat m_function;
};

}  // namespace

QVector<Diagnostic> JavaScriptLanguageProvider::diagnose(const QString& text,
                                                         const QString& filePath) const
{
    Q_UNUSED(filePath)
    QVector<Diagnostic> diagnostics;
    if (text.trimmed().isEmpty())
        return diagnostics;

    if (text.size() > 1024 * 1024) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.message = QStringLiteral("脚本超过 1 MB，已跳过实时语法检查");
        diagnostic.source = QStringLiteral("JavaScript");
        return {diagnostic};
    }

    JSRuntime* runtime = JS_NewRuntime();
    if (!runtime)
        return diagnostics;
    JS_SetMemoryLimit(runtime, 128LL * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 512 * 1024);
    JSContext* context = JS_NewContext(runtime);
    if (!context) {
        JS_FreeRuntime(runtime);
        return diagnostics;
    }

    const QByteArray source = text.toUtf8();
    JSValue compiled = JS_Eval(context, source.constData(), static_cast<size_t>(source.size()),
                               "<editor>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        const JsError error = takeException(context);
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.line = qMax(0, error.line - 1);
        diagnostic.column = qMax(0, error.column - 1);
        diagnostic.message = error.message;
        diagnostic.source = QStringLiteral("JavaScript");
        diagnostic.code = error.name;
        diagnostics.append(diagnostic);
    } else {
        JS_FreeValue(context, compiled);
    }
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);

    static const QRegularExpression mainPattern(
        QStringLiteral("(?:function\\s+main\\b|(?:const|let|var)\\s+main\\s*=|"
                       "main\\s*=\\s*(?:async\\s*)?(?:function|\\())"));
    if (diagnostics.isEmpty() && !mainPattern.match(text).hasMatch()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.line = 0;
        diagnostic.column = 0;
        diagnostic.message =
            QStringLiteral("脚本没有定义 main(config, profileName)，运行时会被拒绝");
        diagnostic.source = QStringLiteral("JavaScript");
        diagnostics.append(diagnostic);
    }
    return diagnostics;
}

QVector<CompletionItem> JavaScriptLanguageProvider::completions(
    const CompletionRequest& request) const
{
    const QString& text = request.text;
    const int position = qBound(0, request.position, text.size());
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
    const QString before = text.mid(lineStart, position - lineStart);

    static const QRegularExpression wordPattern(QStringLiteral("([A-Za-z0-9_$]*)$"));
    const auto wordMatch = wordPattern.match(before);
    const QString prefix = wordMatch.hasMatch() ? wordMatch.captured(1) : QString();

    static const QRegularExpression dnsAccess(
        QStringLiteral("\\bconfig\\s*\\.\\s*dns\\s*\\.\\s*([A-Za-z0-9_-]*)$"));
    const auto dnsMatch = dnsAccess.match(before);
    if (dnsMatch.hasMatch())
        return propertyItems(dnsKeys(), dnsMatch.captured(1), 5, true);

    static const QRegularExpression dnsBracketAccess(
        QStringLiteral("\\bconfig\\s*\\.\\s*dns\\s*\\[\\s*([\"'])([^\"']*)$"));
    const auto dnsBracketMatch = dnsBracketAccess.match(before);
    if (dnsBracketMatch.hasMatch()) {
        const QString quote = dnsBracketMatch.captured(1);
        QVector<CompletionItem> items;
        for (const QString& key : dnsKeys()) {
            if (!dnsBracketMatch.captured(2).isEmpty() &&
                !key.startsWith(dnsBracketMatch.captured(2), Qt::CaseInsensitive))
                continue;
            CompletionItem item;
            item.label = key;
            item.insertText = key + quote + QStringLiteral("]");
            item.detail = QStringLiteral("DNS 字段");
            item.kind = CompletionKind::Property;
            item.priority = 5;
            items.append(item);
        }
        return items;
    }

    static const QRegularExpression configAccess(
        QStringLiteral("\\bconfig\\s*\\.\\s*([A-Za-z0-9_-]*)$"));
    const auto configMatch = configAccess.match(before);
    if (configMatch.hasMatch())
        return propertyItems(configKeys(), configMatch.captured(1), 10, true);

    static const QRegularExpression bracketAccess(
        QStringLiteral("\\bconfig\\s*\\[\\s*([\"'])([^\"']*)$"));
    const auto bracketMatch = bracketAccess.match(before);
    if (bracketMatch.hasMatch()) {
        const QString quote = bracketMatch.captured(1);
        QVector<CompletionItem> items;
        for (const QString& key : configKeys()) {
            if (!bracketMatch.captured(2).isEmpty() &&
                !key.startsWith(bracketMatch.captured(2), Qt::CaseInsensitive))
                continue;
            CompletionItem item;
            item.label = key;
            item.insertText = key + quote + QStringLiteral("]");
            item.detail = QStringLiteral("Mihomo 配置字段");
            item.kind = CompletionKind::Property;
            item.priority = 5;
            items.append(item);
        }
        return items;
    }

    static const QRegularExpression consoleAccess(
        QStringLiteral("\\bconsole\\s*\\.\\s*([A-Za-z0-9_]*)$"));
    const auto consoleMatch = consoleAccess.match(before);
    if (consoleMatch.hasMatch()) {
        const QStringList methods = {QStringLiteral("log"), QStringLiteral("info"),
                                     QStringLiteral("warn"), QStringLiteral("error"),
                                     QStringLiteral("debug")};
        return propertyItems(methods, consoleMatch.captured(1), 5);
    }

    QVector<CompletionItem> items;
    const QString mainSnippet =
        QStringLiteral("function main(config, profileName) {\n    \n    return config;\n}");
    const int mainCursor = mainSnippet.indexOf(QLatin1Char('\n')) + 5;
    if (prefix.isEmpty() || QStringLiteral("main").startsWith(prefix, Qt::CaseInsensitive)) {
        items.append(itemFor(QStringLiteral("main"), mainSnippet,
                             QStringLiteral("配置覆写入口函数"), CompletionKind::Snippet, 1,
                             mainCursor));
    }
    if (prefix.isEmpty() || QStringLiteral("async main").startsWith(prefix, Qt::CaseInsensitive)) {
        const QString asyncSnippet =
            QStringLiteral("async function main(config, profileName) {\n    \n    return config;\n}");
        const int asyncCursor = asyncSnippet.indexOf(QLatin1Char('\n')) + 5;
        items.append(itemFor(QStringLiteral("async main"), asyncSnippet,
                             QStringLiteral("异步配置覆写入口"), CompletionKind::Snippet, 2,
                             asyncCursor));
    }

    const QStringList keywords = {
        QStringLiteral("function"), QStringLiteral("const"), QStringLiteral("let"),
        QStringLiteral("var"), QStringLiteral("return"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("do"), QStringLiteral("switch"), QStringLiteral("case"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("new"),
        QStringLiteral("typeof"), QStringLiteral("instanceof"), QStringLiteral("in"),
        QStringLiteral("of"), QStringLiteral("class"), QStringLiteral("extends"),
        QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("finally"),
        QStringLiteral("throw"), QStringLiteral("async"), QStringLiteral("await"),
        QStringLiteral("delete"), QStringLiteral("void"), QStringLiteral("this"),
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null"),
        QStringLiteral("undefined"),
    };
    for (const QString& keyword : keywords) {
        if (!prefix.isEmpty() && !keyword.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        items.append(itemFor(keyword, keyword, QStringLiteral("JavaScript 关键字"),
                             CompletionKind::Keyword, 30));
    }

    const QStringList globals = {
        QStringLiteral("config"), QStringLiteral("profileName"), QStringLiteral("console"),
        QStringLiteral("JSON"), QStringLiteral("Object"), QStringLiteral("Array"),
        QStringLiteral("String"), QStringLiteral("Number"), QStringLiteral("Boolean"),
        QStringLiteral("Math"), QStringLiteral("Date"), QStringLiteral("Promise"),
        QStringLiteral("Map"), QStringLiteral("Set"),
    };
    for (const QString& global : globals) {
        if (!prefix.isEmpty() && !global.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        items.append(itemFor(global, global, QStringLiteral("JavaScript 全局对象"),
                             CompletionKind::Text, 40));
    }

    const QStringList consoleMethods = {QStringLiteral("log"), QStringLiteral("warn"),
                                        QStringLiteral("error")};
    for (const QString& method : consoleMethods) {
        const QString label = QStringLiteral("console.") + method;
        if (!prefix.isEmpty() && !label.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        const QString insert = QStringLiteral("console.%1()").arg(method);
        items.append(itemFor(label, insert, QStringLiteral("脚本日志输出"),
                             CompletionKind::Function, 20, insert.size() - 1));
    }

    std::stable_sort(items.begin(), items.end(), [](const CompletionItem& a,
                                                     const CompletionItem& b) {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return QString::localeAwareCompare(a.label, b.label) < 0;
    });
    return items;
}

QSyntaxHighlighter* JavaScriptLanguageProvider::createHighlighter(
    QTextDocument* document, const LanguageTheme& theme) const
{
    return new JavaScriptHighlighter(document, theme);
}

}  // namespace nq::lang
