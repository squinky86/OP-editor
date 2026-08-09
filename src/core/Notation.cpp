// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Notation.h"

#include <QHash>

#include <algorithm>

namespace ope {
namespace {

constexpr int stepSemitone(QChar step)
{
    switch (step.toUpper().unicode()) {
    case u'C': return 0;
    case u'D': return 2;
    case u'E': return 4;
    case u'F': return 5;
    case u'G': return 7;
    case u'A': return 9;
    case u'B': return 11;
    default: return 0;
    }
}

constexpr int stepIndex(QChar step)
{
    switch (step.toUpper().unicode()) {
    case u'C': return 0;
    case u'D': return 1;
    case u'E': return 2;
    case u'F': return 3;
    case u'G': return 4;
    case u'A': return 5;
    case u'B': return 6;
    default: return 0;
    }
}

constexpr QChar stepForIndex(int index)
{
    constexpr char letters[7] = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };
    return QChar(letters[((index % 7) + 7) % 7]);
}

/// Flags stripped off the tail of the duration token. This mirrors
/// `strip_note_flags`: hairpin and spanner first, then `%dynamic`, then the
/// `/±N` dedup offset, then a loop over the remaining suffixes so their order in
/// the source does not matter.
struct FlagSet {
    bool tie = false;
    bool slurStart = false;
    bool slurEnd = false;
    bool dashedSlurStart = false;
    bool dashedSlurEnd = false;
    bool beamStart = false;
    bool beamEnd = false;
    bool fermata = false;
    bool staccato = false;
    bool chorusStart = false;
    bool codaStart = false;
    QString dynamic;
    QString hairpin;
    QString tempoSpanner;
    bool spannerEnd = false;
    int dedupOffset = 0;
    bool hasDynamicMarker = false;
};

void extractHairpin(QString &clean, FlagSet &flags)
{
    if (!flags.hairpin.isEmpty())
        return;
    QString kind;
    if (clean.endsWith(QLatin1String("\\<")))
        kind = QStringLiteral("crescendo");
    else if (clean.endsWith(QLatin1String("\\>")))
        kind = QStringLiteral("diminuendo");
    else if (clean.endsWith(QLatin1String("\\!")))
        kind = QStringLiteral("end");
    if (!kind.isEmpty()) {
        clean.chop(2);
        flags.hairpin = kind;
    }
}

void extractTempoSpanner(QString &clean, FlagSet &flags)
{
    if (!flags.spannerEnd && clean.endsWith(QLatin1String("\\spanend"))) {
        clean.chop(8);
        flags.spannerEnd = true;
        // A start marker may still sit in front of the terminator; fall through.
    }
    if (!flags.tempoSpanner.isEmpty())
        return;
    for (const QString &name : tempoSpannerNames()) {
        const QString needle = u'\\' + name;
        if (clean.endsWith(needle)) {
            clean.chop(needle.size());
            flags.tempoSpanner = name;
            return;
        }
    }
}

/// Strip every suffix marker from a duration token, leaving the digits and dots.
QString stripFlags(const QString &durationToken, FlagSet &flags)
{
    QString clean = durationToken;

    extractHairpin(clean, flags);
    extractTempoSpanner(clean, flags);

    if (const qsizetype pct = clean.indexOf(u'%'); pct >= 0) {
        flags.hasDynamicMarker = true;
        flags.dynamic = clean.sliced(pct + 1);
        clean.truncate(pct);
    }

    if (const qsizetype slash = clean.lastIndexOf(u'/'); slash >= 0) {
        const QString tail = clean.sliced(slash + 1);
        qsizetype end = 0;
        while (end < tail.size()
            && (tail.at(end).isDigit() || tail.at(end) == u'+' || tail.at(end) == u'-'))
            ++end;
        const QString numeric = tail.first(end);
        bool ok = false;
        const int offset = numeric.toInt(&ok);
        if (ok) {
            flags.dedupOffset = offset;
            clean.truncate(slash);
            clean.append(tail.sliced(end));
        }
    }

    for (;;) {
        const qsizetype before = clean.size();

        extractHairpin(clean, flags);
        extractTempoSpanner(clean, flags);

        // `-.` before the section markers and before `-(` / `-)`, which it shadows.
        if (clean.endsWith(QLatin1String("-."))) {
            flags.staccato = true;
            clean.chop(2);
        }
        if (clean.endsWith(QLatin1String("@c"))) {
            flags.chorusStart = true;
            clean.chop(2);
        }
        if (clean.endsWith(QLatin1String("@e"))) {
            flags.codaStart = true;
            clean.chop(2);
        }
        if (clean.endsWith(u'!')) {
            flags.fermata = true;
            clean.chop(1);
        }
        if (clean.endsWith(u'~')) {
            flags.tie = true;
            clean.chop(1);
        }

        if (clean.endsWith(QLatin1String("[("))) {
            flags.slurStart = true;
            flags.beamStart = true;
            clean.chop(2);
        } else if (clean.endsWith(QLatin1String("-("))) {
            flags.dashedSlurStart = true;
            clean.chop(2);
        } else if (clean.endsWith(u'(')) {
            flags.slurStart = true;
            clean.chop(1);
        } else if (clean.endsWith(u'[')) {
            flags.beamStart = true;
            clean.chop(1);
        }

        if (clean.endsWith(QLatin1String("])"))) {
            flags.slurEnd = true;
            flags.beamEnd = true;
            clean.chop(2);
        } else if (clean.endsWith(QLatin1String("-)"))) {
            flags.dashedSlurEnd = true;
            clean.chop(2);
        } else if (clean.endsWith(u')')) {
            flags.slurEnd = true;
            clean.chop(1);
        } else if (clean.endsWith(u']')) {
            flags.beamEnd = true;
            clean.chop(1);
        }

        if (clean.size() == before)
            break;
    }

    return clean;
}

void applyFlags(Event &event, const FlagSet &flags)
{
    event.tie = flags.tie;
    event.slurStart = flags.slurStart;
    event.slurEnd = flags.slurEnd;
    event.dashedSlurStart = flags.dashedSlurStart;
    event.dashedSlurEnd = flags.dashedSlurEnd;
    event.beamStart = flags.beamStart;
    event.beamEnd = flags.beamEnd;
    event.fermata = flags.fermata;
    event.staccato = flags.staccato;
    event.chorusStart = flags.chorusStart;
    event.codaStart = flags.codaStart;
    event.dynamic = flags.dynamic;
    event.hairpin = flags.hairpin;
    event.tempoSpanner = flags.tempoSpanner;
    event.spannerEnd = flags.spannerEnd;
    event.dedupOffset = flags.dedupOffset;
}

/// Parse the digits-and-dots remainder into a duration, recording whether the
/// seeder would have silently defaulted it to a quarter.
Duration parseDurationText(const QString &text, bool &missing, bool &unknown)
{
    Duration duration;
    QString digits;
    int dots = 0;
    for (const QChar c : text) {
        if (c == u'.')
            ++dots;
        else
            digits.append(c);
    }
    duration.dots = dots;
    missing = digits.isEmpty();
    unknown = false;
    static const QList<int> valid { 1, 2, 4, 8, 16, 32, 64 };
    bool ok = false;
    const int base = digits.toInt(&ok);
    if (!ok || !valid.contains(base)) {
        if (!missing)
            unknown = true;
        duration.base = 4;  // the seeder's silent fallback
    } else {
        duration.base = base;
    }
    return duration;
}

/// Read one whitespace-delimited token, mirroring `read_until_boundary`:
/// a backslash escapes only the hairpin markers, and `<`, `{`, `}` end a token.
QString readToken(const QString &text, qsizetype &pos)
{
    QString token;
    while (pos < text.size()) {
        const QChar c = text.at(pos);
        if (c == u'\\') {
            ++pos;
            if (pos < text.size()) {
                const QChar next = text.at(pos);
                if (next == u'<' || next == u'>' || next == u'!') {
                    token.append(u'\\');
                    token.append(next);
                    ++pos;
                    continue;
                }
            }
            token.append(u'\\');
            continue;
        }
        if (c.isSpace() || c == u'<' || c == u'{' || c == u'}')
            break;
        token.append(c);
        ++pos;
    }
    return token;
}

} // namespace

