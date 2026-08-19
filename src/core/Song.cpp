// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Song.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace ope {
namespace {

/// Canonical order for top-level keys in a freshly written file, and the order
/// used to choose an insertion point when a key is added to an existing file.
const QStringList &canonicalHeaderOrder()
{
    static const QStringList order {
        QStringLiteral("title"),
        QStringLiteral("subtitle"),
        QStringLiteral("language"),
        QStringLiteral("active"),
        QStringLiteral("copyrights"),
        QStringLiteral("key_signature"),
        QStringLiteral("time_sig_numerator"),
        QStringLiteral("time_sig_denominator"),
        QStringLiteral("tempo_bpm"),
        QStringLiteral("verse_count"),
        QStringLiteral("default_verses"),
        QStringLiteral("phrase_breaks"),
        QStringLiteral("optional_phrase_breaks"),
        QStringLiteral("non_breaking_phrase_breaks"),
        QStringLiteral("commentary"),
        QStringLiteral("converge_verses"),
    };
    return order;
}

const QStringList &canonicalPartOrder()
{
    static const QStringList order {
        QStringLiteral("choral_type"),
        QStringLiteral("clef"),
        QStringLiteral("staff_number"),
        QStringLiteral("splice_lyrics_into"),
        QStringLiteral("suppress_verses"),
        QStringLiteral("suppress_verses_when"),
        QStringLiteral("notes"),
    };
    return order;
}

const QStringList &knownPartKeys() { return canonicalPartOrder(); }

QList<PhraseBreak> parseBreaks(const QStringList &entries)
{
    QList<PhraseBreak> out;
    for (const QString &entry : entries) {
        if (const auto parsed = PhraseBreak::fromString(entry))
            out.append(*parsed);
    }
    return out;
}

QStringList breaksToStrings(const QList<PhraseBreak> &breaks)
{
    QStringList out;
    out.reserve(breaks.size());
    for (const PhraseBreak &item : breaks)
        out.append(item.toString());
    return out;
}

/// Re-emit an array in the style it was originally written in, so a file that
/// used a one-per-line block keeps its block and one that used a single line
/// keeps its single line.
QByteArray emitStringArrayLike(const toml::Value &original, const QStringList &items)
{
    if (original.kind == toml::ValueKind::Array && original.multilineLayout)
        return toml::emitStringArrayBlock(items);
    return toml::emitStringArrayInline(items);
}

void bindString(Field<QString> &field, const toml::KeyValue *pair)
{
    if (pair && pair->value.kind == toml::ValueKind::String)
        field.bind(pair->value.string, pair->value.span);
}

void bindInt(Field<int> &field, const toml::KeyValue *pair)
{
    if (pair && pair->value.kind == toml::ValueKind::Integer)
        field.bind(static_cast<int>(pair->value.integer), pair->value.span);
}

void bindBool(Field<bool> &field, const toml::KeyValue *pair)
{
    if (pair && pair->value.kind == toml::ValueKind::Boolean)
        field.bind(pair->value.boolean, pair->value.span);
}

void bindStringList(Field<QStringList> &field, const toml::KeyValue *pair)
{
    if (pair && pair->value.kind == toml::ValueKind::Array)
        field.bind(pair->value.toStringList(), pair->value.span);
}

void bindBreaks(Field<QList<PhraseBreak>> &field, const toml::KeyValue *pair)
{
    if (pair && pair->value.kind == toml::ValueKind::Array)
        field.bind(parseBreaks(pair->value.toStringList()), pair->value.span);
}

void bindIntList(Field<QList<int>> &field, const toml::KeyValue *pair)
{
    if (!pair || pair->value.kind != toml::ValueKind::Array)
        return;
    QList<int> values;
    for (const toml::Value &item : pair->value.items) {
        if (item.kind == toml::ValueKind::Integer)
            values.append(static_cast<int>(item.integer));
    }
    field.bind(values, pair->value.span);
}

} // namespace

QString PhraseBreak::toString() const
{
    return QStringLiteral("%1:%2").arg(measure).arg(tick);
}

std::optional<PhraseBreak> PhraseBreak::fromString(QStringView text)
{
    const qsizetype colon = text.indexOf(u':');
    if (colon < 0)
        return std::nullopt;
    bool okMeasure = false;
    bool okTick = false;
    const int measure = text.first(colon).toInt(&okMeasure);
    const int tick = text.sliced(colon + 1).toInt(&okTick);
    if (!okMeasure || !okTick)
        return std::nullopt;
    return PhraseBreak { measure, tick };
}

QStringList LyricSection::syllables() const
{
    QStringList out;
    const QStringList tokens = rawText.split(QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token == QLatin1String("--"))
            continue;
        out.append(token);
    }
    return out;
}

QStringList LyricSection::syllableTypes() const
{
    // Mirrors parse_lyrics: a syllable followed by `--` begins or continues a
    // word; otherwise it ends one or stands alone.
    QStringList types;
    const QStringList tokens = rawText.split(QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    bool inWord = false;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i) == QLatin1String("--"))
            continue;
        const bool hasNextHyphen
            = i + 1 < tokens.size() && tokens.at(i + 1) == QLatin1String("--");
        if (hasNextHyphen)
            types.append(inWord ? QStringLiteral("middle") : QStringLiteral("begin"));
        else
            types.append(inWord ? QStringLiteral("end") : QStringLiteral("single"));
        inWord = hasNextHyphen;
    }
    return types;
}

