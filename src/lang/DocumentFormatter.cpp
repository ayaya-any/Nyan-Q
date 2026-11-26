#include "lang/DocumentFormatter.h"

#include "yaml.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <vector>

namespace nq::lang {
namespace {

// ---------------------------------------------------------------------------
// YAML
// ---------------------------------------------------------------------------

struct YamlSemanticToken {
    int type = 0;
    QString value;
    QString tag;
    QString anchor;

    bool operator==(const YamlSemanticToken& other) const
    {
        return type == other.type && value == other.value && tag == other.tag &&
               anchor == other.anchor;
    }
};

struct YamlFrame {
    bool mapping = true;
    bool expectKey = true;
};

struct YamlAnalysis {
    bool ok = false;
    QString error;
    QVector<int> lineLevel;
    QVector<bool> protectedLine;
    QVector<int> protectedOwner;  // event line that owns a protected scalar block
    QVector<YamlSemanticToken> tokens;
    int documents = 0;
};

QString yamlText(const yaml_char_t* text)
{
    if (!text)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char*>(text));
}

bool isDashLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('-')) &&
           (trimmed.size() == 1 || trimmed.at(1).isSpace());
}

bool analyzeYaml(const QByteArray& data, const QStringList& sourceLines, YamlAnalysis* analysis,
                 QString* error)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        *error = QStringLiteral("无法初始化 YAML 解析器");
        return false;
    }
    yaml_parser_set_input_string(&parser,
                                 reinterpret_cast<const unsigned char*>(data.constData()),
                                 static_cast<size_t>(data.size()));
    const int lineCount = sourceLines.size();
    analysis->lineLevel = QVector<int>(lineCount, -1);
    analysis->protectedLine = QVector<bool>(lineCount, false);
    analysis->protectedOwner = QVector<int>(lineCount, -1);

    QVector<YamlFrame> frames;
    bool done = false;
    bool parseFailed = false;
    while (!done) {
        yaml_event_t event;
        if (!yaml_parser_parse(&parser, &event)) {
            parseFailed = true;
            break;
        }
        const int line = static_cast<int>(event.start_mark.line);
        const int column = static_cast<int>(event.start_mark.column);

        if (event.type != YAML_STREAM_START_EVENT && event.type != YAML_STREAM_END_EVENT) {
            YamlSemanticToken token;
            token.type = static_cast<int>(event.type);
            switch (event.type) {
            case YAML_SCALAR_EVENT:
                token.value = QString::fromUtf8(
                    reinterpret_cast<const char*>(event.data.scalar.value),
                    static_cast<int>(event.data.scalar.length));
                token.tag = yamlText(event.data.scalar.tag);
                token.anchor = yamlText(event.data.scalar.anchor);
                break;
            case YAML_ALIAS_EVENT:
                token.anchor = yamlText(event.data.alias.anchor);
                break;
            case YAML_MAPPING_START_EVENT:
                token.tag = yamlText(event.data.mapping_start.tag);
                token.anchor = yamlText(event.data.mapping_start.anchor);
                break;
            case YAML_SEQUENCE_START_EVENT:
                token.tag = yamlText(event.data.sequence_start.tag);
                token.anchor = yamlText(event.data.sequence_start.anchor);
                break;
            default:
                break;
            }
            analysis->tokens.append(token);
        }

        switch (event.type) {
        case YAML_STREAM_END_EVENT:
            done = true;
            break;
        case YAML_DOCUMENT_START_EVENT:
        case YAML_DOCUMENT_END_EVENT:
            if (event.type == YAML_DOCUMENT_START_EVENT)
                ++analysis->documents;
            break;
        case YAML_MAPPING_START_EVENT:
        case YAML_SEQUENCE_START_EVENT: {
            if (line >= 0 && line < analysis->lineLevel.size() &&
                analysis->lineLevel.at(line) < 0) {
                int level = 0;
                if (!frames.isEmpty()) {
                    const bool sequenceItem =
                        !frames.last().mapping && isDashLine(sourceLines.value(line));
                    level = sequenceItem ? frames.size() - 1 : frames.size();
                }
                analysis->lineLevel[line] = qMax(0, level);
            }
            if (!frames.isEmpty() && frames.last().mapping)
                frames.last().expectKey = true;
            YamlFrame frame;
            frame.mapping = event.type == YAML_MAPPING_START_EVENT;
            frame.expectKey = frame.mapping;
            frames.append(frame);
            break;
        }
        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT:
            if (!frames.isEmpty())
                frames.removeLast();
            break;
        case YAML_SCALAR_EVENT:
        case YAML_ALIAS_EVENT: {
            if (line >= 0 && line < analysis->lineLevel.size() &&
                analysis->lineLevel.at(line) < 0) {
                int level = 0;
                if (!frames.isEmpty()) {
                    const bool sequenceItem =
                        !frames.last().mapping && isDashLine(sourceLines.value(line));
                    if (frames.last().mapping)
                        level = frames.last().expectKey ? qMax(0, frames.size() - 1)
                                                        : frames.size();
                    else
                        level = sequenceItem ? frames.size() - 1 : frames.size();
                }
                analysis->lineLevel[line] = qMax(0, level);
            }
            if (!frames.isEmpty() && frames.last().mapping)
                frames.last().expectKey = !frames.last().expectKey;
            if (event.type == YAML_SCALAR_EVENT) {
                const int endLine = static_cast<int>(event.end_mark.line);
                for (int protect = line + 1;
                     protect <= endLine && protect < analysis->protectedLine.size();
                     ++protect) {
                    analysis->protectedLine[protect] = true;
                    analysis->protectedOwner[protect] = line;
                }
            }
            Q_UNUSED(column)
            break;
        }
        default:
            break;
        }
        yaml_event_delete(&event);
    }

    if (parseFailed) {
        QString problem = parser.problem ? QString::fromUtf8(parser.problem)
                                         : QStringLiteral("无法解析 YAML");
        *error = QStringLiteral("YAML 语法错误（行 %1）：%2")
                     .arg(static_cast<int>(parser.problem_mark.line) + 1)
                     .arg(problem);
        yaml_parser_delete(&parser);
        return false;
    }
    yaml_parser_delete(&parser);
    if (analysis->documents > 1) {
        *error = QStringLiteral("编辑器只支持单个 YAML 文档，已取消格式化");
        return false;
    }
    analysis->ok = true;
    return true;
}