int Pitch::midiNote() const noexcept
{
    return (octave + 1) * 12 + stepSemitone(step) + alter;
}

int Pitch::diatonic() const noexcept { return octave * 7 + stepIndex(step); }

Pitch Pitch::fromDiatonic(int diatonic, int alter)
{
    Pitch pitch;
    pitch.step = stepForIndex(diatonic);
    pitch.octave = (diatonic - ((diatonic % 7) + 7) % 7) / 7;
    pitch.alter = alter;
    return pitch;
}

Pitch Pitch::fromToken(QStringView token)
{
    Pitch pitch;
    if (token.isEmpty())
        return pitch;

    qsizetype pos = 0;
    pitch.step = token.at(pos).toUpper();
    ++pos;

    // Accidental letters, exactly as the seeder collects them: any run of
    // i / e / s, then matched against the four known spellings.
    QString accidental;
    while (pos < token.size()) {
        const QChar c = token.at(pos);
        if (c == u'i' || c == u'e' || c == u's') {
            accidental.append(c);
            ++pos;
        } else {
            break;
        }
    }
    if (accidental == QLatin1String("is"))
        pitch.alter = 1;
    else if (accidental == QLatin1String("es"))
        pitch.alter = -1;
    else if (accidental == QLatin1String("isis"))
        pitch.alter = 2;
    else if (accidental == QLatin1String("eses"))
        pitch.alter = -2;

    pitch.octave = 3;
    for (; pos < token.size(); ++pos) {
        if (token.at(pos) == u'\'')
            ++pitch.octave;
        else if (token.at(pos) == u',')
            --pitch.octave;
    }
    return pitch;
}

