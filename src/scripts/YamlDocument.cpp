#include "scripts/YamlDocument.h"

#include <yaml.h>

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <limits>

namespace nq::yaml {
namespace {

constexpr int kMaxDepth = 64;
constexpr int kMaxNodes = 200000;
constexpr int kMaxPathLength = 512;

QString fromYamlString(const yaml_char_t* value, size_t length = 0)
{
    if (!value)
        return {};
    if (length == 0)
        return QString::fromUtf8(reinterpret_cast<const char*>(value));
    return QString::fromUtf8(reinterpret_cast<const char*>(value), static_cast<int>(length));
}

Style styleFromYaml(yaml_scalar_style_t style)
{
    switch (style) {
    case YAML_SINGLE_QUOTED_SCALAR_STYLE:
        return Style::SingleQuoted;
    case YAML_DOUBLE_QUOTED_SCALAR_STYLE:
        return Style::DoubleQuoted;
    case YAML_LITERAL_SCALAR_STYLE:
        return Style::Literal;
    case YAML_FOLDED_SCALAR_STYLE:
        return Style::Folded;
    default:
        return Style::Plain;
    }
}

yaml_scalar_style_t styleToYaml(Style style)
{
    switch (style) {
    case Style::SingleQuoted:
        return YAML_SINGLE_QUOTED_SCALAR_STYLE;
    case Style::DoubleQuoted:
        return YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    case Style::Literal:
        return YAML_LITERAL_SCALAR_STYLE;
    case Style::Folded:
        return YAML_FOLDED_SCALAR_STYLE;
    default:
        return YAML_PLAIN_SCALAR_STYLE;
    }
}

bool isCoreTag(const QString& tag)
{
    return tag.isEmpty() || tag.startsWith(QStringLiteral("tag:yaml.org,2002:"));
}

QString coreTagName(const QString& tag)
{
    static const QString prefix = QStringLiteral("tag:yaml.org,2002:");
    if (tag.startsWith(prefix))
        return tag.mid(prefix.size());
    return {};
}

bool parseInteger(const QString& raw, qint64* output)
{
    QString text = raw;
    text.remove(QLatin1Char('_'));
    if (text.isEmpty())
        return false;

    bool negative = false;
    if (text.startsWith(QLatin1Char('-')) || text.startsWith(QLatin1Char('+'))) {
        negative = text.startsWith(QLatin1Char('-'));
        text.remove(0, 1);
    }
    if (text.isEmpty())
        return false;

    int base = 10;
    QString digits = text;
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        base = 16;
        digits = text.mid(2);
    } else if (text.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive)) {
        base = 8;
        digits = text.mid(2);
    } else if (text.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
        base = 2;
        digits = text.mid(2);
    }

    bool ok = false;
    const qint64 parsed = digits.toLongLong(&ok, base);
    if (!ok)
        return false;
    if (negative) {
        if (parsed == std::numeric_limits<qint64>::min())
            return false;
        *output = -parsed;
    } else {
        *output = parsed;
    }
    return true;
}

bool looksLikeFloat(const QString& text)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[-+]?(?:(?:[0-9][0-9_]*)?\.[0-9_]+|[0-9][0-9_]*\.)(?:[eE][-+]?[0-9]+)?$|^[-+]?[0-9][0-9_]*[eE][-+]?[0-9]+$)"));
    return pattern.match(text).hasMatch();
}

