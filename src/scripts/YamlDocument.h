#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

namespace nq::yaml {

// A small, order-preserving YAML document model. Mihomo configurations are
// read into this tree, exposed to JavaScript, written back, and compared for
// previews. The model intentionally keeps every field it does not understand
// so custom/extension keys survive a script round-trip.
enum class Type { Null, Scalar, Sequence, Mapping };
enum class ScalarType { Null, Bool, Int, Double, String };
enum class Style { Plain, SingleQuoted, DoubleQuoted, Literal, Folded };

struct Node {
    Type type = Type::Null;
    ScalarType scalarType = ScalarType::Null;
    Style style = Style::Plain;
    QString value;
    QString tag;
    QVector<Node> sequence;
    QVector<QPair<QString, Node>> mapping;

    static Node null();
    static Node string(const QString& value);
    static Node integer(qint64 value);
    static Node number(double value);
    static Node boolean(bool value);
    static Node array();
    static Node object();

    bool isNull() const { return type == Type::Null; }
    bool isScalar() const { return type == Type::Scalar; }
    bool isSequence() const { return type == Type::Sequence; }
    bool isMapping() const { return type == Type::Mapping; }

    const Node* find(const QString& key) const;
    Node* find(const QString& key);
    int indexOfKey(const QString& key) const;
    // Replaces the value at the existing position, or appends a new key.
    Node& set(const QString& key, Node value);
    bool remove(const QString& key);

    // Stable, human-readable representation used by diff and error messages.
    QString summary(int maxLength = 120) const;
    QString toJsonLike(int maxLength = 200) const;
};

struct ParseError {
    QString message;
    int line = 0;
    int column = 0;

    bool isEmpty() const { return message.isEmpty(); }
    QString toString() const;
};

class Document {
public:
    Node root;
    ParseError error;

    bool parse(const QByteArray& utf8);
    QByteArray dump(int indent = 2) const;

    bool valid() const { return error.isEmpty(); }
};

struct DiffEntry {
    enum class Kind { Added, Removed, Changed };

    Kind kind = Kind::Changed;
    QString path;
    QString before;
    QString after;
};

struct DiffResult {
    QVector<DiffEntry> entries;
    int total = 0;
    bool truncated = false;
};

// Compares two configuration trees and reports changed paths. Sequence changes
// are reported by index so that "rules[3]" points at the exact rule.
DiffResult diff(const Node& before, const Node& after, int maxEntries = 1000);

QString pathJoin(const QString& base, const QString& key, int index = -1);

}  // namespace nq::yaml