QString Pitch::toToken() const
{
    QString out;
    out.append(step.toLower());
    if (alter == 1)
        out.append(QStringLiteral("is"));
    else if (alter == -1)
        out.append(QStringLiteral("es"));
    else if (alter == 2)
        out.append(QStringLiteral("isis"));
    else if (alter == -2)
        out.append(QStringLiteral("eses"));
    for (int i = 3; i < octave; ++i)
        out.append(u'\'');
    for (int i = octave; i < 3; ++i)
        out.append(u',');
    return out;
}

int Duration::baseTicks() const noexcept
{
    switch (base) {
    case 1: return ticks::Whole;
    case 2: return ticks::Half;
    case 4: return ticks::Quarter;
    case 8: return ticks::Eighth;
    case 16: return ticks::Sixteenth;
    case 32: return ticks::ThirtySecond;
    case 64: return ticks::SixtyFourth;
    default: return ticks::Quarter;
    }
}

int Duration::notatedTicks() const noexcept
{
    int total = baseTicks();
    int add = baseTicks() / 2;
    for (int i = 0; i < dots; ++i) {
        total += add;
        add /= 2;
    }
    return total;
}

QString Duration::toToken() const
{
    return QString::number(base) + QString(dots, u'.');
}

QString Duration::typeName(int base)
{
    switch (base) {
    case 1: return QStringLiteral("whole");
    case 2: return QStringLiteral("half");
    case 4: return QStringLiteral("quarter");
    case 8: return QStringLiteral("eighth");
    case 16: return QStringLiteral("16th");
    case 32: return QStringLiteral("32nd");
    case 64: return QStringLiteral("64th");
    default: return QStringLiteral("quarter");
    }
}

int Event::playedTicks() const noexcept
{
    const int notated = duration.notatedTicks();
    if (tuplet && tuplet->actual > 0)
        return notated * tuplet->normal / tuplet->actual;  // truncating, as the seeder does
    return notated;
}

