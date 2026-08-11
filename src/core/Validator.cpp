// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Validator.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace ope {
namespace {

/// Longest note that can carry a beam instead of a slur: a dotted eighth.
constexpr int BeamableTicks = 36;

struct GroupMember {
    int measureIndex = 0;
    int eventIndex = 0;
    const Event *event = nullptr;
};

enum class GroupKind { Slur, Beam, Dashed };

struct Group {
    GroupKind kind = GroupKind::Slur;
    QList<GroupMember> members;
};

/// Collect slur, beam, and dashed-slur groups, mirroring audit_style.py's
/// slur_beam_groups: a group runs from its opening marker to its closing one.
QList<Group> collectGroups(const NoteStream &stream)
{
    QList<Group> groups;
    struct Open {
        GroupKind kind;
        Group group;
        bool active = false;
    };
    Open slur { GroupKind::Slur, {}, false };
    Open beam { GroupKind::Beam, {}, false };
    Open dashed { GroupKind::Dashed, {}, false };

    const QList<Measure> &measures = stream.measures();
    for (int m = 0; m < measures.size(); ++m) {
        const QList<Event> &events = measures.at(m).events;
        for (int e = 0; e < events.size(); ++e) {
            const Event &event = events.at(e);
            const GroupMember member { m, e, &event };

            if (event.slurStart) {
                slur.active = true;
                slur.group = Group { GroupKind::Slur, {} };
            }
            if (event.beamStart) {
                beam.active = true;
                beam.group = Group { GroupKind::Beam, {} };
            }
            if (event.dashedSlurStart) {
                dashed.active = true;
                dashed.group = Group { GroupKind::Dashed, {} };
            }

            if (slur.active)
                slur.group.members.append(member);
            if (beam.active)
                beam.group.members.append(member);
            if (dashed.active)
                dashed.group.members.append(member);

            if (event.slurEnd && slur.active) {
                groups.append(slur.group);
                slur.active = false;
            }
            if (event.beamEnd && beam.active) {
                groups.append(beam.group);
                beam.active = false;
            }
            if (event.dashedSlurEnd && dashed.active) {
                groups.append(dashed.group);
                dashed.active = false;
            }
        }
    }
    return groups;
}

QString pitchKey(const Event &event)
{
    if (event.isRest())
        return QStringLiteral("r");
    if (event.isSpacer())
        return QStringLiteral("s");
    QStringList parts;
    for (const Pitch &pitch : event.pitches)
        parts.append(QString::number(pitch.midiNote()));
    return parts.join(u',');
}

QString rawOf(const QList<GroupMember> &members)
{
    QStringList tokens;
    for (const GroupMember &member : members)
        tokens.append(member.event->raw);
    return tokens.join(u' ');
}

} // namespace

QString Finding::location() const
{
    QStringList bits;
    if (!partName.isEmpty())
        bits.append(partName);
    if (measure > 0)
        bits.append(QStringLiteral("m%1").arg(measure));
    if (!lyricKey.isEmpty())
        bits.append(QStringLiteral("lyrics.%1").arg(lyricKey));
    if (slot >= 0)
        bits.append(QStringLiteral("slot %1").arg(slot));
    return bits.join(QStringLiteral(" · "));
}

QString Finding::formatted() const
{
    const QString where = location();
    if (where.isEmpty())
        return QStringLiteral("%1  %2").arg(rule, message);
    return QStringLiteral("%1  %2: %3").arg(rule, where, message);
}

QStringList validKeySignatures()
{
    static const QStringList keys { QStringLiteral("C"), QStringLiteral("G"), QStringLiteral("D"),
        QStringLiteral("A"), QStringLiteral("E"), QStringLiteral("B"), QStringLiteral("F#"),
        QStringLiteral("F"), QStringLiteral("Bb"), QStringLiteral("Eb"), QStringLiteral("Ab"),
        QStringLiteral("Db"), QStringLiteral("Gb"), QStringLiteral("Cb"), QStringLiteral("C#") };
    return keys;
}

QStringList validClefs()
{
    static const QStringList clefs { QStringLiteral("treble"), QStringLiteral("bass"),
        QStringLiteral("treble_8") };
    return clefs;
}

QStringList validChoralTypes()
{
    static const QStringList types { QStringLiteral("soprano"), QStringLiteral("alto"),
        QStringLiteral("tenor"), QStringLiteral("bass") };
    return types;
}

int countBySeverity(const QList<Finding> &findings, Severity severity)
{
    return static_cast<int>(std::count_if(findings.begin(), findings.end(),
        [severity](const Finding &finding) { return finding.severity == severity; }));
}

