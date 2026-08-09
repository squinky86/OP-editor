// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Toml.h"

#include <QtGlobal>

#include <algorithm>

namespace ope::toml {
namespace {

constexpr bool isBareKeyChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
        || c == '-';
}

constexpr bool isSpaceOrTab(char c) noexcept { return c == ' ' || c == '\t'; }

/// True for bytes that can start a bare number, sign, or date.
constexpr bool isNumberStart(char c) noexcept
{
    return (c >= '0' && c <= '9') || c == '+' || c == '-';
}

/// Tokenizer over the raw bytes. Every production records byte spans so the
/// document can later be rewritten by splicing rather than re-emitting.
class Parser {
public:
    explicit Parser(const QByteArray &bytes) : m_bytes(bytes), m_size(bytes.size()) {}

    std::expected<void, ParseError> run(QList<KeyValue> &rootPairs, QList<Table> &tables);

private:
    [[nodiscard]] bool atEnd() const noexcept { return m_pos >= m_size; }
    [[nodiscard]] char peek(qsizetype ahead = 0) const noexcept
    {
        const qsizetype at = m_pos + ahead;
        return at < m_size ? m_bytes.at(at) : '\0';
    }
    [[nodiscard]] bool startsWith(const char *literal) const noexcept
    {
        return m_bytes.sliced(std::min(m_pos, m_size)).startsWith(literal);
    }

    void skipSpaces() noexcept
    {
        while (!atEnd() && isSpaceOrTab(peek()))
            ++m_pos;
    }
    void skipComment() noexcept
    {
        if (peek() == '#')
            while (!atEnd() && peek() != '\n')
                ++m_pos;
    }
    /// Advance past whitespace, comments, and newlines.
    void skipTrivia() noexcept
    {
        while (!atEnd()) {
            const char c = peek();
            if (isSpaceOrTab(c) || c == '\n' || c == '\r') {
                ++m_pos;
            } else if (c == '#') {
                skipComment();
            } else {
                break;
            }
        }
    }
    /// Consume the rest of a line: optional spaces, optional comment, newline.
    std::expected<void, ParseError> expectLineEnd(const char *context);

    std::expected<QStringList, ParseError> parseKeyPath(Span &span);
    std::expected<QString, ParseError> parseKeySegment();
    std::expected<KeyValue, ParseError> parseKeyValue();
    std::expected<Value, ParseError> parseValue();
    std::expected<Value, ParseError> parseString();
    std::expected<Value, ParseError> parseArray();
    std::expected<Value, ParseError> parseInlineTable();
    std::expected<Value, ParseError> parseAtom();
    std::expected<QString, ParseError> decodeEscapes(const QByteArray &raw, bool multiline);

    ParseError error(const QString &message) const { return errorAt(m_pos, message); }
    ParseError errorAt(qsizetype offset, const QString &message) const;