QString Event::toSource() const
{
    QString out;
    switch (kind) {
    case EventKind::Rest: out = QStringLiteral("r"); break;
    case EventKind::Spacer: out = QStringLiteral("s"); break;
    case EventKind::Chord: {
        out = u'<';
        for (qsizetype i = 0; i < pitches.size(); ++i) {
            if (i > 0)
                out.append(u' ');
            out.append(pitches.at(i).toToken());
        }
        out.append(u'>');
        break;
    }
    case EventKind::Note:
        out = pitches.isEmpty() ? QStringLiteral("c") : pitches.front().toToken();
        break;
    }

    out.append(duration.toToken());

    // Canonical suffix order. The seeder's stripping loop is order-independent,
    // so this choice only needs to be stable, not to match any particular file.
    if (dedupOffset != 0)
        out.append(u'/' + (dedupOffset > 0 ? QStringLiteral("+") : QString())
            + QString::number(dedupOffset));
    if (slurStart && beamStart)
        out.append(QStringLiteral("[("));
    else if (dashedSlurStart)
        out.append(QStringLiteral("-("));
    else if (slurStart)
        out.append(u'(');
    else if (beamStart)
        out.append(u'[');
    if (tie)
        out.append(u'~');
    if (fermata)
        out.append(u'!');
    if (staccato)
        out.append(QStringLiteral("-."));
    if (chorusStart)
        out.append(QStringLiteral("@c"));
    if (codaStart)
        out.append(QStringLiteral("@e"));
    if (!dynamic.isEmpty())
        out.append(u'%' + dynamic);
    if (hairpin == QLatin1String("crescendo"))
        out.append(QStringLiteral("\\<"));
    else if (hairpin == QLatin1String("diminuendo"))
        out.append(QStringLiteral("\\>"));
    else if (hairpin == QLatin1String("end"))
        out.append(QStringLiteral("\\!"));
    if (!tempoSpanner.isEmpty())
        out.append(u'\\' + tempoSpanner);
    if (spannerEnd)
        out.append(QStringLiteral("\\spanend"));
    if (slurEnd && beamEnd)
        out.append(QStringLiteral("])"));
    else if (dashedSlurEnd)
        out.append(QStringLiteral("-)"));
    else if (slurEnd)
        out.append(u')');
    else if (beamEnd)
        out.append(u']');
    return out;
}

int Measure::notatedTicks() const
{
    int total = 0;
    for (const Event &event : events)
        total += event.duration.notatedTicks();
    return total;
}

int Measure::playedTicks() const
{
    int total = 0;
    for (const Event &event : events)
        total += event.playedTicks();
    return total;
}