void resolveScalar(const QString& raw, Style style, const QString& tag, Node* node)
{
    node->type = Type::Scalar;
    node->style = style;
    node->tag = isCoreTag(tag) ? QString() : tag;
    node->value = raw;

    const QString tagName = coreTagName(tag);
    const bool forcedString = tagName == QStringLiteral("str") || tagName == QStringLiteral("timestamp") ||
                              tagName == QStringLiteral("binary");
    const bool forcedBool = tagName == QStringLiteral("bool");
    const bool forcedInt = tagName == QStringLiteral("int");
    const bool forcedFloat = tagName == QStringLiteral("float");
    const bool forcedNull = tagName == QStringLiteral("null");

    if (!node->tag.isEmpty()) {
        // Unknown custom tags are preserved verbatim but treated as strings so
        // that a Mihomo extension is never silently retyped.
        node->scalarType = ScalarType::String;
        return;
    }

    if (forcedString) {
        node->scalarType = ScalarType::String;
        return;
    }
    if (forcedNull) {
        node->type = Type::Null;
        node->scalarType = ScalarType::Null;
        node->value = QStringLiteral("null");
        return;
    }
    if (forcedBool) {
        const QString lower = raw.trimmed().toLower();
        if (lower == QStringLiteral("true") || lower == QStringLiteral("yes") ||
            lower == QStringLiteral("on")) {
            node->scalarType = ScalarType::Bool;
            node->value = QStringLiteral("true");
        } else if (lower == QStringLiteral("false") || lower == QStringLiteral("no") ||
                   lower == QStringLiteral("off")) {
            node->scalarType = ScalarType::Bool;
            node->value = QStringLiteral("false");
        } else {
            node->scalarType = ScalarType::String;
        }
        return;
    }
    if (forcedInt) {
        qint64 parsed = 0;
        if (parseInteger(raw, &parsed)) {
            node->scalarType = ScalarType::Int;
            node->value = QString::number(parsed);
        } else {
            node->scalarType = ScalarType::String;
        }
        return;
    }
    if (forcedFloat) {
        bool ok = false;
        const double parsed = raw.toDouble(&ok);
        if (ok && std::isfinite(parsed)) {
            node->scalarType = ScalarType::Double;
            QString text = QString::number(parsed, 'g', 17);
            if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e')) &&
                !text.contains(QLatin1Char('E')))
                text += QStringLiteral(".0");
            node->value = text;
        } else {
            node->scalarType = ScalarType::String;
        }
        return;
    }

    if (style != Style::Plain) {
        node->scalarType = ScalarType::String;
        return;
    }

    const QString trimmed = raw.trimmed();
    const QString lower = trimmed.toLower();
    if (trimmed.isEmpty() || lower == QStringLiteral("~") || lower == QStringLiteral("null")) {
        node->type = Type::Null;
        node->scalarType = ScalarType::Null;
        node->value = QStringLiteral("null");
        return;
    }
    if (lower == QStringLiteral("true") || lower == QStringLiteral("yes") ||
        lower == QStringLiteral("on")) {
        node->scalarType = ScalarType::Bool;
        node->value = QStringLiteral("true");
        return;
    }
    if (lower == QStringLiteral("false") || lower == QStringLiteral("no") ||
        lower == QStringLiteral("off")) {
        node->scalarType = ScalarType::Bool;
        node->value = QStringLiteral("false");
        return;
    }

    qint64 integer = 0;
    if (parseInteger(trimmed, &integer)) {
        node->scalarType = ScalarType::Int;
        node->value = QString::number(integer);
        return;
    }
    if (looksLikeFloat(trimmed)) {
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        if (ok && std::isfinite(parsed)) {
            node->scalarType = ScalarType::Double;
            QString text = QString::number(parsed, 'g', 17);
            if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e')) &&
                !text.contains(QLatin1Char('E')))
                text += QStringLiteral(".0");
            node->value = text;
            return;
        }
    }

    node->scalarType = ScalarType::String;
}

struct ParseContext {
    yaml_parser_t* parser = nullptr;
    QHash<QString, Node> anchors;
    int nodes = 0;
    ParseError error;
};

bool nextEvent(ParseContext& context, yaml_event_t* event)
{
    if (yaml_parser_parse(context.parser, event))
        return true;
    context.error.message = QString::fromUtf8(context.parser->problem
                                                   ? context.parser->problem
                                                   : "无法解析 YAML");
    context.error.line = static_cast<int>(context.parser->problem_mark.line) + 1;
    context.error.column = static_cast<int>(context.parser->problem_mark.column) + 1;
    return false;
}

bool buildNode(ParseContext& context, yaml_event_t& event, Node* node, int depth);