    const QByteArray &m_bytes;
    qsizetype m_size = 0;
    qsizetype m_pos = 0;
};

ParseError Parser::errorAt(qsizetype offset, const QString &message) const
{
    ParseError err;
    err.offset = std::clamp<qsizetype>(offset, 0, m_size);
    err.message = message;

    int line = 1;
    qsizetype lineBegin = 0;
    for (qsizetype i = 0; i < err.offset; ++i) {
        if (m_bytes.at(i) == '\n') {
            ++line;
            lineBegin = i + 1;
        }
    }
    qsizetype lineFinish = m_bytes.indexOf('\n', lineBegin);
    if (lineFinish < 0)
        lineFinish = m_size;

    err.line = line;
    err.sourceLine = QString::fromUtf8(m_bytes.sliced(lineBegin, lineFinish - lineBegin)).trimmed();
    // Columns are counted in code points so the caret lands correctly under the
    // accented text and undertie characters translations are full of.
    err.column = static_cast<int>(
                     QString::fromUtf8(m_bytes.sliced(lineBegin, err.offset - lineBegin)).size())
        + 1;
    return err;
}

std::expected<void, ParseError> Parser::expectLineEnd(const char *context)
{
    skipSpaces();
    skipComment();
    if (atEnd())
        return {};
    const char c = peek();
    if (c == '\n') {
        ++m_pos;
        return {};
    }
    if (c == '\r' && peek(1) == '\n') {
        m_pos += 2;
        return {};
    }
    return std::unexpected(error(QStringLiteral("unexpected text after %1").arg(context)));
}

std::expected<QString, ParseError> Parser::parseKeySegment()
{
    if (peek() == '"' || peek() == '\'') {
        const auto value = parseString();
        if (!value)
            return std::unexpected(value.error());
        return value->string;
    }
    const qsizetype begin = m_pos;
    while (!atEnd() && isBareKeyChar(peek()))
        ++m_pos;
    if (m_pos == begin)
        return std::unexpected(error(QStringLiteral("expected a key")));
    return QString::fromUtf8(m_bytes.sliced(begin, m_pos - begin));
}

std::expected<QStringList, ParseError> Parser::parseKeyPath(Span &span)
{
    QStringList path;
    span.begin = m_pos;
    for (;;) {
        const auto segment = parseKeySegment();
        if (!segment)
            return std::unexpected(segment.error());
        path.append(*segment);
        span.end = m_pos;
        skipSpaces();
        if (peek() == '.') {
            ++m_pos;
            skipSpaces();
            continue;
        }
        break;
    }
    return path;
}

std::expected<QString, ParseError> Parser::decodeEscapes(const QByteArray &raw, bool multiline)
{
    QString out;
    out.reserve(raw.size());
    const QString text = QString::fromUtf8(raw);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != u'\\') {
            out.append(c);
            continue;
        }
        if (++i >= text.size())
            return std::unexpected(error(QStringLiteral("string ends with a dangling backslash")));
        const QChar esc = text.at(i);
        switch (esc.unicode()) {
        case u'b': out.append(u'\b'); break;
        case u't': out.append(u'\t'); break;
        case u'n': out.append(u'\n'); break;
        case u'f': out.append(u'\f'); break;
        case u'r': out.append(u'\r'); break;
        case u'"': out.append(u'"'); break;
        case u'\\': out.append(u'\\'); break;
        case u'u':
        case u'U': {
            const int digits = esc == u'u' ? 4 : 8;
            if (i + digits >= text.size())
                return std::unexpected(error(QStringLiteral("truncated \\u escape")));
            bool ok = false;
            const char32_t code = text.sliced(i + 1, digits).toUInt(&ok, 16);
            if (!ok)
                return std::unexpected(error(QStringLiteral("invalid \\u escape")));
            out.append(QString::fromUcs4(&code, 1));
            i += digits;
            break;
        }
        default:
            if (multiline && (esc == u'\n' || esc == u'\r' || esc == u' ' || esc == u'\t')) {
                // Line-ending backslash: swallow the newline and the whitespace
                // that follows it.
                while (i < text.size()
                    && (text.at(i) == u' ' || text.at(i) == u'\t' || text.at(i) == u'\n'
                        || text.at(i) == u'\r'))
                    ++i;
                --i;
                break;
            }
            return std::unexpected(
                error(QStringLiteral("unknown escape sequence \\%1").arg(esc)));
        }
    }
    return out;
}