NoteStream NoteStream::parse(const QString &notes, QList<TokenIssue> *issues)
{
    NoteStream stream;

    const auto addIssue = [issues](TokenIssue::Code code, int measureIndex, int eventIndex,
                              const QString &token, const QString &detail = {}) {
        if (issues)
            issues->append({ code, measureIndex, eventIndex, token, detail });
    };

    // A newline is a measure separator exactly as `|` is (the seeder replaces
    // '\n' with '|' before splitting), so the line grouping is free-form and
    // recorded here only so an edited block can be written back the same shape.
    QList<int> lineLayout;
    for (const QString &line : notes.split(u'\n')) {
        int count = 0;
        for (const QString &segment : line.split(u'|')) {
            if (!segment.trimmed().isEmpty())
                ++count;
        }
        if (count > 0)
            lineLayout.append(count);
    }
    stream.setLineLayout(lineLayout);

    QString normalized = notes;
    normalized.replace(u'\n', u'|');

    int measureIndex = 0;
    for (const QString &rawMeasure : normalized.split(u'|')) {
        const QString text = rawMeasure.trimmed();
        if (text.isEmpty())
            continue;

        Measure measure;
        std::optional<Tuplet> activeTuplet;
        bool tupletStartPending = false;
        qsizetype pos = 0;

        while (pos < text.size()) {
            const QChar c = text.at(pos);
            if (c.isSpace()) {
                ++pos;
                continue;
            }

            if (c == u'{') {
                ++pos;
                QString spec;
                while (pos < text.size() && !text.at(pos).isSpace() && text.at(pos) != u'}') {
                    spec.append(text.at(pos));
                    ++pos;
                }
                Tuplet tuplet;
                if (const qsizetype colon = spec.indexOf(u':'); colon >= 0) {
                    bool okA = false;
                    bool okN = false;
                    const int actual = spec.first(colon).toInt(&okA);
                    const int normal = spec.sliced(colon + 1).toInt(&okN);
                    tuplet.actual = okA ? actual : 3;
                    tuplet.normal = okN ? normal : 2;
                } else {
                    bool ok = false;
                    const int actual = spec.toInt(&ok);
                    tuplet.actual = ok ? actual : 3;
                    int power = 1;
                    while (power * 2 < tuplet.actual)
                        power *= 2;
                    tuplet.normal = std::max(power, 1);
                }
                activeTuplet = tuplet;
                tupletStartPending = true;
                continue;
            }

            if (c == u'}') {
                ++pos;
                if (!measure.events.isEmpty() && measure.events.back().tuplet) {
                    measure.events.back().tuplet->isEnd = true;
                } else if (!activeTuplet) {
                    addIssue(TokenIssue::Code::StrayCloseBrace, measureIndex,
                        static_cast<int>(measure.events.size()), QStringLiteral("}"));
                }
                activeTuplet.reset();
                tupletStartPending = false;
                continue;
            }

            Event event;
            const qsizetype tokenBegin = pos;

            if (c == u'<') {
                ++pos;
                QString pitchText;
                bool closed = false;
                while (pos < text.size()) {
                    if (text.at(pos) == u'>') {
                        ++pos;
                        closed = true;
                        break;
                    }
                    pitchText.append(text.at(pos));
                    ++pos;
                }
                const QString durationToken = readToken(text, pos);
                FlagSet flags;
                const QString clean = stripFlags(durationToken, flags);
                bool missing = false;
                bool unknown = false;
                event.duration = parseDurationText(clean, missing, unknown);
                event.kind = EventKind::Chord;
                for (const QString &piece :
                    pitchText.split(u' ', Qt::SkipEmptyParts))
                    event.pitches.append(Pitch::fromToken(piece));
                applyFlags(event, flags);
                event.raw = text.sliced(tokenBegin, pos - tokenBegin);

                const int eventIndex = static_cast<int>(measure.events.size());
                if (!closed)
                    addIssue(TokenIssue::Code::UnterminatedChord, measureIndex, eventIndex,
                        event.raw);
                if (event.pitches.isEmpty())
                    addIssue(TokenIssue::Code::EmptyChord, measureIndex, eventIndex, event.raw);
                if (event.pitches.size() == 1)
                    event.kind = EventKind::Chord;  // authored as a chord; keep it one
                if (missing)
                    addIssue(TokenIssue::Code::MissingDuration, measureIndex, eventIndex,
                        event.raw);
                if (unknown)
                    addIssue(TokenIssue::Code::UnknownDuration, measureIndex, eventIndex,
                        event.raw, clean);
            } else {
                const QString token = readToken(text, pos);
                if (token.isEmpty()) {
                    ++pos;
                    continue;
                }

                // The pitch ends at the first ASCII digit; rests are special-cased
                // so `r` keeps its own scan, exactly as parse_single_token does.
                qsizetype digitAt = -1;
                for (qsizetype i = 0; i < token.size(); ++i) {
                    if (token.at(i).isDigit() && token.at(i).unicode() < 128) {
                        digitAt = i;
                        break;
                    }
                }
                QString pitchText;
                QString durationToken;
                if (digitAt == 0 && token.startsWith(u'r')) {
                    qsizetype restDigit = token.size();
                    for (qsizetype i = 1; i < token.size(); ++i) {
                        if (token.at(i).isDigit()) {
                            restDigit = i;
                            break;
                        }
                    }
                    pitchText = token.first(restDigit);
                    durationToken = token.sliced(restDigit);
                } else {
                    const qsizetype split = digitAt < 0 ? token.size() : digitAt;
                    pitchText = token.first(split);
                    durationToken = token.sliced(split);
                }

                FlagSet flags;
                const QString clean = stripFlags(durationToken, flags);
                bool missing = false;
                bool unknown = false;
                event.duration = parseDurationText(clean, missing, unknown);
                applyFlags(event, flags);

                if (pitchText == QLatin1String("r")) {
                    event.kind = EventKind::Rest;
                    event.tie = false;  // the seeder clears a tie on a rest token
                } else if (pitchText == QLatin1String("s")) {
                    event.kind = EventKind::Spacer;
                } else if (pitchText.isEmpty()) {
                    ++pos;
                    continue;
                } else {
                    event.kind = EventKind::Note;
                    event.pitches.append(Pitch::fromToken(pitchText));
                }
                event.raw = token;

                const int eventIndex = static_cast<int>(measure.events.size());
                if (missing)
                    addIssue(TokenIssue::Code::MissingDuration, measureIndex, eventIndex, token);
                if (unknown)
                    addIssue(
                        TokenIssue::Code::UnknownDuration, measureIndex, eventIndex, token, clean);
                if (flags.hasDynamicMarker) {
                    if (!recognisedDynamics().contains(flags.dynamic))
                        addIssue(TokenIssue::Code::UnknownDynamic, measureIndex, eventIndex, token,
                            flags.dynamic);
                    else if (!documentedDynamics().contains(flags.dynamic))
                        addIssue(TokenIssue::Code::UndocumentedDynamic, measureIndex, eventIndex,
                            token, flags.dynamic);
                }
                if (clean.contains(u'\\'))
                    addIssue(TokenIssue::Code::UnknownSpanner, measureIndex, eventIndex, token,
                        clean);
                else if (!clean.isEmpty()
                    && std::any_of(clean.begin(), clean.end(),
                        [](QChar ch) { return !ch.isDigit() && ch != u'.'; }))
                    addIssue(TokenIssue::Code::LeftoverText, measureIndex, eventIndex, token, clean);
            }

            if (activeTuplet) {
                Tuplet tuplet = *activeTuplet;
                tuplet.isStart = tupletStartPending;
                tuplet.isEnd = false;
                event.tuplet = tuplet;
                tupletStartPending = false;
            }
            measure.events.append(event);
        }

        if (activeTuplet)
            addIssue(TokenIssue::Code::UnterminatedTuplet, measureIndex,
                static_cast<int>(measure.events.size()), text);

        stream.m_measures.append(measure);
        ++measureIndex;
    }

    stream.reindex();
    return stream;
}