int Part::sortRank() const
{
    const QString lower = name.toLower();
    qsizetype digitAt = lower.size();
    for (qsizetype i = 0; i < lower.size(); ++i) {
        if (lower.at(i).isDigit()) {
            digitAt = i;
            break;
        }
    }
    const QString base = lower.first(digitAt).trimmed();
    int rank = 99;
    if (base == QLatin1String("soprano"))
        rank = 1;
    else if (base == QLatin1String("alto"))
        rank = 2;
    else if (base == QLatin1String("tenor"))
        rank = 3;
    else if (base == QLatin1String("bass"))
        rank = 4;
    const int suffix = lower.sliced(digitAt).trimmed().toInt();
    return rank * 100 + suffix;
}

void Part::reparse()
{
    tokenIssues.clear();
    const QList<int> layout = stream.lineLayout();
    stream = NoteStream::parse(notes.valueOr(QString()), &tokenIssues);
    if (!layout.isEmpty() && stream.lineLayout().isEmpty())
        stream.setLineLayout(layout);
}

const Part *SongDocument::part(QStringView name) const
{
    for (const Part &item : parts) {
        if (item.name == name)
            return &item;
    }
    return nullptr;
}

Part *SongDocument::part(QStringView name)
{
    for (Part &item : parts) {
        if (item.name == name)
            return &item;
    }
    return nullptr;
}

QList<const Part *> SongDocument::partsInDisplayOrder() const
{
    QList<const Part *> out;
    out.reserve(parts.size());
    for (const Part &item : parts)
        out.append(&item);
    std::stable_sort(out.begin(), out.end(), [](const Part *a, const Part *b) {
        if (a->sortRank() != b->sortRank())
            return a->sortRank() < b->sortRank();
        return a->name.toLower() < b->name.toLower();
    });
    return out;
}

std::pair<int, int> SongDocument::timeSigForMeasure(int measureNumber) const
{
    const std::pair<int, int> fallback { timeSigNumerator.valueOr(4),
        timeSigDenominator.valueOr(4) };
    if (!timeSigChanges.present())
        return fallback;
    for (const TimeSigChange &change : *timeSigChanges) {
        const int begin = change.measure;
        const int end = change.measure + change.duration;
        if (measureNumber >= begin && measureNumber < end)
            return { change.numerator, change.denominator };
    }
    return fallback;
}

int SongDocument::expectedTicksForMeasure(int measureNumber) const
{
    const auto [num, den] = timeSigForMeasure(measureNumber);
    return ticks::forTimeSignature(num, den);
}

int SongDocument::measureCount() const
{
    int count = 0;
    for (const Part &item : parts)
        count = std::max<int>(count, static_cast<int>(item.stream.measureCount()));
    return count;
}

QList<PhraseBreak> SongDocument::allPhraseBreaks() const
{
    QList<PhraseBreak> out = phraseBreaks.valueOr({});
    out.append(optionalPhraseBreaks.valueOr({}));
    out.append(nonBreakingPhraseBreaks.valueOr({}));
    std::sort(out.begin(), out.end());
    return out;
}