std::expected<Value, ParseError> Parser::parseString()
{
    Value value;
    value.kind = ValueKind::String;
    value.span.begin = m_pos;

    const bool literal = peek() == '\'';
    const char quote = literal ? '\'' : '"';
    const bool multiline = peek(1) == quote && peek(2) == quote;

    if (multiline) {
        m_pos += 3;
        // A newline immediately after the opening delimiter is not content.
        if (peek() == '\r' && peek(1) == '\n')
            m_pos += 2;
        else if (peek() == '\n')
            ++m_pos;
        const qsizetype bodyBegin = m_pos;
        const char triple[4] = { quote, quote, quote, '\0' };
        for (;;) {
            if (atEnd())
                return std::unexpected(
                    errorAt(value.span.begin, QStringLiteral("unterminated multi-line string")));
            if (peek() == quote && peek(1) == quote && peek(2) == quote) {
                if (!literal && m_pos > bodyBegin && m_bytes.at(m_pos - 1) == '\\') {
                    ++m_pos;
                    continue;
                }
                break;
            }
            ++m_pos;
        }
        Q_UNUSED(triple);
        const QByteArray body = m_bytes.sliced(bodyBegin, m_pos - bodyBegin);
        m_pos += 3;
        value.span.end = m_pos;
        value.stringStyle
            = literal ? StringStyle::MultilineLiteral : StringStyle::MultilineBasic;
        if (literal) {
            value.string = QString::fromUtf8(body);
        } else {
            const auto decoded = decodeEscapes(body, true);
            if (!decoded)
                return std::unexpected(decoded.error());
            value.string = *decoded;
        }
        return value;
    }

    ++m_pos;
    const qsizetype bodyBegin = m_pos;
    for (;;) {
        if (atEnd() || peek() == '\n')
            return std::unexpected(
                errorAt(value.span.begin, QStringLiteral("unterminated string")));
        if (peek() == '\\' && !literal) {
            m_pos += 2;
            continue;
        }
        if (peek() == quote)
            break;
        ++m_pos;
    }
    const QByteArray body = m_bytes.sliced(bodyBegin, m_pos - bodyBegin);
    ++m_pos;
    value.span.end = m_pos;
    value.stringStyle = literal ? StringStyle::Literal : StringStyle::Basic;
    if (literal) {
        value.string = QString::fromUtf8(body);
    } else {
        const auto decoded = decodeEscapes(body, false);
        if (!decoded)
            return std::unexpected(decoded.error());
        value.string = *decoded;
    }
    return value;
}

std::expected<Value, ParseError> Parser::parseArray()
{
    Value value;
    value.kind = ValueKind::Array;
    value.span.begin = m_pos;
    ++m_pos; // '['

    for (;;) {
        const qsizetype beforeTrivia = m_pos;
        skipTrivia();
        if (m_bytes.sliced(beforeTrivia, m_pos - beforeTrivia).contains('\n'))
            value.multilineLayout = true;
        if (atEnd())
            return std::unexpected(
                errorAt(value.span.begin, QStringLiteral("unterminated array")));
        if (peek() == ']') {
            ++m_pos;
            break;
        }
        const auto item = parseValue();
        if (!item)
            return std::unexpected(item.error());
        value.items.append(*item);
        skipTrivia();
        if (peek() == ',') {
            ++m_pos;
            continue;
        }
        skipTrivia();
        if (peek() == ']') {
            ++m_pos;
            break;
        }
        return std::unexpected(error(QStringLiteral("expected ',' or ']' in array")));
    }
    value.span.end = m_pos;
    return value;
}

std::expected<Value, ParseError> Parser::parseInlineTable()
{
    Value value;
    value.kind = ValueKind::InlineTable;
    value.span.begin = m_pos;
    ++m_pos; // '{'

    for (;;) {
        skipSpaces();
        if (atEnd())
            return std::unexpected(
                errorAt(value.span.begin, QStringLiteral("unterminated inline table")));
        if (peek() == '}') {
            ++m_pos;
            break;
        }
        Span keySpan;
        const auto key = parseKeyPath(keySpan);
        if (!key)
            return std::unexpected(key.error());
        skipSpaces();
        if (peek() != '=')
            return std::unexpected(error(QStringLiteral("expected '=' in inline table")));
        ++m_pos;
        skipSpaces();
        const auto item = parseValue();
        if (!item)
            return std::unexpected(item.error());
        value.itemKeys.append(key->join(u'.'));
        value.items.append(*item);
        skipSpaces();
        if (peek() == ',') {
            ++m_pos;
            continue;
        }
        if (peek() == '}') {
            ++m_pos;
            break;
        }
        return std::unexpected(error(QStringLiteral("expected ',' or '}' in inline table")));
    }
    value.span.end = m_pos;
    return value;
}