bool sameSemantics(const QVector<YamlSemanticToken>& left,
                   const QVector<YamlSemanticToken>& right)
{
    if (left.size() != right.size())
        return false;
    for (int i = 0; i < left.size(); ++i) {
        if (!(left.at(i) == right.at(i)))
            return false;
    }
    return true;
}

}  // namespace

FormatResult DocumentFormatter::formatYaml(const QString& text)
{
    FormatResult result;
    QString source = text;
    if (source.startsWith(QChar(0xFEFF)))
        source.remove(0, 1);
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (source.trimmed().isEmpty()) {
        result.ok = true;
        result.text = source.isEmpty() ? QString() : QStringLiteral("\n");
        return result;
    }

    QStringList lines = source.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QByteArray data = source.toUtf8();
    YamlAnalysis before;
    QString error;
    if (!analyzeYaml(data, lines, &before, &error)) {
        result.error = error;
        return result;
    }

    QStringList output;
    output.reserve(lines.size());
    QVector<bool> outputProtected;
    outputProtected.reserve(lines.size());
    QVector<int> outputLevel;
    outputLevel.reserve(lines.size());
    QVector<int> pendingComments;
    QStringList pendingCommentText;
    bool lastBlank = false;
    bool wroteContent = false;
    const auto leadingIndent = [](const QString& line) {
        int count = 0;
        while (count < line.size() &&
               (line.at(count) == QLatin1Char(' ') || line.at(count) == QLatin1Char('\t')))
            ++count;
        return count;
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString raw = lines.at(i);
        if (before.protectedLine.value(i)) {
            // Block scalars and multi-line scalars belong to an owner line.
            // When the owner is re-indented, shift continuation lines by the
            // same delta so the scalar content keeps its relative indentation.
            QString printed = raw;
            const int owner = before.protectedOwner.value(i, -1);
            if (owner >= 0 && owner < lines.size() && !raw.trimmed().isEmpty()) {
                const int level = before.lineLevel.value(owner);
                if (level >= 0) {
                    const int delta = level * 2 - leadingIndent(lines.at(owner));
                    if (delta > 0) {
                        printed = QString(delta, QLatin1Char(' ')) + raw;
                    } else if (delta < 0) {
                        const int remove = qMin(-delta, leadingIndent(raw));
                        printed = raw.mid(remove);
                    }
                }
            }
            output.append(printed);
            outputProtected.append(true);
            outputLevel.append(-1);
            lastBlank = printed.trimmed().isEmpty();
            wroteContent = true;
            continue;
        }
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) {
            if (wroteContent && !lastBlank) {
                output.append(QString());
                outputProtected.append(false);
                outputLevel.append(-1);
            }
            lastBlank = true;
            continue;
        }
        lastBlank = false;
        wroteContent = true;

        if (before.lineLevel.value(i) < 0) {
            if (trimmed.startsWith(QLatin1Char('#'))) {
                pendingComments.append(output.size());
                pendingCommentText.append(trimmed);
                output.append(QString());
                outputProtected.append(false);
                outputLevel.append(-2);
            } else {
                output.append(raw);
                outputProtected.append(true);
                outputLevel.append(-1);
            }
            continue;
        }

        const int level = before.lineLevel.at(i);
        const int first = static_cast<int>(raw.size());
        int firstNonSpace = 0;
        while (firstNonSpace < first &&
               (raw.at(firstNonSpace) == QLatin1Char(' ') ||
                raw.at(firstNonSpace) == QLatin1Char('\t')))
            ++firstNonSpace;        int lastNonSpace = first - 1;
        while (lastNonSpace >= firstNonSpace &&
               (raw.at(lastNonSpace) == QLatin1Char(' ') ||
                raw.at(lastNonSpace) == QLatin1Char('\t')))
            --lastNonSpace;
        if (lastNonSpace < firstNonSpace) {
            output.append(QString());
            outputProtected.append(false);
            outputLevel.append(-1);
            continue;
        }
        const QString content = raw.mid(firstNonSpace, lastNonSpace - firstNonSpace + 1);
        output.append(QString(level * 2, QLatin1Char(' ')) + content);
        outputProtected.append(false);
        outputLevel.append(level);
    }

    // Align comment-only lines with the nearest following (or preceding)
    // formatted line; comments are not part of the YAML event stream.
    for (int c = 0; c < pendingComments.size(); ++c) {
        const int index = pendingComments.at(c);
        int level = -1;
        for (int scan = index + 1; scan < outputLevel.size(); ++scan) {
            if (outputLevel.at(scan) >= 0) {
                level = outputLevel.at(scan);
                break;
            }
        }
        if (level < 0) {
            for (int scan = index - 1; scan >= 0; --scan) {
                if (outputLevel.at(scan) >= 0) {
                    level = outputLevel.at(scan);
                    break;
                }
            }
        }
        if (level < 0)
            level = 0;
        output[index] = QString(level * 2, QLatin1Char(' ')) + pendingCommentText.at(c);
    }

    // Drop trailing blank lines that we synthesized (protected block scalar
    // blanks are never removed).
    while (!output.isEmpty() && output.last().isEmpty() &&
           !outputProtected.value(output.size() - 1))
        output.removeLast();

    QString formatted = output.join(QLatin1Char('\n'));
    if (!formatted.isEmpty())
        formatted += QLatin1Char('\n');

    QStringList formattedLines = formatted.split(QLatin1Char('\n'));
    if (!formattedLines.isEmpty() && formattedLines.last().isEmpty())
        formattedLines.removeLast();
    YamlAnalysis after;
    if (!analyzeYaml(formatted.toUtf8(), formattedLines, &after, &error)) {
        result.error = error.isEmpty() ? QStringLiteral("格式化结果无法解析，已取消")
                                       : QStringLiteral("格式化结果无法解析，已取消：%1").arg(error);
        return result;
    }
    if (!sameSemantics(before.tokens, after.tokens)) {
        result.error = QStringLiteral(
            "格式化会改变 YAML 结构（例如多行标量/块标量的缩进依赖），已取消。"
            "请手动检查该文件后重试");
        return result;
    }
    result.ok = true;
    result.text = formatted;
    return result;
}