QList<Finding> validate(
    const SongDocument &doc, bool languageKnown, const QString &baseLanguage)
{
    QList<Finding> findings;
    const auto add = [&findings](Severity severity, const QString &rule, const QString &message,
                         const QString &partName = {}, int measure = -1, int eventIndex = -1,
                         const QString &lyricKey = {}, int slot = -1,
                         const QString &fixHint = {}) {
        findings.append(Finding { severity, rule, message, partName, measure, eventIndex,
            lyricKey, slot, fixHint });
    };

    // ---------------------------------------------------------------- structure

    if (!doc.title.present() || doc.title->trimmed().isEmpty())
        add(Severity::Error, QStringLiteral("E-TITLE"),
            QStringLiteral("missing required field `title`"));
    if (doc.parts.isEmpty())
        add(Severity::Error, QStringLiteral("E-NOPARTS"), QStringLiteral("no [parts.*] defined"));
    for (const Part &part : doc.parts) {
        if (!part.notes.present() || part.notes->trimmed().isEmpty())
            add(Severity::Error, QStringLiteral("E-NONOTES"),
                QStringLiteral("part \"%1\" has no `notes`").arg(part.name), part.name);
    }

    if (doc.isOverlay) {
        if (!languageKnown)
            add(Severity::Error, QStringLiteral("E-LANG-UNKNOWN"),
                QStringLiteral("unknown language \"%1\" — add it to LANGUAGES in src/i18n.rs "
                               "and to OPE's bundled registry")
                    .arg(doc.language),
                {}, -1, -1, {}, -1, QStringLiteral("show the registry entry to add"));
        if (!baseLanguage.isEmpty() && doc.language == baseLanguage)
            add(Severity::Error, QStringLiteral("E-LANG-DUP"),
                QStringLiteral("same language as song.toml (\"%1\") — that is a duplicate, "
                               "not a translation")
                    .arg(baseLanguage));
    } else if (doc.declaredLanguage.present() && !languageKnown) {
        add(Severity::Error, QStringLiteral("E-LANG-UNKNOWN"),
            QStringLiteral("unknown language \"%1\" — add it to LANGUAGES in src/i18n.rs")
                .arg(doc.language));
    }

    if (doc.tempoBpm.present() && *doc.tempoBpm <= 0) {
        add(Severity::Error, QStringLiteral("E-TEMPO"),
            QStringLiteral("tempo_bpm must be positive; found %1").arg(*doc.tempoBpm));
    }
    if (doc.verseCount.present() && *doc.verseCount <= 0) {
        add(Severity::Error, QStringLiteral("E-VERSE-COUNT"),
            QStringLiteral("verse_count must be positive; found %1").arg(*doc.verseCount));
    }

    for (const QString &key : doc.unknownKeys)
        add(Severity::Info, QStringLiteral("I-UNKNOWN-KEY"),
            QStringLiteral("`%1` is not a field OPE knows; it is preserved untouched").arg(key));

    // Metre values feed every measure-duration calculation. Reject values the
    // tick engine cannot represent instead of silently treating them as /4.
    const auto validateMetre = [&add](int numerator, int denominator, const QString &where,
                                  int measure = -1) {
        if (numerator <= 0) {
            add(Severity::Error, QStringLiteral("E-METRE"),
                QStringLiteral("%1 has numerator %2; it must be positive").arg(where).arg(numerator),
                {}, measure);
        }
        if (!ticks::isSupportedTimeSignatureDenominator(denominator)) {
            add(Severity::Error, QStringLiteral("E-METRE"),
                QStringLiteral("%1 has denominator %2; supported values are 1, 2, 4, 8, 16, 32, "
                               "and 64")
                    .arg(where)
                    .arg(denominator),
                {}, measure);
        }
    };
    validateMetre(doc.timeSigNumerator.valueOr(4), doc.timeSigDenominator.valueOr(4),
        QStringLiteral("the song metre"));

    if (doc.timeSigChanges.present()) {
        const int measures = doc.measureCount();
        QList<std::pair<int, int>> occupiedRanges;
        for (qsizetype i = 0; i < doc.timeSigChanges->size(); ++i) {
            const TimeSigChange &change = doc.timeSigChanges->at(i);
            const QString where = QStringLiteral("time_sig_changes row %1").arg(i + 1);
            validateMetre(change.numerator, change.denominator, where, change.measure);
            if (change.measure <= 0 || change.duration <= 0) {
                add(Severity::Error, QStringLiteral("E-METRE-RANGE"),
                    QStringLiteral("%1 must have positive measure and duration values").arg(where),
                    {}, change.measure);
                continue;
            }
            const int last = change.measure + change.duration - 1;
            if (measures > 0 && last > measures) {
                add(Severity::Error, QStringLiteral("E-METRE-RANGE"),
                    QStringLiteral("%1 covers measures %2–%3, past the song's final measure %4")
                        .arg(where)
                        .arg(change.measure)
                        .arg(last)
                        .arg(measures),
                    {}, change.measure);
            }
            for (const auto &[firstOccupied, lastOccupied] : occupiedRanges) {
                if (change.measure <= lastOccupied && last >= firstOccupied) {
                    add(Severity::Error, QStringLiteral("E-METRE-OVERLAP"),
                        QStringLiteral("%1 overlaps another metre change in measures %2–%3")
                            .arg(where)
                            .arg(std::max(change.measure, firstOccupied))
                            .arg(std::min(last, lastOccupied)),
                        {}, change.measure);
                    break;
                }
            }
            occupiedRanges.append({ change.measure, last });
        }
    }

    // ------------------------------------------------------------- token issues

    for (const Part &part : doc.parts) {
        for (const TokenIssue &issue : part.tokenIssues) {
            const int measure = issue.measureIndex + 1;
            switch (issue.code) {
            case TokenIssue::Code::MissingDuration:
                add(Severity::Error, QStringLiteral("E-DUR-MISSING"),
                    QStringLiteral("`%1` has no duration; the seeder would import it as a "
                                   "quarter note without complaint")
                        .arg(issue.token),
                    part.name, measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UnknownDuration:
                add(Severity::Error, QStringLiteral("E-DUR-UNKNOWN"),
                    QStringLiteral("`%1` has duration \"%2\", which is not one of 1 2 4 8 16 32 "
                                   "64; the seeder would silently use a quarter note")
                        .arg(issue.token, issue.detail),
                    part.name, measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UnknownDynamic:
                add(Severity::Error, QStringLiteral("E-DYN-UNKNOWN"),
                    QStringLiteral("`%1` has dynamic \"%2\", which no exporter recognises; "
                                   "playback would fall back to mezzo-forte")
                        .arg(issue.token, issue.detail),
                    part.name, measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UndocumentedDynamic:
                add(Severity::Warning, QStringLiteral("W-DYN-UNDOCUMENTED"),
                    QStringLiteral("dynamic \"%1\" works but is not one of the ten in the format "
                                   "documentation")
                        .arg(issue.detail),
                    part.name, measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UnknownSpanner:
                add(Severity::Error, QStringLiteral("E-SPAN-UNKNOWN"),
                    QStringLiteral("`%1` contains an unrecognised marker (\"%2\" is left in the "
                                   "duration, so the note becomes a quarter)")
                        .arg(issue.token, issue.detail),
                    part.name, measure, issue.eventIndex);
                break;
            case TokenIssue::Code::EmptyChord:
                add(Severity::Error, QStringLiteral("E-CHORD-EMPTY"),
                    QStringLiteral("`%1` is a chord with no pitches").arg(issue.token), part.name,
                    measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UnterminatedChord:
                add(Severity::Error, QStringLiteral("E-CHORD-EMPTY"),
                    QStringLiteral("`%1` is missing its closing `>`").arg(issue.token), part.name,
                    measure, issue.eventIndex);
                break;
            case TokenIssue::Code::UnterminatedTuplet:
                add(Severity::Error, QStringLiteral("E-TUPLET-OPEN"),
                    QStringLiteral("a tuplet `{` is never closed in this measure"), part.name,
                    measure, issue.eventIndex);
                break;
            case TokenIssue::Code::StrayCloseBrace:
                add(Severity::Error, QStringLiteral("E-TUPLET-OPEN"),
                    QStringLiteral("`}` closes a tuplet that was never opened"), part.name,
                    measure, issue.eventIndex);
                break;
            case TokenIssue::Code::LeftoverText:
                add(Severity::Error, QStringLiteral("E-DUR-UNKNOWN"),
                    QStringLiteral("`%1` has unparsed text \"%2\" after its duration")
                        .arg(issue.token, issue.detail),
                    part.name, measure, issue.eventIndex);
                break;
            }
        }
    }

    // ----------------------------------------------------------------- measures

    QHash<QString, int> measureCounts;
    for (const Part &part : doc.parts) {
        measureCounts.insert(part.name, static_cast<int>(part.stream.measureCount()));
        const QList<Measure> &measures = part.stream.measures();
        for (int m = 0; m < measures.size(); ++m) {
            const int measureNumber = m + 1;
            const auto [num, den] = doc.timeSigForMeasure(measureNumber);
            const int expected = ticks::forTimeSignature(num, den);
            const int actual = measures.at(m).playedTicks();
            if (actual != expected) {
                add(Severity::Error, QStringLiteral("E-MEASURE"),
                    QStringLiteral("Measure duration mismatch in \"%1\" part \"%2\" measure %3: "
                                   "expected %4/%5 (%6 ticks), got %7 ticks")
                        .arg(doc.title.valueOr(QString()), part.name)
                        .arg(measureNumber)
                        .arg(num)
                        .arg(den)
                        .arg(expected)
                        .arg(actual),
                    part.name, measureNumber, -1, {}, -1,
                    actual < expected ? QStringLiteral("fill the remainder with a rest")
                                      : QString());
            }
        }
    }
    if (!measureCounts.isEmpty()) {
        // Bind the list: `values()` returns a temporary, so taking begin() and
        // end() from two separate calls would iterate across different objects.
        const QList<int> counts = measureCounts.values();
        const QSet<int> distinct(counts.begin(), counts.end());
        if (distinct.size() > 1) {
            QStringList detail;
            for (const Part *part : doc.partsInDisplayOrder())
                detail.append(QStringLiteral("%1=%2").arg(part->name)
                                  .arg(measureCounts.value(part->name)));
            add(Severity::Error, QStringLiteral("E-MEASURE-COUNT"),
                QStringLiteral("parts have differing measure counts: %1")
                    .arg(detail.join(QStringLiteral(", "))));
        }
    }

    // ------------------------------------------------------------------ lyrics

    QHash<QString, PartAlignment> alignments;
    for (const Part &part : doc.parts) {
        const PartAlignment alignment = alignPart(doc, part);
        alignments.insert(part.name, alignment);

        for (const QString &error : alignment.errors) {
            const QString rule = error.contains(QLatin1String("must not reference"))
                ? QStringLiteral("E-SHARED-NEST")
                : (error.contains(QLatin1String("malformed")) ? QStringLiteral("E-SHARED-MALFORMED")
                                                             : QStringLiteral("E-SHARED-REF"));
            add(Severity::Error, rule,
                QStringLiteral("Song \"%1\" %2").arg(doc.title.valueOr(QString()), error),
                part.name);
        }

        const int slotCount = static_cast<int>(alignment.lyricSlots.size());
        QSet<int> covered;
        for (const AttachedSection &section : alignment.sections) {
            const int syllables = static_cast<int>(section.syllables.size());
            const int last = section.slotOffset + syllables;
            for (int slot = section.slotOffset; slot < last && slot < slotCount; ++slot)
                covered.insert(slot);

            if (section.slotOffset >= slotCount && syllables > 0) {
                // Nothing attaches at all. This is the established idiom for a
                // part that sings only some sections (song 103's Bass2 answers
                // the chorus and inherits verse text it never sings), so it is
                // reported but not treated as a defect.
                add(Severity::Warning, QStringLiteral("W-SLOTS-UNSUNG-SECTION"),
                    QStringLiteral("none of these %1 syllables attach — the section starts at "
                                   "slot %2 but the part has %3 lyric slots, so this part does "
                                   "not sing it")
                        .arg(syllables)
                        .arg(section.slotOffset)
                        .arg(slotCount),
                    part.name, -1, -1, section.key, -1);
            } else if (last > slotCount) {
                add(Severity::Error, QStringLiteral("E-SLOTS"),
                    QStringLiteral("%1 syllables from slot %2, but the part has only %3 lyric "
                                   "slots — the seeder drops the last %4 without complaint")
                        .arg(syllables)
                        .arg(section.slotOffset)
                        .arg(slotCount)
                        .arg(last - slotCount),
                    part.name, -1, -1, section.key, slotCount);
            }
            if (!section.isChorus && !section.isCoda && alignment.maxVerseLength > 0
                && syllables != alignment.maxVerseLength) {
                add(Severity::Error, QStringLiteral("E-SLOTS"),
                    QStringLiteral("verse has %1 syllables but the longest verse of this part has "
                                   "%2 — pad the short verse with `_` placeholders rather than "
                                   "changing the notes")
                        .arg(syllables)
                        .arg(alignment.maxVerseLength),
                    part.name, -1, -1, section.key, -1,
                    QStringLiteral("balance verse lengths"));
            }
        }
        if (!alignment.sections.isEmpty() && covered.size() < slotCount) {
            QList<int> uncovered;
            for (int slot = 0; slot < slotCount; ++slot) {
                if (!covered.contains(slot))
                    uncovered.append(slot);
            }
            add(Severity::Warning, QStringLiteral("W-SLOTS-UNSUNG"),
                QStringLiteral("%1 lyric slot(s) carry no syllable in any verse (first: slot %2)")
                    .arg(uncovered.size())
                    .arg(uncovered.isEmpty() ? -1 : uncovered.first()),
                part.name, -1, -1, {}, uncovered.isEmpty() ? -1 : uncovered.first());
        }

        // A coda needs its @e marker, or the seeder refuses the song.
        const bool hasCoda = std::any_of(alignment.sections.begin(), alignment.sections.end(),
            [](const AttachedSection &section) { return section.isCoda; });
        if (hasCoda && !alignment.codaStartSlot)
            add(Severity::Error, QStringLiteral("E-CODA"),
                QStringLiteral("Song \"%1\" defines [lyrics.coda] but part has no @e marker")
                    .arg(doc.title.valueOr(QString())),
                part.name);
    }

    // Chorus lyrics with no @c anywhere (style guide §10.1).
    bool hasChorusLyrics = false;
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (SongDocument::isChorusKey(it.key()))
            hasChorusLyrics = true;
    }
    for (const Part &part : doc.parts) {
        for (auto it = part.lyrics.constBegin(); it != part.lyrics.constEnd(); ++it) {
            if (SongDocument::isChorusKey(it.key()))
                hasChorusLyrics = true;
        }
    }
    bool hasChorusMarker = false;
    for (const Part &part : doc.parts) {
        for (const Measure &measure : part.stream.measures()) {
            for (const Event &event : measure.events) {
                if (event.chorusStart)
                    hasChorusMarker = true;
            }
        }
    }
    if (hasChorusLyrics && !hasChorusMarker)
        add(Severity::Warning, QStringLiteral("R10.1"),
            QStringLiteral("song has chorus lyrics but no @c marker in any part"), {}, -1, -1,
            QStringLiteral("chorus"), -1, QStringLiteral("mark the chorus's first event"));

    // Syllable separator style.
    const auto checkSeparator = [&](const QString &owner, const QString &partName,
                                    const QString &key, const QString &text) {
        static const QRegularExpression tight(QStringLiteral("\\S--|--\\S"));
        if (tight.match(text).hasMatch())
            add(Severity::Warning, QStringLiteral("R11.1"),
                QStringLiteral("%1: syllable separator is not ` -- ` (found a tight `--`)")
                    .arg(owner),
                partName, -1, -1, key, -1, QStringLiteral("insert spaces around `--`"));
    };
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it)
        checkSeparator(QStringLiteral("lyrics.%1").arg(it.key()), {}, it.key(), it->rawText);
    for (const Part &part : doc.parts) {
        for (auto it = part.lyrics.constBegin(); it != part.lyrics.constEnd(); ++it) {
            checkSeparator(QStringLiteral("parts.%1.lyrics.%2").arg(part.name, it.key()),
                part.name, it.key(), it->rawText);
        }
    }

    // --------------------------------------------------------- notation style

    for (const Part &part : doc.parts) {
        const QString choral = part.choralType.valueOr(QString()).toLower();

        // Marker balance. Unbalanced markers cascade into bogus melisma state, so
        // the slot-based checks below are unreliable for that part.
        int slurOpen = 0;
        int slurClose = 0;
        int beamOpen = 0;
        int beamClose = 0;
        int dashedOpen = 0;
        int dashedClose = 0;
        for (const Measure &measure : part.stream.measures()) {
            for (const Event &event : measure.events) {
                slurOpen += event.slurStart;
                slurClose += event.slurEnd;
                beamOpen += event.beamStart;
                beamClose += event.beamEnd;
                dashedOpen += event.dashedSlurStart;
                dashedClose += event.dashedSlurEnd;
            }
        }
        const auto balance = [&](const QString &kind, int open, int close) {
            if (open != close)
                add(Severity::Warning, QStringLiteral("R-BAL"),
                    QStringLiteral("unbalanced %1 markers (%2 open, %3 close)")
                        .arg(kind)
                        .arg(open)
                        .arg(close),
                    part.name);
        };
        balance(QStringLiteral("slur"), slurOpen, slurClose);
        balance(QStringLiteral("beam"), beamOpen, beamClose);
        balance(QStringLiteral("dashed slur"), dashedOpen, dashedClose);

        for (const Group &group : collectGroups(part.stream)) {
            const QList<GroupMember> &members = group.members;
            if (members.isEmpty())
                continue;
            const int firstMeasure = members.first().measureIndex + 1;
            const int lastMeasure = members.last().measureIndex + 1;
            const QString raw = rawOf(members);
            const bool allBeamable = std::all_of(members.begin(), members.end(),
                [](const GroupMember &m) { return m.event->duration.notatedTicks() <= BeamableTicks; });
            QSet<QString> pitches;
            for (const GroupMember &member : members)
                pitches.insert(pitchKey(*member.event));
            const bool samePitch = pitches.size() == 1;
            const bool hasBeamMarks = std::any_of(members.begin(), members.end(),
                [](const GroupMember &m) { return m.event->beamStart || m.event->beamEnd; });

            if (group.kind == GroupKind::Slur && members.size() >= 2) {
                const bool tiedInside = std::any_of(members.begin(), members.end() - 1,
                    [](const GroupMember &m) { return m.event->tie; });
                if (samePitch && !tiedInside) {
                    add(Severity::Warning, QStringLiteral("R3.1"),
                        QStringLiteral("same-pitch slur should be a tie: `%1`").arg(raw),
                        part.name, firstMeasure, members.first().eventIndex, {}, -1,
                        QStringLiteral("convert the slur to a tie"));
                } else if (allBeamable && firstMeasure == lastMeasure && !hasBeamMarks) {
                    add(Severity::Warning, QStringLiteral("R1.1"),
                        QStringLiteral("all-short melisma is slurred; it should be beamed: `%1`")
                            .arg(raw),
                        part.name, firstMeasure, members.first().eventIndex, {}, -1,
                        QStringLiteral("convert the slur to a beam"));
                }
            }
            if (group.kind == GroupKind::Beam) {
                QStringList longNotes;
                for (const GroupMember &member : members) {
                    if (member.event->duration.notatedTicks() > BeamableTicks)
                        longNotes.append(member.event->raw);
                }
                if (!longNotes.isEmpty())
                    add(Severity::Warning, QStringLiteral("R1.2"),
                        QStringLiteral("beam contains a quarter-or-longer note (%1): `%2`")
                            .arg(longNotes.join(QStringLiteral(", ")), raw),
                        part.name, firstMeasure, members.first().eventIndex, {}, -1,
                        QStringLiteral("convert the beam to a slur"));
                if (firstMeasure != lastMeasure)
                    add(Severity::Warning, QStringLiteral("R1.4"),
                        QStringLiteral("beam crosses a barline: `%1`").arg(raw), part.name,
                        firstMeasure, members.first().eventIndex);
            }
            if (group.kind == GroupKind::Dashed && hasBeamMarks)
                add(Severity::Warning, QStringLiteral("R2.3"),
                    QStringLiteral("dashed-slur group is also beamed: `%1`").arg(raw), part.name,
                    firstMeasure, members.first().eventIndex);
        }

        // Ties.
        const Event *previous = nullptr;
        const QList<Measure> &measures = part.stream.measures();
        for (int m = 0; m < measures.size(); ++m) {
            for (const Event &event : measures.at(m).events) {
                if (previous && previous->tie) {
                    if (event.isRest() || event.isSpacer()) {
                        add(Severity::Warning, QStringLiteral("R3.4"),
                            QStringLiteral("tie into a rest or spacer: `%1` → `%2`")
                                .arg(previous->raw, event.raw),
                            part.name, m + 1, event.indexInMeasure);
                    } else if (pitchKey(event) != pitchKey(*previous)) {
                        add(Severity::Warning, QStringLiteral("R3.1"),
                            QStringLiteral("tie between different pitches: `%1` → `%2`")
                                .arg(previous->raw, event.raw),
                            part.name, m + 1, event.indexInMeasure);
                    }
                }
                previous = &event;
            }
        }

        // Hairpin termination.
        std::optional<std::pair<int, QString>> openHairpin;
        for (int m = 0; m < measures.size(); ++m) {
            for (const Event &event : measures.at(m).events) {
                if (event.hairpin == QLatin1String("crescendo")
                    || event.hairpin == QLatin1String("diminuendo"))
                    openHairpin = std::make_pair(m + 1, event.hairpin);
                else if (event.hairpin == QLatin1String("end")
                    || (openHairpin && !event.dynamic.isEmpty()))
                    openHairpin.reset();
            }
        }
        if (openHairpin)
            add(Severity::Warning, QStringLiteral("R5.2"),
                QStringLiteral("%1 opened in m%2 is never terminated with `\\!`")
                    .arg(openHairpin->second)
                    .arg(openHairpin->first),
                part.name, openHairpin->first, -1, {}, -1,
                QStringLiteral("terminate the hairpin"));

        // Tempo spanners belong to the soprano line only.
        if (choral != QLatin1String("soprano")) {
            for (int m = 0; m < measures.size(); ++m) {
                for (const Event &event : measures.at(m).events) {
                    // The terminator is as out of place as the start marker:
                    // the whole spanner apparatus is song-level and belongs on
                    // the soprano line.
                    if (!event.tempoSpanner.isEmpty())
                        add(Severity::Warning, QStringLiteral("R5.3"),
                            QStringLiteral("tempo spanner \\%1 on a non-soprano part")
                                .arg(event.tempoSpanner),
                            part.name, m + 1, event.indexInMeasure);
                    if (event.spannerEnd)
                        add(Severity::Warning, QStringLiteral("R5.3"),
                            QStringLiteral("tempo spanner \\spanend on a non-soprano part"),
                            part.name, m + 1, event.indexInMeasure);
                }
            }
        }
    }

    // Dynamics must appear on every part at the same moment: playback velocity is
    // per track, so a lone soprano marking leaves the other voices at default.
    {
        QHash<QString, QSet<int>> dynamicMeasures;
        for (const Part &part : doc.parts) {
            QSet<int> measuresWithDynamics;
            const QList<Measure> &measures = part.stream.measures();
            for (int m = 0; m < measures.size(); ++m) {
                for (const Event &event : measures.at(m).events) {
                    if (!event.dynamic.isEmpty())
                        measuresWithDynamics.insert(m);
                }
            }
            dynamicMeasures.insert(part.name, measuresWithDynamics);
        }
        QSet<int> union_;
        for (const QSet<int> &set : dynamicMeasures)
            union_.unite(set);
        QList<int> sorted(union_.begin(), union_.end());
        std::sort(sorted.begin(), sorted.end());
        for (const int m : sorted) {
            QStringList missing;
            for (const Part *part : doc.partsInDisplayOrder()) {
                if (!dynamicMeasures.value(part->name).contains(m))
                    missing.append(part->name);
            }
            if (!missing.isEmpty())
                add(Severity::Warning, QStringLiteral("R5.1"),
                    QStringLiteral("a dynamic here is missing on %1")
                        .arg(missing.join(QStringLiteral(", "))),
                    {}, m + 1, -1, {}, -1, QStringLiteral("copy the dynamic to all voices"));
        }
    }

    // Fermatas and staccatos go on every voice sounding at that tick.
    {
        struct Moment {
            int measure;
            int tick;
            bool operator<(const Moment &other) const
            {
                return measure != other.measure ? measure < other.measure : tick < other.tick;
            }
            bool operator==(const Moment &other) const = default;
        };
        const auto momentKey = [](const Moment &m) { return (qint64(m.measure) << 32) | m.tick; };

        QHash<QString, QSet<qint64>> sounding;
        QHash<QString, QSet<qint64>> fermatas;
        QHash<QString, QSet<qint64>> staccatos;
        QHash<qint64, Moment> moments;
        for (const Part &part : doc.parts) {
            const QList<Measure> &measures = part.stream.measures();
            for (int m = 0; m < measures.size(); ++m) {
                int tick = 0;
                for (const Event &event : measures.at(m).events) {
                    const Moment moment { m, tick };
                    const qint64 key = momentKey(moment);
                    moments.insert(key, moment);
                    if (event.isSounding())
                        sounding[part.name].insert(key);
                    if (event.fermata && !event.isSpacer())
                        fermatas[part.name].insert(key);
                    if (event.staccato && !event.isSpacer())
                        staccatos[part.name].insert(key);
                    tick += event.playedTicks();
                }
            }
        }
        const auto checkMarking = [&](const QHash<QString, QSet<qint64>> &marked,
                                      const QString &rule, const QString &label) {
            QHash<qint64, QStringList> haveAt;
            for (auto it = marked.constBegin(); it != marked.constEnd(); ++it) {
                for (const qint64 key : it.value())
                    haveAt[key].append(it.key());
            }
            QList<qint64> keys = haveAt.keys();
            std::sort(keys.begin(), keys.end());
            for (const qint64 key : keys) {
                QStringList missing;
                for (const Part *part : doc.partsInDisplayOrder()) {
                    if (!haveAt.value(key).contains(part->name)
                        && sounding.value(part->name).contains(key))
                        missing.append(part->name);
                }
                if (!missing.isEmpty()) {
                    const Moment moment = moments.value(key);
                    add(Severity::Warning, rule,
                        QStringLiteral("%1 at tick %2 is missing on sounding part(s) %3")
                            .arg(label)
                            .arg(moment.tick)
                            .arg(missing.join(QStringLiteral(", "))),
                        {}, moment.measure + 1, -1, {}, -1,
                        QStringLiteral("copy the marking to all sounding voices"));
                }
            }
        };
        checkMarking(fermatas, QStringLiteral("R6.1-fermata"), QStringLiteral("fermata"));
        checkMarking(staccatos, QStringLiteral("R6.1-staccato"), QStringLiteral("staccato"));
    }

    // ---------------------------------------------------------- phrase breaks

    if (!doc.phraseBreaks.present() || doc.phraseBreaks->isEmpty())
        add(Severity::Warning, QStringLiteral("R9.1"),
            QStringLiteral("no `phrase_breaks`; every poetic line end needs one"));

    {
        const int measureTotal = doc.measureCount();
        struct BreakEntry {
            PhraseBreak brk;
            QString field;
        };
        QList<BreakEntry> entries;
        const auto collect = [&entries](const Field<QList<PhraseBreak>> &field,
                                 const QString &name) {
            if (!field.present())
                return;
            for (const PhraseBreak &brk : *field)
                entries.append({ brk, name });
        };
        collect(doc.phraseBreaks, QStringLiteral("phrase_breaks"));
        collect(doc.optionalPhraseBreaks, QStringLiteral("optional_phrase_breaks"));
        collect(doc.nonBreakingPhraseBreaks, QStringLiteral("non_breaking_phrase_breaks"));

        for (const BreakEntry &entry : entries) {
            const int measureNumber = entry.brk.measure;
            if (measureNumber < 1 || measureNumber > measureTotal) {
                add(Severity::Error, QStringLiteral("E-TICK-GRID"),
                    QStringLiteral("%1 \"%2\" names measure %3, but the song has %4 measures")
                        .arg(entry.field, entry.brk.toString())
                        .arg(measureNumber)
                        .arg(measureTotal),
                    {}, measureNumber);
                continue;
            }
            const int measureTicks = doc.expectedTicksForMeasure(measureNumber);
            const int internal = ticks::fromPhraseTicks(entry.brk.tick);
            if (internal > measureTicks) {
                add(Severity::Error, QStringLiteral("E-TICK-GRID"),
                    QStringLiteral("%1 \"%2\" is past the end of measure %3 (%4 ticks of %5)")
                        .arg(entry.field, entry.brk.toString())
                        .arg(measureNumber)
                        .arg(entry.brk.tick)
                        .arg(ticks::toPhraseTicks(measureTicks)),
                    {}, measureNumber);
                continue;
            }

            QStringList partsWithoutBoundary;
            for (const Part *part : doc.partsInDisplayOrder()) {
                if (measureNumber > part->stream.measureCount())
                    continue;
                const Measure &measure = part->stream.measures().at(measureNumber - 1);
                bool boundary = internal == 0;
                int tick = 0;
                for (const Event &event : measure.events) {
                    tick += event.playedTicks();
                    if (tick == internal)
                        boundary = true;
                }
                if (!boundary)
                    partsWithoutBoundary.append(part->name);
            }
            if (!partsWithoutBoundary.isEmpty())
                add(Severity::Warning, QStringLiteral("R9.5"),
                    QStringLiteral("%1 \"%2\" does not fall on a note boundary in %3")
                        .arg(entry.field, entry.brk.toString(),
                            partsWithoutBoundary.join(QStringLiteral(", "))),
                    {}, measureNumber);

            // A break inside a melisma splits a syllable from its own notes.
            for (const Part *part : doc.partsInDisplayOrder()) {
                for (const Group &group : collectGroups(part->stream)) {
                    if (group.kind == GroupKind::Dashed || group.members.isEmpty())
                        continue;
                    if (group.members.first().measureIndex != group.members.last().measureIndex)
                        continue;
                    if (group.members.first().measureIndex != measureNumber - 1)
                        continue;
                    const Measure &measure
                        = part->stream.measures().at(group.members.first().measureIndex);
                    int tick = 0;
                    QList<int> tickOf;
                    for (const Event &event : measure.events) {
                        tickOf.append(tick);
                        tick += event.playedTicks();
                    }
                    const int begin = tickOf.value(group.members.first().eventIndex);
                    const int end = tickOf.value(group.members.last().eventIndex)
                        + group.members.last().event->playedTicks();
                    if (internal > begin && internal < end) {
                        add(Severity::Warning, QStringLiteral("R9.4"),
                            QStringLiteral("%1 \"%2\" falls inside a melisma (ticks %3–%4)")
                                .arg(entry.field, entry.brk.toString())
                                .arg(ticks::toPhraseTicks(begin))
                                .arg(ticks::toPhraseTicks(end)),
                            part->name, measureNumber);
                        break;
                    }
                }
            }
        }
    }

    // Dashed slurs exist for verses that genuinely disagree.
    for (const Part &part : doc.parts) {
        if (!part.lyrics.isEmpty())
            continue;
        const PartAlignment &alignment = alignments[part.name];
        QList<AttachedSection> verses;
        for (const AttachedSection &section : alignment.sections) {
            if (!section.isChorus && !section.isCoda)
                verses.append(section);
        }
        if (verses.isEmpty())
            continue;
        for (int slot = 0; slot < alignment.lyricSlots.size(); ++slot) {
            if (!alignment.lyricSlots.at(slot).dashedContinuation)
                continue;
            QStringList values;
            for (const AttachedSection &verse : verses) {
                const QString syllable = alignment.syllableAt(verse, slot);
                if (!syllable.isEmpty())
                    values.append(syllable);
            }
            if (values.isEmpty())
                continue;
            const bool allPlaceholders = std::all_of(values.begin(), values.end(),
                [](const QString &value) { return value == QLatin1String("_"); });
            const bool nonePlaceholders = std::none_of(values.begin(), values.end(),
                [](const QString &value) { return value == QLatin1String("_"); });
            const int measureNumber = alignment.lyricSlots.at(slot).measureIndex + 1;
            if (allPlaceholders)
                add(Severity::Warning, QStringLiteral("R2.1"),
                    QStringLiteral("dashed slur where every verse uses `_` — this is an ordinary "
                                   "melisma, so use a slur or beam"),
                    part.name, measureNumber, -1, {}, slot,
                    QStringLiteral("convert to a slur or beam"));
            else if (nonePlaceholders)
                add(Severity::Warning, QStringLiteral("R2.1"),
                    QStringLiteral("dashed slur where no verse uses `_` — the marking may be "
                                   "unnecessary"),
                    part.name, measureNumber, -1, {}, slot);
        }
    }

    // ------------------------------------------------------------ echo splice

    for (const Part &part : doc.parts) {
        const SpliceReport report = analyseSplice(doc, part);
        if (!report.configured)
            continue;
        if (report.targetPartName.isEmpty()) {
            add(Severity::Warning, QStringLiteral("W-SPLICE-TARGET"),
                QStringLiteral("splice_lyrics_into = \"%1\" but no part has that choral_type")
                    .arg(report.targetChoralType),
                part.name);
            continue;
        }
        for (const SpliceReport::VerseResult &verse : report.verses) {
            if (!verse.splices)
                add(Severity::Info, QStringLiteral("I-SPLICE"),
                    QStringLiteral("verse will not splice into %1: %2")
                        .arg(report.targetPartName, verse.reason),
                    part.name, -1, -1, verse.key);
        }
    }

    // --------------------------------------------------------------- metadata

    if (doc.keySignature.present()) {
        QString key = *doc.keySignature;
        if (key.endsWith(u'm'))
            key.chop(1);
        if (!validKeySignatures().contains(key))
            add(Severity::Warning, QStringLiteral("W-KEY"),
                QStringLiteral("key signature \"%1\" is not one of the documented keys")
                    .arg(*doc.keySignature));
    }
    for (const Part &part : doc.parts) {
        if (part.clef.present() && !validClefs().contains(*part.clef))
            add(Severity::Warning, QStringLiteral("W-CLEF"),
                QStringLiteral("clef \"%1\" is not treble, bass, or treble_8").arg(*part.clef),
                part.name);
        if (part.choralType.present() && !validChoralTypes().contains(part.choralType->toLower()))
            add(Severity::Warning, QStringLiteral("W-CT"),
                QStringLiteral("choral_type \"%1\" is not soprano, alto, tenor, or bass")
                    .arg(*part.choralType),
                part.name);
        const bool hasSuppress = part.suppressVerses.present()
            && !part.suppressVerses->isEmpty();
        const bool hasWhen = part.suppressVersesWhen.present()
            && !part.suppressVersesWhen->isEmpty();
        if (hasSuppress != hasWhen)
            add(Severity::Warning, QStringLiteral("W-SUPPRESS"),
                QStringLiteral("suppress_verses and suppress_verses_when must both be present; "
                               "with only one, no suppression happens"),
                part.name);
    }

    if (doc.verseCount.present()) {
        const int declared = *doc.verseCount;
        const int actual = static_cast<int>(doc.verseKeys().size());
        if (actual > 0 && declared != actual)
            add(Severity::Warning, QStringLiteral("W-VERSECOUNT"),
                QStringLiteral("verse_count is %1 but %2 numbered lyric section(s) are defined")
                    .arg(declared)
                    .arg(actual));
    }

    if (doc.copyrights.present()) {
        const QStringList lines = *doc.copyrights;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i);
            if (line.trimmed().endsWith(u'.'))
                add(Severity::Warning, QStringLiteral("C1"),
                    QStringLiteral("copyright line %1 ends with a period").arg(i + 1), {}, -1, -1,
                    {}, -1, QStringLiteral("strip the trailing period"));
            static const QRegularExpression unbalancedLink(
                QStringLiteral("\\[[^\\]]*\\](?!\\()"));
            if (unbalancedLink.match(line).hasMatch())
                add(Severity::Warning, QStringLiteral("C1"),
                    QStringLiteral("copyright line %1 has `[text]` with no `(url)` after it")
                        .arg(i + 1));
        }
        const auto arrangement = std::find_if(lines.begin(), lines.end(), [](const QString &line) {
            return line.contains(QLatin1String("Arrangement by OpenPsalm"))
                || line.contains(QLatin1String("Arreglo de OpenPsalm"));
        });
        if (arrangement != lines.end() && arrangement != lines.end() - 1)
            add(Severity::Warning, QStringLiteral("C1"),
                QStringLiteral("the OpenPsalm arrangement line should be last"), {}, -1, -1, {},
                -1, QStringLiteral("move the arrangement line last"));
    }

    // ------------------------------------------------------------ translation

    if (doc.isOverlay) {
        if (!doc.copyrights.dirty() && !doc.copyrights.span().isValid())
            add(Severity::Warning, QStringLiteral("T3"),
                QStringLiteral("this translation does not set `copyrights`, so the page would "
                               "show credits in the base language"));
        for (const Part &part : doc.parts) {
            if (!part.notesInherited && part.notes.span().isValid())
                add(Severity::Info, QStringLiteral("T1"),
                    QStringLiteral("this translation overrides the notation of part \"%1\"; later "
                                   "fixes to the base notes will not reach it")
                        .arg(part.name),
                    part.name);
        }
        const int declared = doc.verseCount.valueOr(0);
        const int actual = static_cast<int>(doc.verseKeys().size());
        if (declared > 0 && actual > 0 && actual < declared)
            add(Severity::Warning, QStringLiteral("T2"),
                QStringLiteral("the translation defines %1 of %2 verses; a lyric map replaces the "
                               "base map wholesale, so the rest would be missing rather than "
                               "inherited")
                    .arg(actual)
                    .arg(declared));
        for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
            for (const QString &syllable : it->syllables()) {
                if (syllable.contains(u' '))
                    add(Severity::Warning, QStringLiteral("T4"),
                        QStringLiteral("a syllable contains a space; two words sung on one note "
                                       "are joined with an undertie ‿ (U+203F)"),
                        {}, -1, -1, it.key());
            }
        }
    }

    return findings;
}

} // namespace ope