std::expected<Value, ParseError> Parser::parseAtom()
{
    Value value;
    value.span.begin = m_pos;

    // Scan to the end of the bare token. Anything that is not an integer, float,
    // or boolean (a date, for instance) is kept as opaque raw text: OPE has no
    // use for it but must not lose or reject it.
    while (!atEnd()) {
        const char c = peek();
        if (c == ',' || c == ']' || c == '}' || c == '\n' || c == '\r' || c == '#')
            break;
        if (isSpaceOrTab(c)) {
            // Dates allow one space between date and time; otherwise whitespace
            // ends the token.
            break;
        }
        ++m_pos;
    }
    value.span.end = m_pos;
    const QByteArray raw = m_bytes.sliced(value.span.begin, value.span.end - value.span.begin);
    if (raw.isEmpty())
        return std::unexpected(error(QStringLiteral("expected a value")));

    if (raw == "true" || raw == "false") {
        value.kind = ValueKind::Boolean;
        value.boolean = raw == "true";
        return value;
    }

    QByteArray cleaned = raw;
    cleaned.replace('_', "");
    bool ok = false;
    if (!cleaned.contains('.') && !cleaned.contains('e') && !cleaned.contains('E')) {
        int base = 10;
        QByteArray digits = cleaned;
        if (cleaned.startsWith("0x") || cleaned.startsWith("0X")) {
            base = 16;
            digits = cleaned.sliced(2);
        } else if (cleaned.startsWith("0o")) {
            base = 8;
            digits = cleaned.sliced(2);
        } else if (cleaned.startsWith("0b")) {
            base = 2;
            digits = cleaned.sliced(2);
        }
        const qlonglong parsed = digits.toLongLong(&ok, base);
        if (ok) {
            value.kind = ValueKind::Integer;
            value.integer = parsed;
            return value;
        }
    }
    const double real = cleaned.toDouble(&ok);
    if (ok) {
        value.kind = ValueKind::Float;
        value.real = real;
        return value;
    }
    if (isNumberStart(raw.front()) || raw.front() == 'i' || raw.front() == 'n') {
        // inf / nan / date-time: opaque but valid.
        value.kind = ValueKind::Opaque;
        return value;
    }
    return std::unexpected(
        errorAt(value.span.begin,
            QStringLiteral("invalid value `%1`").arg(QString::fromUtf8(raw))));
}

std::expected<Value, ParseError> Parser::parseValue()
{
    if (atEnd())
        return std::unexpected(error(QStringLiteral("expected a value")));
    switch (peek()) {
    case '"':
    case '\'': return parseString();
    case '[': return parseArray();
    case '{': return parseInlineTable();
    default: return parseAtom();
    }
}

std::expected<KeyValue, ParseError> Parser::parseKeyValue()
{
    KeyValue pair;
    const qsizetype begin = m_pos;
    const auto key = parseKeyPath(pair.keySpan);
    if (!key)
        return std::unexpected(key.error());
    pair.key = *key;
    skipSpaces();
    if (peek() != '=')
        return std::unexpected(error(QStringLiteral("expected '=' after key `%1`")
                                         .arg(pair.key.join(u'.'))));
    ++m_pos;
    skipSpaces();
    const auto value = parseValue();
    if (!value)
        return std::unexpected(value.error());
    pair.value = *value;
    pair.span = { begin, m_pos };
    return pair;
}