// ---------------------------------------------------------------------------
// JavaScript (indentation only)
// ---------------------------------------------------------------------------

namespace {

enum class JsMode {
    Code,
    SingleQuote,
    DoubleQuote,
    Template,
    LineComment,
    BlockComment,
    Regex,
};

struct JsState {
    JsMode mode = JsMode::Code;
    int depth = 0;
    std::vector<bool> braceIsTemplateExpression;
    bool regexAllowed = true;
    bool stringContinued = false;
};

bool isIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_') || ch == QLatin1Char('$') || ch == QLatin1Char('\\');
}

bool isIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

bool keywordAllowsRegex(const QString& word)
{
    static const QStringList keywords = {
        QStringLiteral("return"),  QStringLiteral("typeof"),  QStringLiteral("instanceof"),
        QStringLiteral("in"),      QStringLiteral("of"),      QStringLiteral("new"),
        QStringLiteral("delete"),  QStringLiteral("void"),    QStringLiteral("throw"),
        QStringLiteral("case"),    QStringLiteral("do"),      QStringLiteral("else"),
        QStringLiteral("yield"),   QStringLiteral("await"),
    };
    return keywords.contains(word);
}

void scanRegex(const QString& line, int* index)
{
    int i = *index + 1;
    bool inClass = false;
    while (i < line.size()) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            inClass = true;
        } else if (ch == QLatin1Char(']')) {
            inClass = false;
        } else if (ch == QLatin1Char('/') && !inClass) {
            ++i;
            while (i < line.size() && line.at(i).isLetter())
                ++i;
            break;
        }
        ++i;
    }
    *index = i;
}