void expandMergeKeys(Node* node, ParseContext& context)
{
    QVector<QPair<QString, Node>> output;
    output.reserve(node->mapping.size());

    QVector<Node> merged;
    for (const auto& entry : std::as_const(node->mapping)) {
        if (entry.first != QStringLiteral("<<")) {
            output.append(entry);
            continue;
        }
        if (entry.second.isMapping()) {
            merged.append(entry.second);
        } else if (entry.second.isSequence()) {
            for (const Node& item : entry.second.sequence) {
                if (!item.isMapping()) {
                    context.error.message = QStringLiteral("YAML 合并键 << 的序列中只能包含映射");
                    return;
                }
                merged.append(item);
            }
        } else {
            context.error.message = QStringLiteral("YAML 合并键 << 必须引用映射或映射序列");
            return;
        }
    }

    for (const Node& source : std::as_const(merged)) {
        for (const auto& entry : source.mapping) {
            bool exists = false;
            for (const auto& current : std::as_const(output)) {
                if (current.first == entry.first) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                output.append(entry);
        }
    }

    node->mapping = output;
}

bool buildNode(ParseContext& context, yaml_event_t& event, Node* node, int depth)
{
    if (depth > kMaxDepth) {
        context.error.message = QStringLiteral("YAML 嵌套层级超过 %1 层").arg(kMaxDepth);
        return false;
    }
    if (++context.nodes > kMaxNodes) {
        context.error.message = QStringLiteral("YAML 节点数量超过 %1 个").arg(kMaxNodes);
        return false;
    }

    switch (event.type) {
    case YAML_SCALAR_EVENT: {
        const QString raw = fromYamlString(event.data.scalar.value, event.data.scalar.length);
        const QString tag = event.data.scalar.tag
                                ? fromYamlString(event.data.scalar.tag)
                                : QString();
        const QString anchor = event.data.scalar.anchor
                                   ? fromYamlString(event.data.scalar.anchor)
                                   : QString();
        resolveScalar(raw, styleFromYaml(event.data.scalar.style), tag, node);
        if (!anchor.isEmpty())
            context.anchors.insert(anchor, *node);
        yaml_event_delete(&event);
        return true;
    }
    case YAML_SEQUENCE_START_EVENT: {
        const QString tag = event.data.sequence_start.tag
                                ? fromYamlString(event.data.sequence_start.tag)
                                : QString();
        const QString anchor = event.data.sequence_start.anchor
                                   ? fromYamlString(event.data.sequence_start.anchor)
                                   : QString();
        *node = Node::array();
        if (!isCoreTag(tag))
            node->tag = tag;
        yaml_event_delete(&event);

        while (true) {
            yaml_event_t childEvent;
            if (!nextEvent(context, &childEvent))
                return false;
            if (childEvent.type == YAML_SEQUENCE_END_EVENT) {
                yaml_event_delete(&childEvent);
                break;
            }
            Node child;
            if (!buildNode(context, childEvent, &child, depth + 1))
                return false;
            node->sequence.append(std::move(child));
        }
        if (!anchor.isEmpty())
            context.anchors.insert(anchor, *node);
        return true;
    }
    case YAML_MAPPING_START_EVENT: {
        const QString tag = event.data.mapping_start.tag
                                ? fromYamlString(event.data.mapping_start.tag)
                                : QString();
        const QString anchor = event.data.mapping_start.anchor
                                   ? fromYamlString(event.data.mapping_start.anchor)
                                   : QString();
        *node = Node::object();
        if (!isCoreTag(tag))
            node->tag = tag;
        yaml_event_delete(&event);

        while (true) {
            yaml_event_t keyEvent;
            if (!nextEvent(context, &keyEvent))
                return false;
            if (keyEvent.type == YAML_MAPPING_END_EVENT) {
                yaml_event_delete(&keyEvent);
                break;
            }

            Node keyNode;
            if (!buildNode(context, keyEvent, &keyNode, depth + 1))
                return false;
            if (!keyNode.isScalar()) {
                context.error.message = QStringLiteral("YAML 映射的键必须是标量");
                return false;
            }
            const QString key = keyNode.value;

            yaml_event_t valueEvent;
            if (!nextEvent(context, &valueEvent))
                return false;
            Node valueNode;
            if (!buildNode(context, valueEvent, &valueNode, depth + 1))
                return false;
            node->set(key, std::move(valueNode));
        }

        expandMergeKeys(node, context);
        if (!context.error.isEmpty())
            return false;
        if (!anchor.isEmpty())
            context.anchors.insert(anchor, *node);
        return true;
    }
    case YAML_ALIAS_EVENT: {
        const QString anchor = event.data.alias.anchor
                                   ? fromYamlString(event.data.alias.anchor)
                                   : QString();
        const auto found = context.anchors.constFind(anchor);
        if (found == context.anchors.cend()) {
            context.error.message = QStringLiteral("YAML 别名引用了未定义的锚点 &%1").arg(anchor);
            return false;
        }
        *node = found.value();
        yaml_event_delete(&event);
        return true;
    }
    default:
        context.error.message = QStringLiteral("YAML 中出现了意外的结构");
        return false;
    }
}

struct EmitContext {
    yaml_emitter_t emitter{};
    QByteArray output;
    bool failed = false;
};

int emitterWrite(void* data, unsigned char* buffer, size_t size)
{
    auto* context = static_cast<EmitContext*>(data);
    if (!context || !buffer)
        return 0;
    context->output.append(reinterpret_cast<const char*>(buffer), static_cast<int>(size));
    return 1;
}

bool emitEvent(EmitContext& context, yaml_event_t* event)
{
    const bool ok = yaml_emitter_emit(&context.emitter, event) != 0;
    if (!ok) {
        yaml_event_delete(event);
        context.failed = true;
    }
    return ok;
}

bool isPlainSafe(const QString& text)
{
    if (text.isEmpty() || text != text.trimmed())
        return false;
    if (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')) ||
        text.contains(QLatin1Char('\t')))
        return false;

    static const QString indicators = QStringLiteral("-?:,[]{}#&*!|>'\"%@`");
    if (indicators.contains(text.front()))
        return false;
    if (text.startsWith(QStringLiteral("---")) || text.startsWith(QStringLiteral("...")))
        return false;
    if (text.contains(QStringLiteral(": ")) || text.contains(QStringLiteral(" #")))
        return false;
    for (const QChar ch : text) {
        if (ch.unicode() < 0x20 && ch != QLatin1Char('\t'))
            return false;
    }

    const QString lower = text.toLower();
    if (lower == QStringLiteral("null") || lower == QStringLiteral("~") ||
        lower == QStringLiteral("true") || lower == QStringLiteral("false") ||
        lower == QStringLiteral("yes") || lower == QStringLiteral("no") ||
        lower == QStringLiteral("on") || lower == QStringLiteral("off"))
        return false;

    qint64 integer = 0;
    if (parseInteger(text, &integer))
        return false;
    if (looksLikeFloat(text))
        return false;
    return true;
}

yaml_scalar_style_t scalarStyleFor(const Node& node)
{
    if (node.scalarType != ScalarType::String)
        return YAML_PLAIN_SCALAR_STYLE;

    const QString& text = node.value;
    if (node.style == Style::Literal || node.style == Style::Folded) {
        if (text.contains(QLatin1Char('\n')))
            return styleToYaml(node.style);
    }
    if (node.style == Style::SingleQuoted && !text.contains(QLatin1Char('\n')))
        return YAML_SINGLE_QUOTED_SCALAR_STYLE;
    if (node.style == Style::DoubleQuoted)
        return YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    if (text.contains(QLatin1Char('\n')))
        return YAML_LITERAL_SCALAR_STYLE;
    if (isPlainSafe(text))
        return YAML_PLAIN_SCALAR_STYLE;
    return YAML_DOUBLE_QUOTED_SCALAR_STYLE;
}

bool emitScalar(EmitContext& context, const Node& node, bool asKey = false)
{
    QString value = node.value;
    yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
    if (node.type == Type::Null) {
        value = QStringLiteral("null");
    } else if (node.scalarType == ScalarType::String) {
        style = scalarStyleFor(node);
        if (asKey && style == YAML_PLAIN_SCALAR_STYLE && !isPlainSafe(value))
            style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }

    const QByteArray utf8 = value.toUtf8();
    const QByteArray tag = node.tag.toUtf8();
    const bool hasTag = !node.tag.isEmpty();
    const int plainImplicit = hasTag ? 0 : (style == YAML_PLAIN_SCALAR_STYLE ? 1 : 0);
    const int quotedImplicit = hasTag ? 0 : (style != YAML_PLAIN_SCALAR_STYLE ? 1 : 0);

    yaml_event_t event;
    if (!yaml_scalar_event_initialize(&event, nullptr,
                                      hasTag ? reinterpret_cast<const yaml_char_t*>(tag.constData())
                                             : nullptr,
                                      reinterpret_cast<const yaml_char_t*>(utf8.constData()),
                                      utf8.size(), plainImplicit, quotedImplicit, style)) {
        context.failed = true;
        return false;
    }
    return emitEvent(context, &event);
}

bool emitNode(EmitContext& context, const Node& node)
{
    switch (node.type) {
    case Type::Null:
    case Type::Scalar:
        return emitScalar(context, node);
    case Type::Sequence: {
        const QByteArray tag = node.tag.toUtf8();
        yaml_event_t start;
        if (!yaml_sequence_start_event_initialize(
                &start, nullptr,
                node.tag.isEmpty() ? nullptr
                                   : reinterpret_cast<const yaml_char_t*>(tag.constData()),
                node.tag.isEmpty() ? 1 : 0, YAML_BLOCK_SEQUENCE_STYLE)) {
            context.failed = true;
            return false;
        }
        if (!emitEvent(context, &start))
            return false;
        for (const Node& child : node.sequence) {
            if (!emitNode(context, child))
                return false;
        }
        yaml_event_t end;
        yaml_sequence_end_event_initialize(&end);
        return emitEvent(context, &end);
    }
    case Type::Mapping: {
        const QByteArray tag = node.tag.toUtf8();
        yaml_event_t start;
        if (!yaml_mapping_start_event_initialize(
                &start, nullptr,
                node.tag.isEmpty() ? nullptr
                                   : reinterpret_cast<const yaml_char_t*>(tag.constData()),
                node.tag.isEmpty() ? 1 : 0, YAML_BLOCK_MAPPING_STYLE)) {
            context.failed = true;
            return false;
        }
        if (!emitEvent(context, &start))
            return false;
        for (const auto& entry : node.mapping) {
            Node key;
            key.type = Type::Scalar;
            key.scalarType = ScalarType::String;
            key.value = entry.first;
            if (!emitScalar(context, key, true))
                return false;
            if (!emitNode(context, entry.second))
                return false;
        }
        yaml_event_t end;
        yaml_mapping_end_event_initialize(&end);
        return emitEvent(context, &end);
    }
    }
    return false;
}

QString scalarSummary(const Node& node)
{
    if (node.type == Type::Null)
        return QStringLiteral("null");
    if (node.scalarType == ScalarType::String)
        return QStringLiteral("\"%1\"").arg(node.value);
    return node.value;
}

QString summaryFor(const Node& node, int maxLength)
{
    QString text;
    switch (node.type) {
    case Type::Null:
        text = QStringLiteral("null");
        break;
    case Type::Scalar:
        text = scalarSummary(node);
        break;
    case Type::Sequence:
        text = QStringLiteral("[…%1 项]").arg(node.sequence.size());
        break;
    case Type::Mapping:
        text = QStringLiteral("{…%1 个字段}").arg(node.mapping.size());
        break;
    }
    if (maxLength > 0 && text.size() > maxLength)
        text = text.left(maxLength - 1) + QStringLiteral("…");
    return text;
}

QString jsonLike(const Node& node, int maxLength, int depth)
{
    if (depth > 32)
        return QStringLiteral("…");
    switch (node.type) {
    case Type::Null:
        return QStringLiteral("null");
    case Type::Scalar:
        if (node.scalarType == ScalarType::String) {
            QString escaped = node.value;
            escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            return QStringLiteral("\"%1\"").arg(escaped);
        }
        return node.value;
    case Type::Sequence: {
        QStringList parts;
        for (const Node& child : node.sequence)
            parts.append(jsonLike(child, 0, depth + 1));
        QString text = QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
        if (maxLength > 0 && text.size() > maxLength)
            text = text.left(maxLength - 1) + QStringLiteral("…");
        return text;
    }
    case Type::Mapping: {
        QStringList parts;
        for (const auto& entry : node.mapping) {
            parts.append(QStringLiteral("%1: %2")
                             .arg(entry.first, jsonLike(entry.second, 0, depth + 1)));
        }
        QString text = QStringLiteral("{%1}").arg(parts.join(QStringLiteral(", ")));
        if (maxLength > 0 && text.size() > maxLength)
            text = text.left(maxLength - 1) + QStringLiteral("…");
        return text;
    }
    }
    return {};
}

bool pathKeyIsPlain(const QString& key)
{
    if (key.isEmpty())
        return false;
    for (const QChar ch : key) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')))
            return false;
    }
    return true;
}