std::expected<void, ParseError> Parser::run(QList<KeyValue> &rootPairs, QList<Table> &tables)
{
    // A UTF-8 BOM is not part of the document.
    if (m_size >= 3 && m_bytes.startsWith("\xEF\xBB\xBF"))
        m_pos = 3;

    // Index rather than pointer: appending to `tables` reallocates, and a
    // pointer into it would dangle the moment a second table header appeared.
    qsizetype current = -1;
    for (;;) {
        skipTrivia();
        if (atEnd())
            break;

        if (peek() == '[') {
            Table table;
            table.headerSpan.begin = m_pos;
            table.isArrayElement = peek(1) == '[';
            m_pos += table.isArrayElement ? 2 : 1;
            skipSpaces();
            Span keySpan;
            const auto path = parseKeyPath(keySpan);
            if (!path)
                return std::unexpected(path.error());
            table.path = *path;
            skipSpaces();
            const char *closer = table.isArrayElement ? "]]" : "]";
            if (!startsWith(closer))
                return std::unexpected(error(
                    QStringLiteral("expected `%1` to close the table header").arg(closer)));
            m_pos += table.isArrayElement ? 2 : 1;
            table.headerSpan.end = m_pos;
            table.span = table.headerSpan;
            if (auto ok = expectLineEnd("table header"); !ok)
                return std::unexpected(ok.error());
            tables.append(table);
            current = tables.size() - 1;
            continue;
        }

        const auto pair = parseKeyValue();
        if (!pair)
            return std::unexpected(pair.error());
        const QByteArray context
            = QStringLiteral("value for `%1`").arg(pair->key.join(u'.')).toUtf8();
        if (auto ok = expectLineEnd(context.constData()); !ok)
            return std::unexpected(ok.error());
        if (current >= 0) {
            tables[current].pairs.append(*pair);
            tables[current].span.end = pair->span.end;
        } else {
            rootPairs.append(*pair);
        }
    }
    return {};
}

} // namespace

QString ParseError::formatted() const
{
    return QStringLiteral("line %1, column %2: %3").arg(line).arg(column).arg(message);
}

QStringList Value::toStringList() const
{
    QStringList out;
    if (kind == ValueKind::String) {
        out.append(string);
        return out;
    }
    for (const Value &item : items) {
        if (item.kind == ValueKind::String)
            out.append(item.string);
    }
    return out;
}

const KeyValue *Table::find(QStringView key) const
{
    for (const KeyValue &pair : pairs) {
        if (pair.key.size() == 1 && pair.key.front() == key)
            return &pair;
    }
    return nullptr;
}

const KeyValue *Document::rootPair(QStringView key) const
{
    for (const KeyValue &pair : m_rootPairs) {
        if (pair.key.size() == 1 && pair.key.front() == key)
            return &pair;
    }
    return nullptr;
}

const Table *Document::table(const QStringList &path) const
{
    for (const Table &table : m_tables) {
        if (table.path == path)
            return &table;
    }
    return nullptr;
}

QList<const Table *> Document::tablesUnder(const QStringList &prefix) const
{
    QList<const Table *> out;
    for (const Table &table : m_tables) {
        if (table.path.size() <= prefix.size())
            continue;
        if (table.path.first(prefix.size()) == prefix)
            out.append(&table);
    }
    return out;
}

QList<const Table *> Document::arrayTables(const QStringList &path) const
{
    QList<const Table *> out;
    for (const Table &table : m_tables) {
        if (table.isArrayElement && table.path == path)
            out.append(&table);
    }
    return out;
}

qsizetype Document::rootPairsEnd() const
{
    qsizetype end = 0;
    for (const KeyValue &pair : m_rootPairs)
        end = std::max(end, pair.span.end);
    return end;
}

qsizetype Document::tableEnd(const Table &table) const { return table.span.end; }

qsizetype Document::lineStart(qsizetype offset) const
{
    offset = std::clamp<qsizetype>(offset, 0, m_bytes.size());
    const qsizetype found = m_bytes.lastIndexOf('\n', offset > 0 ? offset - 1 : 0);
    return found < 0 ? 0 : found + 1;
}

qsizetype Document::lineEnd(qsizetype offset) const
{
    const qsizetype found = m_bytes.indexOf('\n', std::clamp<qsizetype>(offset, 0, m_bytes.size()));
    return found < 0 ? m_bytes.size() : found + 1;
}

int Document::lineNumberAt(qsizetype offset) const
{
    int line = 1;
    for (qsizetype i = 0; i < offset && i < m_bytes.size(); ++i) {
        if (m_bytes.at(i) == '\n')
            ++line;
    }
    return line;
}