// Updates the JS scanning state for one physical line. Depth/brace state is
// always tracked, even when the line itself is emitted verbatim, so following
// code lines are indented correctly.
void scanJsLine(const QString& line, JsState* state)
{
    QString word;
    auto flushWord = [&] {
        if (!word.isEmpty()) {
            if (keywordAllowsRegex(word))
                state->regexAllowed = true;
            else
                state->regexAllowed = false;
            word.clear();
        }
    };
    auto pushBrace = [&](bool templateExpression) {
        state->braceIsTemplateExpression.push_back(templateExpression);
        ++state->depth;
    };
    auto popBrace = [&] {
        if (!state->braceIsTemplateExpression.empty()) {
            const bool templateExpression = state->braceIsTemplateExpression.back();
            state->braceIsTemplateExpression.pop_back();
            state->depth = qMax(0, state->depth - 1);
            if (templateExpression)
                state->mode = JsMode::Template;
        } else {
            state->depth = qMax(0, state->depth - 1);
        }
    };

    int i = 0;
    bool continued = false;
    while (i < line.size()) {
        const QChar ch = line.at(i);
        switch (state->mode) {
        case JsMode::Code:
            if (ch == QLatin1Char('/') && i + 1 < line.size()) {
                const QChar next = line.at(i + 1);
                if (next == QLatin1Char('/')) {
                    flushWord();
                    state->mode = JsMode::LineComment;
                    i += 2;
                    continue;
                }
                if (next == QLatin1Char('*')) {
                    flushWord();
                    state->mode = JsMode::BlockComment;
                    i += 2;
                    continue;
                }
                if (state->regexAllowed) {
                    flushWord();
                    int start = i;
                    scanRegex(line, &start);
                    i = start;
                    state->regexAllowed = false;
                    continue;
                }
                flushWord();
                state->regexAllowed = true;
                ++i;
                continue;
            }
            if (ch == QLatin1Char('\'')) {
                flushWord();
                state->mode = JsMode::SingleQuote;
                ++i;
                continue;
            }
            if (ch == QLatin1Char('"')) {
                flushWord();
                state->mode = JsMode::DoubleQuote;
                ++i;
                continue;
            }
            if (ch == QLatin1Char('`')) {
                flushWord();
                state->mode = JsMode::Template;
                ++i;
                continue;
            }
            if (isIdentifierStart(ch)) {
                word.append(ch);
                ++i;
                continue;
            }
            if (ch.isDigit()) {
                flushWord();
                state->regexAllowed = false;
                ++i;
                continue;
            }
            if (isIdentifierPart(ch)) {
                if (!word.isEmpty())
                    word.append(ch);
                ++i;
                continue;
            }
            flushWord();
            if (ch == QLatin1Char('{')) {
                pushBrace(false);
                state->regexAllowed = true;
            } else if (ch == QLatin1Char('}')) {
                popBrace();
                state->regexAllowed = false;
            } else if (ch == QLatin1Char('[') || ch == QLatin1Char('(')) {
                ++state->depth;
                state->regexAllowed = true;
            } else if (ch == QLatin1Char(']') || ch == QLatin1Char(')')) {
                state->depth = qMax(0, state->depth - 1);
                state->regexAllowed = false;
            } else if (ch == QLatin1Char(';') || ch == QLatin1Char(',') ||
                       ch == QLatin1Char(':') || ch == QLatin1Char('=') ||
                       ch == QLatin1Char('?') || ch == QLatin1Char('+') ||
                       ch == QLatin1Char('-') || ch == QLatin1Char('*') ||
                       ch == QLatin1Char('%') || ch == QLatin1Char('!') ||
                       ch == QLatin1Char('&') || ch == QLatin1Char('|') ||
                       ch == QLatin1Char('^') || ch == QLatin1Char('~') ||
                       ch == QLatin1Char('<') || ch == QLatin1Char('>')) {
                state->regexAllowed = true;
            } else {
                state->regexAllowed = false;
            }
            ++i;
            break;
        case JsMode::SingleQuote:
        case JsMode::DoubleQuote: {
            const QChar quote =
                state->mode == JsMode::SingleQuote ? QLatin1Char('\'') : QLatin1Char('"');
            if (ch == QLatin1Char('\\')) {
                if (i + 1 < line.size())
                    i += 2;
                else {
                    continued = true;
                    ++i;
                }
                continue;
            }
            if (ch == quote) {
                state->mode = JsMode::Code;
                state->regexAllowed = false;
            }
            ++i;
            break;
        }
        case JsMode::Template:
            if (ch == QLatin1Char('\\')) {
                if (i + 1 < line.size())
                    i += 2;
                else {
                    continued = true;
                    ++i;
                }
                continue;
            }
            if (ch == QLatin1Char('`')) {
                state->mode = JsMode::Code;
                state->regexAllowed = false;
                ++i;
                continue;
            }
            if (ch == QLatin1Char('$') && i + 1 < line.size() &&
                line.at(i + 1) == QLatin1Char('{')) {
                state->mode = JsMode::Code;
                pushBrace(true);
                state->regexAllowed = true;
                i += 2;
                continue;
            }
            ++i;
            break;
        case JsMode::LineComment:
            i = line.size();
            break;
        case JsMode::BlockComment:
            if (ch == QLatin1Char('*') && i + 1 < line.size() &&
                line.at(i + 1) == QLatin1Char('/')) {
                state->mode = JsMode::Code;
                i += 2;
                continue;
            }
            ++i;
            break;
        case JsMode::Regex:
            // Regex literals cannot span lines in valid JavaScript; fall back
            // to code at the end of the line.
            state->mode = JsMode::Code;
            break;
        }
    }
    flushWord();
    if (state->mode == JsMode::LineComment)
        state->mode = JsMode::Code;
    if (state->mode == JsMode::Regex)
        state->mode = JsMode::Code;
    state->stringContinued = continued;
}

