// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// A TOML reader that records the exact byte span of every key, value, and
// table, plus an edit accumulator that rewrites a file by splicing those spans.
//
// OPE does not use a general TOML library because it does not merely need the
// parsed values: to keep git diffs reviewable it must rewrite a file by
// replacing only the byte ranges the user actually changed, leaving comments,
// blank lines, key order, and array formatting exactly as the author wrote
// them. Values whose type OPE has no use for (dates, for instance) are kept as
// opaque raw text rather than rejected, so no file that the Rust seeder accepts
// can fail to open here.

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>

namespace ope::toml {

/// Byte range [begin, end) into the original file bytes.
struct Span {
    qsizetype begin = -1;
    qsizetype end = -1;

    [[nodiscard]] constexpr bool isValid() const noexcept { return begin >= 0 && end >= begin; }
    [[nodiscard]] constexpr qsizetype length() const noexcept { return end - begin; }
};

enum class ValueKind { String, Integer, Float, Boolean, Array, InlineTable, Opaque };

/// String flavour, needed to re-emit an edited value in the style it was written.
enum class StringStyle { Basic, Literal, MultilineBasic, MultilineLiteral };

struct Value {
    ValueKind kind = ValueKind::Opaque;
    Span span;

    QString string;                 ///< decoded text (String only)
    StringStyle stringStyle = StringStyle::Basic;
    qlonglong integer = 0;
    double real = 0.0;
    bool boolean = false;

    QList<Value> items;             ///< Array and InlineTable element values
    QStringList itemKeys;           ///< InlineTable keys, parallel to items
    bool multilineLayout = false;   ///< array written across several lines

    [[nodiscard]] QStringList toStringList() const;
};

struct KeyValue {
    QStringList key;    ///< dotted key path, relative to the enclosing table
    Span keySpan;
    Span span;          ///< whole "key = value", excluding the line terminator
    Value value;

    [[nodiscard]] QString joinedKey() const { return key.join(u'.'); }
};

struct Table {
    QStringList path;               ///< e.g. {"parts", "Soprano"}
    bool isArrayElement = false;    ///< declared with [[double brackets]]
    Span headerSpan;                ///< the "[header]" text itself
    Span span;                      ///< header start .. end of its last pair
    QList<KeyValue> pairs;

    [[nodiscard]] QString joinedPath() const { return path.join(u'.'); }
    [[nodiscard]] const KeyValue *find(QStringView key) const;
};

struct ParseError {
    int line = 0;        ///< 1-based
    int column = 0;      ///< 1-based, counted in code points
    qsizetype offset = 0;
    QString message;
    QString sourceLine;  ///< the offending line, for display

    [[nodiscard]] QString formatted() const;
};

/// A parsed file: the original bytes plus every span needed to edit them.
class Document {
public:
    Document() = default;

    [[nodiscard]] const QByteArray &bytes() const noexcept { return m_bytes; }
    [[nodiscard]] const QList<KeyValue> &rootPairs() const noexcept { return m_rootPairs; }
    [[nodiscard]] const QList<Table> &tables() const noexcept { return m_tables; }

    [[nodiscard]] const KeyValue *rootPair(QStringView key) const;
    [[nodiscard]] const Table *table(const QStringList &path) const;
    /// Tables whose path starts with `prefix` (e.g. {"parts"} → every part).
    [[nodiscard]] QList<const Table *> tablesUnder(const QStringList &prefix) const;
    /// Elements of an array-of-tables, in source order.
    [[nodiscard]] QList<const Table *> arrayTables(const QStringList &path) const;

    /// Byte offset just past the last root-level pair (insertion anchor for a
    /// new top-level key), or 0 when the file has none.
    [[nodiscard]] qsizetype rootPairsEnd() const;
    /// Byte offset just past the end of `table`'s last pair.
    [[nodiscard]] qsizetype tableEnd(const Table &table) const;
    /// Offset of the start of the line containing `offset`.
    [[nodiscard]] qsizetype lineStart(qsizetype offset) const;
    /// Offset just past the newline that terminates the line containing `offset`.
    [[nodiscard]] qsizetype lineEnd(qsizetype offset) const;
    [[nodiscard]] int lineNumberAt(qsizetype offset) const;
    [[nodiscard]] QByteArray textOf(Span span) const;

    friend std::expected<Document, ParseError> parse(QByteArray bytes);

private:
    QByteArray m_bytes;
    QList<KeyValue> m_rootPairs;
    QList<Table> m_tables;
};

/// Parse `bytes`. On failure the error names the line, column, and source line;
/// callers refuse to open the document rather than build a partial model.
[[nodiscard]] std::expected<Document, ParseError> parse(QByteArray bytes);

/// An ordered set of byte-range replacements applied in one pass.
class Edit {
public:
    void replace(Span span, const QByteArray &text);
    void insert(qsizetype at, const QByteArray &text);
    void erase(Span span);

    [[nodiscard]] bool isEmpty() const noexcept { return m_ops.isEmpty(); }
    /// Apply every op to `original`. Ops are sorted by position; inserts at the
    /// same offset keep the order they were added in.
    [[nodiscard]] QByteArray apply(const QByteArray &original) const;

private:
    struct Op {
        qsizetype begin = 0;
        qsizetype end = 0;
        QByteArray text;
        int sequence = 0;
    };
    QList<Op> m_ops;
    int m_sequence = 0;
};

// -- emitters -----------------------------------------------------------------

/// Quote and escape `text` as a TOML basic string.
[[nodiscard]] QByteArray emitBasicString(const QString &text);
/// Emit a multi-line basic string: `"""\n<body>\n"""` (the body verbatim).
[[nodiscard]] QByteArray emitMultilineString(const QString &body);
/// Emit an array of strings on one line: `["a", "b"]`.
[[nodiscard]] QByteArray emitStringArrayInline(const QStringList &items);
/// Emit an array of strings one per line with trailing commas, `indent` spaces.
[[nodiscard]] QByteArray emitStringArrayBlock(const QStringList &items, int indent = 2);
/// Emit an array of integers on one line.
[[nodiscard]] QByteArray emitIntArray(const QList<int> &items);
/// A TOML table-header key segment, bare when possible and quoted when not.
[[nodiscard]] QByteArray emitKeySegment(const QString &segment);

} // namespace ope::toml