QByteArray Document::textOf(Span span) const
{
    if (!span.isValid())
        return {};
    const qsizetype begin = std::clamp<qsizetype>(span.begin, 0, m_bytes.size());
    const qsizetype end = std::clamp<qsizetype>(span.end, begin, m_bytes.size());
    return m_bytes.sliced(begin, end - begin);
}

std::expected<Document, ParseError> parse(QByteArray bytes)
{
    Document doc;
    doc.m_bytes = std::move(bytes);
    Parser parser(doc.m_bytes);
    if (auto ok = parser.run(doc.m_rootPairs, doc.m_tables); !ok)
        return std::unexpected(ok.error());
    return doc;
}

void Edit::replace(Span span, const QByteArray &text)
{
    if (!span.isValid())
        return;
    m_ops.append({ span.begin, span.end, text, m_sequence++ });
}

void Edit::insert(qsizetype at, const QByteArray &text)
{
    m_ops.append({ at, at, text, m_sequence++ });
}

void Edit::erase(Span span)
{
    if (span.isValid())
        m_ops.append({ span.begin, span.end, {}, m_sequence++ });
}

QByteArray Edit::apply(const QByteArray &original) const
{
    QList<Op> ops = m_ops;
    std::stable_sort(ops.begin(), ops.end(), [](const Op &a, const Op &b) {
        if (a.begin != b.begin)
            return a.begin < b.begin;
        return a.sequence < b.sequence;
    });

    QByteArray out;
    out.reserve(original.size() + 256);
    qsizetype cursor = 0;
    for (const Op &op : ops) {
        const qsizetype begin = std::clamp<qsizetype>(op.begin, 0, original.size());
        const qsizetype end = std::clamp<qsizetype>(op.end, begin, original.size());
        if (begin < cursor) {
            // Overlapping edits are a programming error; keeping the earlier one
            // is safer than emitting a corrupted file.
            continue;
        }
        out.append(original.sliced(cursor, begin - cursor));
        out.append(op.text);
        cursor = end;
    }
    out.append(original.sliced(cursor));
    return out;
}

QByteArray emitBasicString(const QString &text)
{
    QByteArray out = "\"";
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'"': out += "\\\""; break;
        case u'\\': out += "\\\\"; break;
        case u'\n': out += "\\n"; break;
        case u'\t': out += "\\t"; break;
        case u'\r': out += "\\r"; break;
        case u'\b': out += "\\b"; break;
        case u'\f': out += "\\f"; break;
        default: out += QString(c).toUtf8(); break;
        }
    }
    out += "\"";
    return out;
}

QByteArray emitMultilineString(const QString &body)
{
    QByteArray out = "\"\"\"\n";
    out += body.toUtf8();
    if (!body.endsWith(u'\n'))
        out += "\n";
    out += "\"\"\"";
    return out;
}

QByteArray emitStringArrayInline(const QStringList &items)
{
    QByteArray out = "[";
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += emitBasicString(items.at(i));
    }
    out += "]";
    return out;
}

QByteArray emitStringArrayBlock(const QStringList &items, int indent)
{
    if (items.isEmpty())
        return "[]";
    QByteArray out = "[\n";
    const QByteArray pad(indent, ' ');
    for (const QString &item : items) {
        out += pad;
        out += emitBasicString(item);
        out += ",\n";
    }
    out += "]";
    return out;
}

QByteArray emitIntArray(const QList<int> &items)
{
    QByteArray out = "[";
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += QByteArray::number(items.at(i));
    }
    out += "]";
    return out;
}

QByteArray emitKeySegment(const QString &segment)
{
    const QByteArray utf8 = segment.toUtf8();
    const bool bare = !utf8.isEmpty()
        && std::all_of(utf8.begin(), utf8.end(), [](char c) { return isBareKeyChar(c); });
    return bare ? utf8 : emitBasicString(segment);
}

} // namespace ope::toml