bool jsLineStartsInCode(const JsState& state)
{
    return state.mode == JsMode::Code && !state.stringContinued;
}

}  // namespace

FormatResult DocumentFormatter::formatJavaScript(const QString& text)
{
    FormatResult result;
    QString source = text;
    if (source.startsWith(QChar(0xFEFF)))
        source.remove(0, 1);
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (source.trimmed().isEmpty()) {
        result.ok = true;
        result.text = source.isEmpty() ? QString() : QStringLiteral("\n");
        return result;
    }

    QStringList lines = source.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    JsState state;
    QStringList output;
    bool lastBlank = false;
    bool wroteContent = false;
    for (const QString& line : lines) {
        const bool startsInCode = jsLineStartsInCode(state);
        if (!startsInCode) {
            output.append(line);
            lastBlank = false;
            wroteContent = true;
            scanJsLine(line, &state);
            continue;
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            if (wroteContent && !lastBlank) {
                output.append(QString());
            }
            lastBlank = true;
            continue;
        }

        int leadingClosers = 0;
        int index = 0;
        while (index < trimmed.size()) {
            const QChar ch = trimmed.at(index);
            if (ch == QLatin1Char('}') || ch == QLatin1Char(']') || ch == QLatin1Char(')')) {
                ++leadingClosers;
                ++index;
                continue;
            }
            break;
        }
        const int effectiveDepth = qMax(0, state.depth - leadingClosers);
        output.append(QString(effectiveDepth * 2, QLatin1Char(' ')) + trimmed);
        lastBlank = false;
        wroteContent = true;
        scanJsLine(line, &state);
    }

    while (!output.isEmpty() && output.first().isEmpty())
        output.removeFirst();
    while (!output.isEmpty() && output.last().isEmpty())
        output.removeLast();

    result.ok = true;
    result.text = output.join(QLatin1Char('\n'));
    if (!result.text.isEmpty())
        result.text += QLatin1Char('\n');
    return result;
}

}  // namespace nq::lang