void NoteStream::reindex()
{
    SlotCounter counter;
    int slot = 0;
    for (int m = 0; m < m_measures.size(); ++m) {
        int tick = 0;
        auto &events = m_measures[m].events;
        for (int e = 0; e < events.size(); ++e) {
            Event &event = events[e];
            event.measureIndex = m;
            event.indexInMeasure = e;
            event.tickInMeasure = tick;
            tick += event.playedTicks();
            event.slotIndex = counter.isSlot(event) ? slot++ : -1;
        }
    }
}

QString NoteStream::toSource() const
{
    QList<int> layout = m_lineLayout;
    int planned = 0;
    for (const int count : layout)
        planned += count;
    if (planned != m_measures.size()) {
        // The measure count changed, so the recorded grouping no longer applies.
        // Grow or shrink the last line when the shape is otherwise intact,
        // falling back to four measures per line for a fresh block.
        layout.clear();
        if (!m_lineLayout.isEmpty() && m_lineLayout.size() > 1) {
            int accumulated = 0;
            for (const int count : m_lineLayout) {
                if (accumulated + count >= m_measures.size())
                    break;
                layout.append(count);
                accumulated += count;
            }
            if (accumulated < m_measures.size())
                layout.append(static_cast<int>(m_measures.size()) - accumulated);
        } else if (m_measures.size() <= 4 && !m_lineLayout.isEmpty()
            && m_lineLayout.size() == 1) {
            layout.append(static_cast<int>(m_measures.size()));
        } else {
            for (qsizetype i = 0; i < m_measures.size(); i += 4)
                layout.append(static_cast<int>(std::min<qsizetype>(4, m_measures.size() - i)));
        }
    }

    QStringList lines;
    qsizetype index = 0;
    for (const int count : layout) {
        QStringList measureTexts;
        for (int i = 0; i < count && index < m_measures.size(); ++i, ++index) {
            QStringList tokens;
            const Measure &measure = m_measures.at(index);
            for (qsizetype e = 0; e < measure.events.size(); ++e) {
                const Event &event = measure.events.at(e);
                const bool openTuplet = event.tuplet && event.tuplet->isStart;
                if (openTuplet) {
                    QString spec = QString::number(event.tuplet->actual);
                    int power = 1;
                    while (power * 2 < event.tuplet->actual)
                        power *= 2;
                    if (event.tuplet->normal != std::max(power, 1))
                        spec += u':' + QString::number(event.tuplet->normal);
                    tokens.append(u'{' + spec);
                }
                tokens.append(event.text());
                if (event.tuplet && event.tuplet->isEnd)
                    tokens.append(QStringLiteral("}"));
            }
            measureTexts.append(tokens.join(u' '));
        }
        lines.append(measureTexts.join(QStringLiteral(" | ")));
    }
    return lines.join(QStringLiteral(" |\n"));
}