QStringList SongDocument::verseKeys() const
{
    QStringList keys;
    for (auto it = lyrics.constBegin(); it != lyrics.constEnd(); ++it) {
        if (isVerseKey(it.key()))
            keys.append(it.key());
    }
    std::sort(keys.begin(), keys.end(),
        [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    return keys;
}

QList<int> SongDocument::effectiveDefaultVerses() const
{
    QList<int> all;
    const int count = std::max(0, verseCount.valueOr(0));
    all.reserve(count);
    for (int verse = 1; verse <= count; ++verse)
        all.append(verse);

    if (!defaultVerses.present())
        return all;

    QList<int> selected;
    for (const int verse : *defaultVerses) {
        if (verse >= 1 && verse <= count && !selected.contains(verse))
            selected.append(verse);
    }
    std::sort(selected.begin(), selected.end());
    return selected.isEmpty() ? all : selected;
}

bool SongDocument::isDefaultVerse(int verse) const
{
    // Absence means every verse even when verse_count and the lyric tables are
    // temporarily inconsistent in an in-progress edit.
    if (!defaultVerses.present())
        return true;
    return effectiveDefaultVerses().contains(verse);
}

QStringList SongDocument::sharedKeys() const
{
    QStringList keys;
    for (auto it = lyrics.constBegin(); it != lyrics.constEnd(); ++it) {
        if (isSharedKey(it.key()))
            keys.append(it.key());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool SongDocument::usesSharedLyrics() const
{
    for (auto it = lyrics.constBegin(); it != lyrics.constEnd(); ++it) {
        if (isSharedKey(it.key()))
            return true;
    }
    for (const Part &item : parts) {
        for (auto it = item.lyrics.constBegin(); it != item.lyrics.constEnd(); ++it) {
            if (isSharedKey(it.key()))
                return true;
        }
    }
    return false;
}

bool SongDocument::effectiveConvergeVerses() const
{
    // Authoring with shared sections declares the print-once intent, so it
    // defaults the flag on; an explicit value still wins.
    return convergeVerses.present() ? *convergeVerses : usesSharedLyrics();
}

bool SongDocument::isDirty() const
{
    if (!removedTables.isEmpty())
        return true;
    const auto anyDirty = [](auto &&...fields) { return (fields.dirty() || ...); };
    if (anyDirty(title, subtitle, active, declaredLanguage, keySignature, timeSigNumerator,
            timeSigDenominator, tempoBpm, verseCount, defaultVerses, copyrights, commentary,
            convergeVerses, phraseBreaks, optionalPhraseBreaks, nonBreakingPhraseBreaks,
            timeSigChanges))
        return true;
    for (auto it = lyrics.constBegin(); it != lyrics.constEnd(); ++it) {
        if (it->dirty)
            return true;
    }
    for (const Part &item : parts) {
        if (item.isNew)
            return true;
        if (anyDirty(item.choralType, item.clef, item.staffNumber, item.notes,
                item.suppressVerses, item.suppressVersesWhen, item.spliceLyricsInto))
            return true;
        if (item.stream.anyDirty())
            return true;
        for (auto it = item.lyrics.constBegin(); it != item.lyrics.constEnd(); ++it) {
            if (it->dirty)
                return true;
        }
    }
    return false;
}

void SongDocument::markClean()
{
    removedTables.clear();
    const auto clean = [](auto &...fields) { (fields.markClean(), ...); };
    clean(title, subtitle, active, declaredLanguage, keySignature, timeSigNumerator,
        timeSigDenominator, tempoBpm, verseCount, defaultVerses, copyrights, commentary,
        convergeVerses, phraseBreaks, optionalPhraseBreaks, nonBreakingPhraseBreaks,
        timeSigChanges);
    for (auto it = lyrics.begin(); it != lyrics.end(); ++it)
        it->dirty = false;
    for (Part &item : parts) {
        clean(item.choralType, item.clef, item.staffNumber, item.notes, item.suppressVerses,
            item.suppressVersesWhen, item.spliceLyricsInto);
        item.isNew = false;
        for (auto it = item.lyrics.begin(); it != item.lyrics.end(); ++it)
            it->dirty = false;
        for (Measure &measure : item.stream.measures()) {
            for (Event &event : measure.events)
                event.dirty = false;
        }
    }
}

void SongDocument::removePartLyric(Part &part, const QString &key)
{
    const auto it = part.lyrics.constFind(key);
    if (it == part.lyrics.constEnd())
        return;
    if (it->tableSpan.isValid())
        removedTables.append(it->tableSpan);
    part.lyrics.remove(key);
}

void SongDocument::removeGlobalLyric(const QString &key)
{
    const auto it = lyrics.constFind(key);
    if (it == lyrics.constEnd())
        return;
    if (it->tableSpan.isValid())
        removedTables.append(it->tableSpan);
    lyrics.remove(key);
}

void SongDocument::setLyric(
    QMap<QString, LyricSection> &map, const QString &key, const QString &text)
{
    if (const auto existing = map.constFind(key);
        existing != map.constEnd() && existing->rawText == text)
        return;
    LyricSection &section = map[key];
    section.rawText = text;
    section.dirty = true;
}

QStringList SongDocument::orderedLyricKeys(const QStringList &keys)
{
    const auto rank = [](const QString &key) {
        if (isVerseKey(key))
            return 0;
        if (isChorusKey(key))
            return 1;
        if (isCodaKey(key))
            return 2;
        if (isSharedKey(key))
            return 3;
        return 4;
    };
    QStringList sorted = keys;
    std::stable_sort(sorted.begin(), sorted.end(), [&rank](const QString &a, const QString &b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        if (isVerseKey(a) && isVerseKey(b))
            return a.toInt() < b.toInt();
        if (isSharedKey(a) && isSharedKey(b))
            return QStringView(a).sliced(1).toInt() < QStringView(b).sliced(1).toInt();
        return a < b;
    });
    return sorted;
}

bool SongDocument::isChorusKey(QStringView key)
{
    return key.compare(QLatin1String("chorus"), Qt::CaseInsensitive) == 0;
}

bool SongDocument::isCodaKey(QStringView key)
{
    return key.compare(QLatin1String("coda"), Qt::CaseInsensitive) == 0;
}

bool SongDocument::isSharedKey(QStringView key)
{
    if (key.size() < 2 || key.front() != u's')
        return false;
    for (qsizetype i = 1; i < key.size(); ++i) {
        if (!key.at(i).isDigit())
            return false;
    }
    return true;
}

bool SongDocument::isVerseKey(QStringView key)
{
    if (key.isEmpty())
        return false;
    for (const QChar c : key) {
        if (!c.isDigit())
            return false;
    }
    return true;
}

QString LoadError::formatted() const
{
    if (!message.isEmpty())
        return QStringLiteral("%1: %2").arg(path, message);
    return QStringLiteral("%1: %2").arg(path, parse.formatted());
}

namespace io {
namespace {

void loadLyricMap(QMap<QString, LyricSection> &target, const toml::Document &doc,
    const QStringList &prefix)
{
    for (const toml::Table *table : doc.tablesUnder(prefix)) {
        if (table->path.size() != prefix.size() + 1)
            continue;
        const QString key = table->path.last();
        if (const toml::KeyValue *text = table->find(QStringLiteral("text"));
            text && text->value.kind == toml::ValueKind::String) {
            LyricSection section;
            section.rawText = text->value.string;
            section.span = text->value.span;
            section.tableSpan = table->span;
            target.insert(key, section);
        }
    }
}

} // namespace

std::expected<SongDocument, LoadError> loadBytes(const QString &path, QByteArray bytes)
{
    auto parsed = toml::parse(bytes);
    if (!parsed) {
        LoadError err;
        err.path = path;
        err.parse = parsed.error();
        return std::unexpected(err);
    }

    SongDocument doc;
    doc.path = path;
    doc.originalBytes = bytes;
    doc.source = std::move(*parsed);

    const QFileInfo info(path);
    doc.workId = info.dir().dirName().toInt();
    const QString base = info.fileName();
    if (base.startsWith(QLatin1String("song_")) && base.endsWith(QLatin1String(".toml"))) {
        doc.isOverlay = true;
        doc.language = base.sliced(5, base.size() - 5 - 5);
    }

    const toml::Document &src = doc.source;
    bindString(doc.title, src.rootPair(QStringLiteral("title")));
    bindString(doc.subtitle, src.rootPair(QStringLiteral("subtitle")));
    bindBool(doc.active, src.rootPair(QStringLiteral("active")));
    bindString(doc.declaredLanguage, src.rootPair(QStringLiteral("language")));
    bindString(doc.keySignature, src.rootPair(QStringLiteral("key_signature")));
    bindInt(doc.timeSigNumerator, src.rootPair(QStringLiteral("time_sig_numerator")));
    bindInt(doc.timeSigDenominator, src.rootPair(QStringLiteral("time_sig_denominator")));
    bindInt(doc.tempoBpm, src.rootPair(QStringLiteral("tempo_bpm")));
    bindInt(doc.verseCount, src.rootPair(QStringLiteral("verse_count")));
    bindIntList(doc.defaultVerses, src.rootPair(QStringLiteral("default_verses")));
    bindStringList(doc.copyrights, src.rootPair(QStringLiteral("copyrights")));
    bindString(doc.commentary, src.rootPair(QStringLiteral("commentary")));
    bindBool(doc.convergeVerses, src.rootPair(QStringLiteral("converge_verses")));
    bindBreaks(doc.phraseBreaks, src.rootPair(QStringLiteral("phrase_breaks")));
    bindBreaks(doc.optionalPhraseBreaks, src.rootPair(QStringLiteral("optional_phrase_breaks")));
    bindBreaks(
        doc.nonBreakingPhraseBreaks, src.rootPair(QStringLiteral("non_breaking_phrase_breaks")));

    if (!doc.isOverlay)
        doc.language = doc.declaredLanguage.valueOr(QStringLiteral("en"));

    // time_sig_changes: an array of tables, edited as a block.
    const QList<const toml::Table *> changes
        = src.arrayTables({ QStringLiteral("time_sig_changes") });
    if (!changes.isEmpty()) {
        QList<TimeSigChange> parsedChanges;
        toml::Span span { changes.first()->span.begin, changes.last()->span.end };
        for (const toml::Table *table : changes) {
            TimeSigChange change;
            if (const auto *pair = table->find(QStringLiteral("measure")))
                change.measure = static_cast<int>(pair->value.integer);
            if (const auto *pair = table->find(QStringLiteral("numerator")))
                change.numerator = static_cast<int>(pair->value.integer);
            if (const auto *pair = table->find(QStringLiteral("denominator")))
                change.denominator = static_cast<int>(pair->value.integer);
            if (const auto *pair = table->find(QStringLiteral("duration")))
                change.duration = static_cast<int>(pair->value.integer);
            parsedChanges.append(change);
        }
        doc.timeSigChanges.bind(parsedChanges, span);
    }

    // Unknown root keys are preserved by span-splicing; record them for the
    // Problems panel so a format addition is visible rather than invisible.
    for (const toml::KeyValue &pair : src.rootPairs()) {
        const QString key = pair.joinedKey();
        if (!canonicalHeaderOrder().contains(key))
            doc.unknownKeys.append(key);
    }

    // Parts.
    for (const toml::Table *table : src.tablesUnder({ QStringLiteral("parts") })) {
        if (table->path.size() != 2)
            continue;
        Part part;
        part.name = table->path.at(1);
        part.tableSpan = table->span;
        part.tablePath = table->path;
        bindString(part.choralType, table->find(QStringLiteral("choral_type")));
        bindString(part.clef, table->find(QStringLiteral("clef")));
        bindInt(part.staffNumber, table->find(QStringLiteral("staff_number")));
        bindString(part.notes, table->find(QStringLiteral("notes")));
        bindIntList(part.suppressVerses, table->find(QStringLiteral("suppress_verses")));
        bindStringList(
            part.suppressVersesWhen, table->find(QStringLiteral("suppress_verses_when")));
        bindString(part.spliceLyricsInto, table->find(QStringLiteral("splice_lyrics_into")));
        for (const toml::KeyValue &pair : table->pairs) {
            if (!knownPartKeys().contains(pair.joinedKey()))
                doc.unknownKeys.append(QStringLiteral("parts.%1.%2").arg(part.name,
                    pair.joinedKey()));
        }
        loadLyricMap(part.lyrics, src, { QStringLiteral("parts"), part.name,
                                           QStringLiteral("lyrics") });
        part.reparse();
        doc.parts.append(part);
    }

    loadLyricMap(doc.lyrics, src, { QStringLiteral("lyrics") });
    return doc;
}

std::expected<SongDocument, LoadError> load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LoadError err;
        err.path = path;
        err.message = file.errorString();
        return std::unexpected(err);
    }
    return loadBytes(path, file.readAll());
}

namespace {

/// Replace, insert, or delete one top-level key.
void spliceRootField(toml::Edit &edit, const SongDocument &doc, const QString &key,
    bool present, bool dirty, toml::Span span, const QByteArray &emitted)
{
    if (!dirty)
        return;
    if (present && span.isValid()) {
        edit.replace(span, emitted);
        return;
    }
    if (present) {
        // A new key: insert after the last existing root pair, or at the top of
        // the file when there is none. Every root key must precede the first
        // table header, or TOML would assign it to that table.
        const qsizetype anchor = doc.source.rootPairsEnd();
        const qsizetype at = anchor > 0 ? doc.source.lineEnd(anchor) : 0;
        edit.insert(at, toml::emitKeySegment(key) + " = " + emitted + "\n");
        return;
    }
    if (span.isValid()) {
        // Removing a key takes its whole line with it.
        const qsizetype from = doc.source.lineStart(span.begin);
        const qsizetype to = doc.source.lineEnd(span.end);
        edit.erase({ from, to });
    }
}

void splicePartField(toml::Edit &edit, const SongDocument &doc, const Part &part,
    const QString &key, bool present, bool dirty, toml::Span span, const QByteArray &emitted)
{
    if (!dirty)
        return;
    if (present && span.isValid()) {
        edit.replace(span, emitted);
        return;
    }
    if (present) {
        const toml::Table *table = doc.source.table(part.tablePath);
        const qsizetype at = table ? doc.source.lineEnd(table->span.end) : doc.originalBytes.size();
        edit.insert(at, toml::emitKeySegment(key) + " = " + emitted + "\n");
        return;
    }
    if (span.isValid()) {
        edit.erase({ doc.source.lineStart(span.begin), doc.source.lineEnd(span.end) });
    }
}

QByteArray emitPartTable(const Part &part)
{
    QByteArray out = "[parts." + toml::emitKeySegment(part.name) + "]\n";
    if (part.choralType.present())
        out += "choral_type = " + toml::emitBasicString(*part.choralType) + "\n";
    if (part.clef.present())
        out += "clef = " + toml::emitBasicString(*part.clef) + "\n";
    if (part.staffNumber.present())
        out += "staff_number = " + QByteArray::number(*part.staffNumber) + "\n";
    if (part.spliceLyricsInto.present())
        out += "splice_lyrics_into = " + toml::emitBasicString(*part.spliceLyricsInto) + "\n";
    if (part.suppressVerses.present())
        out += "suppress_verses = " + toml::emitIntArray(*part.suppressVerses) + "\n";
    if (part.suppressVersesWhen.present())
        out += "suppress_verses_when = " + toml::emitStringArrayInline(*part.suppressVersesWhen)
            + "\n";
    out += "notes = " + toml::emitMultilineString(part.stream.toSource()) + "\n";
    return out;
}

QByteArray emitLyricTable(const QStringList &path, const LyricSection &section)
{
    QByteArray header = "[";
    for (qsizetype i = 0; i < path.size(); ++i) {
        if (i > 0)
            header += ".";
        header += toml::emitKeySegment(path.at(i));
    }
    header += "]\n";
    return header + "text = " + toml::emitBasicString(section.rawText) + "\n";
}

/// Where a brand-new lyric table should go: beside the ones it belongs with —
/// after the last `[parts.X.lyrics.*]` of the same part, or after that part's
/// own table, or after the last `[lyrics.*]` for a global section. Appending
/// everything at the end of the file would be valid TOML and an unreadable diff.
qsizetype lyricTableAnchor(const SongDocument &doc, const QStringList &path)
{
    const QStringList prefix = path.first(path.size() - 1);
    qsizetype anchor = -1;
    for (const toml::Table *table : doc.source.tablesUnder(prefix)) {
        if (table->path.size() == prefix.size() + 1)
            anchor = std::max(anchor, doc.source.lineEnd(table->span.end));
    }
    if (anchor < 0 && prefix.size() == 3) {
        // parts.X.lyrics.KEY with no siblings yet: sit under the part itself.
        if (const toml::Table *part = doc.source.table(prefix.first(2)))
            anchor = doc.source.lineEnd(part->span.end);
    }
    return anchor < 0 ? doc.originalBytes.size() : anchor;
}

QByteArray emitTimeSigChanges(const QList<TimeSigChange> &changes)
{
    QByteArray out;
    for (const TimeSigChange &change : changes) {
        out += "[[time_sig_changes]]\n";
        out += "measure = " + QByteArray::number(change.measure) + "\n";
        out += "numerator = " + QByteArray::number(change.numerator) + "\n";
        out += "denominator = " + QByteArray::number(change.denominator) + "\n";
        out += "duration = " + QByteArray::number(change.duration) + "\n";
    }
    if (out.endsWith("\n"))
        out.chop(1);
    return out;
}

} // namespace

QByteArray serialize(const SongDocument &doc)
{
    // A merged view mixes spans from two files; writing it would splice the
    // overlay's bytes at the base file's offsets. Refuse identically in Debug
    // and Release; an assertion here used to abort the Debug regression suite.
    if (doc.isMergedView)
        return doc.originalBytes;

    toml::Edit edit;

    const auto stringField = [&](const QString &key, const Field<QString> &field) {
        spliceRootField(edit, doc, key, field.present(), field.dirty(), field.span(),
            field.present() ? toml::emitBasicString(*field) : QByteArray());
    };
    const auto intField = [&](const QString &key, const Field<int> &field) {
        spliceRootField(edit, doc, key, field.present(), field.dirty(), field.span(),
            field.present() ? QByteArray::number(*field) : QByteArray());
    };
    const auto boolField = [&](const QString &key, const Field<bool> &field) {
        spliceRootField(edit, doc, key, field.present(), field.dirty(), field.span(),
            field.present() ? (*field ? QByteArray("true") : QByteArray("false")) : QByteArray());
    };
    const auto breaksField = [&](const QString &key, const Field<QList<PhraseBreak>> &field) {
        QByteArray emitted;
        if (field.present()) {
            const toml::KeyValue *original = doc.source.rootPair(key);
            const QStringList items = breaksToStrings(*field);
            emitted = original ? emitStringArrayLike(original->value, items)
                               : toml::emitStringArrayInline(items);
        }
        spliceRootField(edit, doc, key, field.present(), field.dirty(), field.span(), emitted);
    };

    stringField(QStringLiteral("title"), doc.title);
    stringField(QStringLiteral("subtitle"), doc.subtitle);
    stringField(QStringLiteral("language"), doc.declaredLanguage);
    stringField(QStringLiteral("key_signature"), doc.keySignature);
    stringField(QStringLiteral("commentary"), doc.commentary);
    intField(QStringLiteral("time_sig_numerator"), doc.timeSigNumerator);
    intField(QStringLiteral("time_sig_denominator"), doc.timeSigDenominator);
    intField(QStringLiteral("tempo_bpm"), doc.tempoBpm);
    intField(QStringLiteral("verse_count"), doc.verseCount);
    if (doc.defaultVerses.dirty()) {
        spliceRootField(edit, doc, QStringLiteral("default_verses"),
            doc.defaultVerses.present(), true, doc.defaultVerses.span(),
            doc.defaultVerses.present() ? toml::emitIntArray(*doc.defaultVerses)
                                        : QByteArray());
    }
    boolField(QStringLiteral("active"), doc.active);
    boolField(QStringLiteral("converge_verses"), doc.convergeVerses);
    breaksField(QStringLiteral("phrase_breaks"), doc.phraseBreaks);
    breaksField(QStringLiteral("optional_phrase_breaks"), doc.optionalPhraseBreaks);
    breaksField(QStringLiteral("non_breaking_phrase_breaks"), doc.nonBreakingPhraseBreaks);

    if (doc.copyrights.dirty()) {
        QByteArray emitted;
        if (doc.copyrights.present()) {
            const toml::KeyValue *original = doc.source.rootPair(QStringLiteral("copyrights"));
            // Copyright blocks are conventionally one entry per line; keep that
            // for a list that was written inline only if it started that way.
            emitted = original ? emitStringArrayLike(original->value, *doc.copyrights)
                               : toml::emitStringArrayBlock(*doc.copyrights);
        }
        spliceRootField(edit, doc, QStringLiteral("copyrights"), doc.copyrights.present(),
            true, doc.copyrights.span(), emitted);
    }

    if (doc.timeSigChanges.dirty()) {
        if (doc.timeSigChanges.present()) {
            if (doc.timeSigChanges.span().isValid()) {
                edit.replace(doc.timeSigChanges.span(), emitTimeSigChanges(*doc.timeSigChanges));
            } else {
                edit.insert(doc.originalBytes.size(),
                    "\n" + emitTimeSigChanges(*doc.timeSigChanges) + "\n");
            }
        } else if (doc.timeSigChanges.span().isValid()) {
            qsizetype from = doc.source.lineStart(doc.timeSigChanges.span().begin);
            qsizetype to = doc.source.lineEnd(doc.timeSigChanges.span().end);
            if (to < doc.originalBytes.size() && doc.originalBytes.at(to) == '\n')
                ++to;
            else if (from >= 2 && doc.originalBytes.at(from - 1) == '\n'
                && doc.originalBytes.at(from - 2) == '\n')
                --from;
            edit.erase({ from, to });
        }
    }

    // Parts.
    for (const Part &part : doc.parts) {
        if (part.isNew) {
            edit.insert(doc.originalBytes.size(), "\n" + emitPartTable(part));
            continue;
        }
        splicePartField(edit, doc, part, QStringLiteral("choral_type"), part.choralType.present(),
            part.choralType.dirty(), part.choralType.span(),
            part.choralType.present() ? toml::emitBasicString(*part.choralType) : QByteArray());
        splicePartField(edit, doc, part, QStringLiteral("clef"), part.clef.present(),
            part.clef.dirty(), part.clef.span(),
            part.clef.present() ? toml::emitBasicString(*part.clef) : QByteArray());
        splicePartField(edit, doc, part, QStringLiteral("staff_number"), part.staffNumber.present(),
            part.staffNumber.dirty(), part.staffNumber.span(),
            part.staffNumber.present() ? QByteArray::number(*part.staffNumber) : QByteArray());
        splicePartField(edit, doc, part, QStringLiteral("splice_lyrics_into"),
            part.spliceLyricsInto.present(), part.spliceLyricsInto.dirty(),
            part.spliceLyricsInto.span(),
            part.spliceLyricsInto.present() ? toml::emitBasicString(*part.spliceLyricsInto)
                                            : QByteArray());
        splicePartField(edit, doc, part, QStringLiteral("suppress_verses"),
            part.suppressVerses.present(), part.suppressVerses.dirty(),
            part.suppressVerses.span(),
            part.suppressVerses.present() ? toml::emitIntArray(*part.suppressVerses)
                                          : QByteArray());
        splicePartField(edit, doc, part, QStringLiteral("suppress_verses_when"),
            part.suppressVersesWhen.present(), part.suppressVersesWhen.dirty(),
            part.suppressVersesWhen.span(),
            part.suppressVersesWhen.present()
                ? toml::emitStringArrayInline(*part.suppressVersesWhen)
                : QByteArray());

        // Notation: rewrite the string body only when a token actually changed,
        // so an untouched part keeps its exact line layout and spacing.
        if (part.notes.dirty() || part.stream.anyDirty()) {
            const toml::Table *table = doc.source.table(part.tablePath);
            const toml::KeyValue *pair = table ? table->find(QStringLiteral("notes")) : nullptr;
            const QByteArray emitted = toml::emitMultilineString(part.stream.toSource());
            if (pair)
                edit.replace(pair->value.span, emitted);
            else if (table)
                edit.insert(doc.source.lineEnd(table->span.end), "notes = " + emitted + "\n");
        }

        for (auto it = part.lyrics.constBegin(); it != part.lyrics.constEnd(); ++it) {
            if (!it->dirty)
                continue;
            const QStringList path { QStringLiteral("parts"), part.name,
                QStringLiteral("lyrics"), it.key() };
            if (it->span.isValid())
                edit.replace(it->span, toml::emitBasicString(it->rawText));
            else
                edit.insert(lyricTableAnchor(doc, path), "\n" + emitLyricTable(path, *it));
        }
    }

    // Global lyrics.
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (!it->dirty)
            continue;
        const QStringList path { QStringLiteral("lyrics"), it.key() };
        if (it->span.isValid())
            edit.replace(it->span, toml::emitBasicString(it->rawText));
        else
            edit.insert(lyricTableAnchor(doc, path), emitLyricTable(path, *it) + "\n");
    }

    // Deleted tables go last so their spans are not disturbed by the edits above
    // (Edit sorts by position, so ordering here only decides ties).
    for (const toml::Span &span : doc.removedTables) {
        if (!span.isValid())
            continue;
        qsizetype from = doc.source.lineStart(span.begin);
        qsizetype to = doc.source.lineEnd(span.end);
        // A block owns one of the blank lines around it: the one after it, or —
        // when it is the last thing in the file and there is no "after" — the one
        // before. Without this, deleting a section leaves a widening gap where
        // it used to be, and adding a section then deleting it again would not
        // restore the file.
        if (to < doc.originalBytes.size() && doc.originalBytes.at(to) == '\n') {
            ++to;
        } else if (from >= 2 && doc.originalBytes.at(from - 1) == '\n'
            && doc.originalBytes.at(from - 2) == '\n') {
            --from;
        }
        edit.erase({ from, to });
    }

    return edit.apply(doc.originalBytes);
}

QByteArray serializeFresh(const SongDocument &doc)
{
    QByteArray out;
    const auto line = [&out](const QByteArray &text) { out += text + "\n"; };

    if (doc.title.present())
        line("title = " + toml::emitBasicString(*doc.title));
    if (doc.subtitle.present())
        line("subtitle = " + toml::emitBasicString(*doc.subtitle));
    if (doc.declaredLanguage.present())
        line("language = " + toml::emitBasicString(*doc.declaredLanguage));
    if (doc.active.present())
        line(QByteArray("active = ") + (*doc.active ? "true" : "false"));
    if (doc.copyrights.present())
        line("copyrights = " + toml::emitStringArrayBlock(*doc.copyrights));
    if (doc.keySignature.present())
        line("key_signature = " + toml::emitBasicString(*doc.keySignature));
    if (doc.timeSigNumerator.present())
        line("time_sig_numerator = " + QByteArray::number(*doc.timeSigNumerator));
    if (doc.timeSigDenominator.present())
        line("time_sig_denominator = " + QByteArray::number(*doc.timeSigDenominator));
    if (doc.tempoBpm.present())
        line("tempo_bpm = " + QByteArray::number(*doc.tempoBpm));
    if (doc.verseCount.present())
        line("verse_count = " + QByteArray::number(*doc.verseCount));
    if (doc.defaultVerses.present())
        line("default_verses = " + toml::emitIntArray(*doc.defaultVerses));
    if (doc.phraseBreaks.present())
        line("phrase_breaks = "
            + toml::emitStringArrayInline(breaksToStrings(*doc.phraseBreaks)));
    if (doc.optionalPhraseBreaks.present())
        line("optional_phrase_breaks = "
            + toml::emitStringArrayInline(breaksToStrings(*doc.optionalPhraseBreaks)));
    if (doc.nonBreakingPhraseBreaks.present())
        line("non_breaking_phrase_breaks = "
            + toml::emitStringArrayInline(breaksToStrings(*doc.nonBreakingPhraseBreaks)));
    if (doc.commentary.present())
        line("commentary = " + toml::emitBasicString(*doc.commentary));
    if (doc.convergeVerses.present())
        line(QByteArray("converge_verses = ") + (*doc.convergeVerses ? "true" : "false"));

    if (doc.timeSigChanges.present() && !doc.timeSigChanges->isEmpty()) {
        out += "\n";
        out += emitTimeSigChanges(*doc.timeSigChanges) + "\n";
    }

    for (const Part *part : doc.partsInDisplayOrder()) {
        out += "\n";
        out += emitPartTable(*part);
        for (const QString &key : SongDocument::orderedLyricKeys(part->lyrics.keys())) {
            out += "\n";
            out += emitLyricTable(
                { QStringLiteral("parts"), part->name, QStringLiteral("lyrics"), key },
                part->lyrics.value(key));
        }
    }

    const auto emitLyric = [&](const QString &key) {
        const auto it = doc.lyrics.constFind(key);
        if (it == doc.lyrics.constEnd())
            return;
        out += "\n";
        out += emitLyricTable({ QStringLiteral("lyrics"), key }, *it);
    };
    for (const QString &key : doc.verseKeys())
        emitLyric(key);
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (SongDocument::isChorusKey(it.key()))
            emitLyric(it.key());
    }
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (SongDocument::isCodaKey(it.key()))
            emitLyric(it.key());
    }
    for (const QString &key : doc.sharedKeys())
        emitLyric(key);

    return out;
}