class DiffBuilder {
public:
    explicit DiffBuilder(int maxEntries)
        : m_maxEntries(maxEntries)
    {
    }

    void compare(const Node& before, const Node& after, const QString& path)
    {        if (before.type != after.type) {
            add(DiffEntry::Kind::Changed, path, before, after);
            return;
        }

        switch (before.type) {
        case Type::Null:
            return;
        case Type::Scalar:
            if (before.scalarType != after.scalarType || before.value != after.value ||
                before.tag != after.tag) {
                add(DiffEntry::Kind::Changed, path, before, after);
            }
            return;
        case Type::Sequence: {
            bool scalarOnly = true;
            for (const Node& item : before.sequence) {
                if (!item.isScalar()) {
                    scalarOnly = false;
                    break;
                }
            }
            if (scalarOnly) {
                for (const Node& item : after.sequence) {
                    if (!item.isScalar()) {
                        scalarOnly = false;
                        break;
                    }
                }
            }
            if (scalarOnly && !before.sequence.isEmpty() && !after.sequence.isEmpty() &&
                static_cast<qint64>(before.sequence.size()) * after.sequence.size() <= 2000000) {
                compareScalarSequence(before, after, path);
                return;
            }

            const int common = qMin(before.sequence.size(), after.sequence.size());
            for (int i = 0; i < common; ++i)
                compare(before.sequence.at(i), after.sequence.at(i), pathJoin(path, {}, i));
            for (int i = common; i < before.sequence.size(); ++i) {
                add(DiffEntry::Kind::Removed, pathJoin(path, {}, i),
                    before.sequence.at(i), Node::null());
            }
            for (int i = common; i < after.sequence.size(); ++i) {
                add(DiffEntry::Kind::Added, pathJoin(path, {}, i), Node::null(),
                    after.sequence.at(i));
            }
            return;
        }
        case Type::Mapping: {
            for (const auto& entry : before.mapping) {
                const Node* next = after.find(entry.first);
                if (!next) {
                    add(DiffEntry::Kind::Removed, pathJoin(path, entry.first), entry.second,
                        Node::null());
                } else {
                    compare(entry.second, *next, pathJoin(path, entry.first));
                }
            }
            for (const auto& entry : after.mapping) {
                if (!before.find(entry.first)) {
                    add(DiffEntry::Kind::Added, pathJoin(path, entry.first), Node::null(),
                        entry.second);
                }
            }
            return;
        }
        }
    }