bool NoteStream::anyDirty() const
{
    for (const Measure &measure : m_measures) {
        for (const Event &event : measure.events) {
            if (event.dirty)
                return true;
        }
    }
    return false;
}

bool SlotCounter::isSlot(const Event &event)
{
    const bool isRestOrSpacer = event.isRest() || event.isSpacer();
    const bool tieContinuation = m_tieActive;

    // Snapshot before updating, so the slur-end note (still inside the melisma)
    // is excluded.
    const bool wasInSlur = m_inSlur;
    const bool wasInBeam = m_inBeam;

    if (isRestOrSpacer) {
        m_tieActive = false;
    } else {
        m_tieActive = event.tie;
        if (event.slurStart)
            m_inSlur = true;
        if (event.slurEnd)
            m_inSlur = false;
        if (event.beamStart)
            m_inBeam = true;
        if (event.beamEnd)
            m_inBeam = false;
    }

    if (isRestOrSpacer || tieContinuation)
        return false;

    return (!wasInBeam && (event.slurStart || !wasInSlur))
        || (!wasInSlur && event.beamStart);
}

QStringList documentedDynamics()
{
    static const QStringList names { QStringLiteral("ppp"), QStringLiteral("pp"),
        QStringLiteral("p"), QStringLiteral("mp"), QStringLiteral("mf"), QStringLiteral("f"),
        QStringLiteral("ff"), QStringLiteral("fff"), QStringLiteral("fp"),
        QStringLiteral("sfz") };
    return names;
}

QStringList recognisedDynamics()
{
    static const QStringList names { QStringLiteral("ppp"), QStringLiteral("pppp"),
        QStringLiteral("pp"), QStringLiteral("p"), QStringLiteral("mp"), QStringLiteral("mf"),
        QStringLiteral("f"), QStringLiteral("fp"), QStringLiteral("pf"), QStringLiteral("ff"),
        QStringLiteral("sf"), QStringLiteral("sfp"), QStringLiteral("rf"), QStringLiteral("fz"),
        QStringLiteral("fff"), QStringLiteral("ffff"), QStringLiteral("sfz"),
        QStringLiteral("sffz"), QStringLiteral("rfz"), QStringLiteral("sfpp") };
    return names;
}

int velocityForDynamic(QStringView name)
{
    static const QHash<QString, int> map {
        { QStringLiteral("ppp"), 20 }, { QStringLiteral("pppp"), 20 },
        { QStringLiteral("pp"), 35 }, { QStringLiteral("p"), 50 },
        { QStringLiteral("mp"), 64 }, { QStringLiteral("mf"), 80 },
        { QStringLiteral("f"), 96 }, { QStringLiteral("fp"), 96 },
        { QStringLiteral("pf"), 96 }, { QStringLiteral("ff"), 112 },
        { QStringLiteral("sf"), 112 }, { QStringLiteral("sfp"), 112 },
        { QStringLiteral("rf"), 112 }, { QStringLiteral("fz"), 112 },
        { QStringLiteral("fff"), 127 }, { QStringLiteral("ffff"), 127 },
        { QStringLiteral("sfz"), 127 }, { QStringLiteral("sffz"), 127 },
        { QStringLiteral("rfz"), 127 }, { QStringLiteral("sfpp"), 127 },
    };
    return map.value(name.toString(), 80);
}

QStringList tempoSpannerNames()
{
    // Longest first so `\ritard` is matched before `\rit`.
    static const QStringList names { QStringLiteral("ritard"), QStringLiteral("string"),
        QStringLiteral("accel"), QStringLiteral("atempo"), QStringLiteral("rall"),
        QStringLiteral("rit") };
    return names;
}

} // namespace ope