std::expected<void, QString> writeAtomically(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return std::unexpected(file.errorString());
    if (file.write(bytes) != bytes.size())
        return std::unexpected(file.errorString());
    if (!file.commit())
        return std::unexpected(file.errorString());
    return {};
}

} // namespace io

void materialiseOverlayLyrics(
    SongDocument &overlay, const SongDocument &merged, const QString &partName)
{
    if (!overlay.isOverlay)
        return;
    const QMap<QString, LyricSection> *inherited = nullptr;
    QMap<QString, LyricSection> *target = nullptr;
    if (partName.isEmpty()) {
        inherited = &merged.lyrics;
        target = &overlay.lyrics;
    } else {
        const Part *from = merged.part(partName);
        Part *to = overlay.part(partName);
        if (!from || !to)
            return;
        inherited = &from->lyrics;
        target = &to->lyrics;
    }
    if (!target->isEmpty())
        return;
    for (auto it = inherited->constBegin(); it != inherited->constEnd(); ++it)
        SongDocument::setLyric(*target, it.key(), it->rawText);
}

SongDocument mergeOverlay(const SongDocument &base, const SongDocument &overlay)
{
    SongDocument merged = overlay;
    merged.isMergedView = true;
    merged.parts.clear();

    const auto inheritString = [](Field<QString> &target, const Field<QString> &from) {
        if (!target.present() && from.present())
            target.bind(*from, {});
    };
    const auto inheritInt = [](Field<int> &target, const Field<int> &from) {
        if (!target.present() && from.present())
            target.bind(*from, {});
    };
    const auto inheritBool = [](Field<bool> &target, const Field<bool> &from) {
        if (!target.present() && from.present())
            target.bind(*from, {});
    };

    // An empty overlay title inherits rather than blanking the song.
    if (!merged.title.present() || merged.title->trimmed().isEmpty())
        merged.title = base.title;
    inheritString(merged.subtitle, base.subtitle);
    inheritString(merged.keySignature, base.keySignature);
    inheritString(merged.commentary, base.commentary);
    inheritInt(merged.timeSigNumerator, base.timeSigNumerator);
    inheritInt(merged.timeSigDenominator, base.timeSigDenominator);
    inheritInt(merged.tempoBpm, base.tempoBpm);
    inheritInt(merged.verseCount, base.verseCount);
    if (!merged.defaultVerses.present() && base.defaultVerses.present())
        merged.defaultVerses.bind(*base.defaultVerses, {});
    inheritBool(merged.active, base.active);
    inheritBool(merged.convergeVerses, base.convergeVerses);
    if (!merged.copyrights.present() && base.copyrights.present())
        merged.copyrights.bind(*base.copyrights, {});
    if (!merged.phraseBreaks.present() && base.phraseBreaks.present())
        merged.phraseBreaks.bind(*base.phraseBreaks, {});
    if (!merged.optionalPhraseBreaks.present() && base.optionalPhraseBreaks.present())
        merged.optionalPhraseBreaks.bind(*base.optionalPhraseBreaks, {});
    if (!merged.nonBreakingPhraseBreaks.present() && base.nonBreakingPhraseBreaks.present())
        merged.nonBreakingPhraseBreaks.bind(*base.nonBreakingPhraseBreaks, {});
    if (!merged.timeSigChanges.present() && base.timeSigChanges.present())
        merged.timeSigChanges.bind(*base.timeSigChanges, {});

    // Parts merge field-wise; a part the overlay never mentions is inherited whole.
    for (const Part &basePart : base.parts) {
        const Part *overlayPart = overlay.part(basePart.name);
        if (!overlayPart) {
            Part inherited = basePart;
            inherited.inheritedFromBase = true;
            inherited.notesInherited = true;
            merged.parts.append(inherited);
            continue;
        }
        Part result = *overlayPart;
        const auto takeString = [](Field<QString> &target, const Field<QString> &fallback) {
            if (!target.present() && fallback.present())
                target.bind(*fallback, {});
        };
        takeString(result.choralType, basePart.choralType);
        takeString(result.clef, basePart.clef);
        takeString(result.spliceLyricsInto, basePart.spliceLyricsInto);
        if (!result.staffNumber.present() && basePart.staffNumber.present())
            result.staffNumber.bind(*basePart.staffNumber, {});
        if (!result.suppressVerses.present() && basePart.suppressVerses.present())
            result.suppressVerses.bind(*basePart.suppressVerses, {});
        if (!result.suppressVersesWhen.present() && basePart.suppressVersesWhen.present())
            result.suppressVersesWhen.bind(*basePart.suppressVersesWhen, {});
        // A blank or absent `notes` inherits the base notation.
        if (!result.notes.present() || result.notes->trimmed().isEmpty()) {
            result.notes = basePart.notes;
            result.stream = basePart.stream;
            result.tokenIssues = basePart.tokenIssues;
            result.notesInherited = true;
        }
        // Defining any per-part lyric entry replaces that part's whole map.
        if (result.lyrics.isEmpty())
            result.lyrics = basePart.lyrics;
        result.inheritedFromBase = true;
        merged.parts.append(result);
    }
    for (const Part &overlayPart : overlay.parts) {
        if (!base.part(overlayPart.name))
            merged.parts.append(overlayPart);
    }

    // Lyric maps replace wholesale on purpose: a per-verse merge would let a
    // translation supply verses 1-2 and silently inherit the base's 3-4.
    if (merged.lyrics.isEmpty())
        merged.lyrics = base.lyrics;

    return merged;
}

} // namespace ope