    const DiffResult& result() const { return m_result; }

private:
    void compareScalarSequence(const Node& before, const Node& after, const QString& path)
    {
        const QVector<Node>& a = before.sequence;
        const QVector<Node>& b = after.sequence;
        const int n = a.size();
        const int m = b.size();
        const int stride = m + 1;
        QVector<int> lcs(static_cast<qsizetype>(n + 1) * stride, 0);
        const auto at = [&](int i, int j) -> int& {
            return lcs[static_cast<qsizetype>(i) * stride + j];
        };
        for (int i = 1; i <= n; ++i) {
            for (int j = 1; j <= m; ++j) {
                if (a.at(i - 1).scalarType == b.at(j - 1).scalarType &&
                    a.at(i - 1).value == b.at(j - 1).value) {
                    at(i, j) = at(i - 1, j - 1) + 1;
                } else {
                    at(i, j) = qMax(at(i - 1, j), at(i, j - 1));
                }
            }
        }

        QVector<QPair<DiffEntry::Kind, Node>> operations;
        int i = n;
        int j = m;
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 &&
                a.at(i - 1).scalarType == b.at(j - 1).scalarType &&
                a.at(i - 1).value == b.at(j - 1).value) {
                --i;
                --j;
            } else if (j == 0 || (i > 0 && at(i - 1, j) >= at(i, j - 1))) {
                operations.append({DiffEntry::Kind::Removed, a.at(i - 1)});
                --i;
            } else {
                operations.append({DiffEntry::Kind::Added, b.at(j - 1)});
                --j;
            }
        }
        for (auto it = operations.crbegin(); it != operations.crend(); ++it) {
            if (it->first == DiffEntry::Kind::Removed)
                add(DiffEntry::Kind::Removed, path, it->second, Node::null());
            else
                add(DiffEntry::Kind::Added, path, Node::null(), it->second);
        }
    }

    void add(DiffEntry::Kind kind, const QString& rawPath, const Node& before, const Node& after)
    {
        ++m_result.total;
        if (m_result.entries.size() >= m_maxEntries) {
            m_result.truncated = true;
            return;
        }
        DiffEntry entry;
        entry.kind = kind;
        entry.path = rawPath;
        if (entry.path.size() > kMaxPathLength)
            entry.path = entry.path.left(kMaxPathLength - 1) + QStringLiteral("…");
        entry.before = summaryFor(before, 300);
        entry.after = summaryFor(after, 300);
        m_result.entries.append(std::move(entry));
    }

    int m_maxEntries = 1000;
    DiffResult m_result;
};

}  // namespace

Node Node::null()
{
    Node node;
    node.type = Type::Null;
    node.scalarType = ScalarType::Null;
    node.value = QStringLiteral("null");
    return node;
}

Node Node::string(const QString& value)
{
    Node node;
    node.type = Type::Scalar;
    node.scalarType = ScalarType::String;
    node.value = value;
    return node;
}

Node Node::integer(qint64 value)
{
    Node node;
    node.type = Type::Scalar;
    node.scalarType = ScalarType::Int;
    node.value = QString::number(value);
    return node;
}

Node Node::number(double value)
{
    Node node;
    node.type = Type::Scalar;
    node.scalarType = ScalarType::Double;
    QString text = QString::number(value, 'g', 17);
    if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e')) &&
        !text.contains(QLatin1Char('E')))
        text += QStringLiteral(".0");
    node.value = text;
    return node;
}

Node Node::boolean(bool value)
{
    Node node;
    node.type = Type::Scalar;
    node.scalarType = ScalarType::Bool;
    node.value = value ? QStringLiteral("true") : QStringLiteral("false");
    return node;
}

Node Node::array()
{
    Node node;
    node.type = Type::Sequence;
    return node;
}

Node Node::object()
{
    Node node;
    node.type = Type::Mapping;
    return node;
}

const Node* Node::find(const QString& key) const
{
    for (const auto& entry : mapping) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

Node* Node::find(const QString& key)
{
    for (auto& entry : mapping) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

int Node::indexOfKey(const QString& key) const
{
    for (int i = 0; i < mapping.size(); ++i) {
        if (mapping.at(i).first == key)
            return i;
    }
    return -1;
}

Node& Node::set(const QString& key, Node value)
{
    const int index = indexOfKey(key);
    if (index >= 0) {
        mapping[index].second = std::move(value);
        return mapping[index].second;
    }
    mapping.append({key, std::move(value)});
    return mapping.last().second;
}

bool Node::remove(const QString& key)
{
    const int index = indexOfKey(key);
    if (index < 0)
        return false;
    mapping.removeAt(index);
    return true;
}

QString Node::summary(int maxLength) const
{
    return summaryFor(*this, maxLength);
}

QString Node::toJsonLike(int maxLength) const
{
    return jsonLike(*this, maxLength, 0);
}

QString ParseError::toString() const
{
    if (message.isEmpty())
        return {};
    if (line > 0)
        return QStringLiteral("%1（第 %2 行，第 %3 列）").arg(message).arg(line).arg(column);
    return message;
}

bool Document::parse(const QByteArray& input)
{
    root = Node::null();
    error = {};

    QByteArray data = input;
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    if (data.trimmed().isEmpty()) {
        error.message = QStringLiteral("YAML 配置为空");
        return false;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        error.message = QStringLiteral("无法初始化 YAML 解析器");
        return false;
    }
    yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char*>(data.constData()),
                                 static_cast<size_t>(data.size()));

    ParseContext context;
    context.parser = &parser;

    yaml_event_t event;
    bool ok = true;
    int documents = 0;

    if (!yaml_parser_parse(&parser, &event)) {
        context.error.message = QString::fromUtf8(parser.problem ? parser.problem
                                                                  : "无法解析 YAML");
        context.error.line = static_cast<int>(parser.problem_mark.line) + 1;
        context.error.column = static_cast<int>(parser.problem_mark.column) + 1;
        ok = false;
    } else if (event.type != YAML_STREAM_START_EVENT) {
        context.error.message = QStringLiteral("YAML 流格式无效");
        ok = false;
        yaml_event_delete(&event);
    } else {
        yaml_event_delete(&event);
    }

    while (ok) {
        if (!yaml_parser_parse(&parser, &event)) {
            context.error.message = QString::fromUtf8(parser.problem ? parser.problem
                                                                      : "无法解析 YAML");
            context.error.line = static_cast<int>(parser.problem_mark.line) + 1;
            context.error.column = static_cast<int>(parser.problem_mark.column) + 1;
            ok = false;
            break;
        }

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        if (event.type != YAML_DOCUMENT_START_EVENT) {
            context.error.message = QStringLiteral("YAML 文档结构无效");
            ok = false;
            yaml_event_delete(&event);
            break;
        }
        yaml_event_delete(&event);

        if (++documents > 1) {
            context.error.message = QStringLiteral("配置中包含多个 YAML 文档，仅支持单个文档");
            ok = false;
            break;
        }

        yaml_event_t rootEvent;
        if (!nextEvent(context, &rootEvent)) {
            ok = false;
            break;
        }
        Node rootNode;
        if (!buildNode(context, rootEvent, &rootNode, 0)) {
            ok = false;
            break;
        }
        root = std::move(rootNode);

        yaml_event_t documentEnd;
        if (!nextEvent(context, &documentEnd)) {
            ok = false;
            break;
        }
        if (documentEnd.type != YAML_DOCUMENT_END_EVENT) {
            context.error.message = QStringLiteral("YAML 文档缺少结束标记");
            yaml_event_delete(&documentEnd);
            ok = false;
            break;
        }
        yaml_event_delete(&documentEnd);
    }

    yaml_parser_delete(&parser);
    if (!ok) {
        error = context.error;
        root = Node::null();
        return false;
    }
    return true;
}

QByteArray Document::dump(int indent) const
{
    EmitContext context;
    if (!yaml_emitter_initialize(&context.emitter))
        return {};
    yaml_emitter_set_output(&context.emitter, emitterWrite, &context);
    yaml_emitter_set_unicode(&context.emitter, 1);
    yaml_emitter_set_indent(&context.emitter, qBound(2, indent, 8));
    yaml_emitter_set_width(&context.emitter, 100000);

    yaml_event_t event;
    if (!yaml_emitter_open(&context.emitter)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }

    if (!yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 1)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }
    if (!emitEvent(context, &event)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }

    if (!emitNode(context, root)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }

    if (!yaml_document_end_event_initialize(&event, 1)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }
    if (!emitEvent(context, &event)) {
        yaml_emitter_delete(&context.emitter);
        return {};
    }

    if (!context.failed)
        yaml_emitter_flush(&context.emitter);
    yaml_emitter_delete(&context.emitter);

    if (context.failed)
        return {};

    if (!context.output.isEmpty() && !context.output.endsWith('\n'))
        context.output.append('\n');
    return context.output;
}

QString pathJoin(const QString& base, const QString& key, int index)
{
    if (index >= 0) {
        return QStringLiteral("%1[%2]").arg(base).arg(index);
    }
    if (base.isEmpty())
        return pathKeyIsPlain(key) ? key : QStringLiteral("[\"%1\"]").arg(key);
    if (pathKeyIsPlain(key))
        return base + QLatin1Char('.') + key;
    return QStringLiteral("%1[\"%2\"]").arg(base, key);
}

DiffResult diff(const Node& before, const Node& after, int maxEntries)
{
    DiffBuilder builder(qMax(1, maxEntries));
    builder.compare(before, after, QString());
    return builder.result();
}

}  // namespace nq::yaml
