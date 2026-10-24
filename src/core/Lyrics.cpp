// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Lyrics.h"
#include "Voicing.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace ope {
namespace {

QStringList splitSyllables(const QString &text)
{
    QStringList out;
    for (const QString &token :
        text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (token != QLatin1String("--"))
            out.append(token);
    }
    return out;
}

using LyricPhrase = QList<QPair<int, QString>>;
using VersePhrases = QMap<QString, QList<LyricPhrase>>;

// Mirror the print exporter's phrase fingerprints: exact text and onset ticks,
// split at every kind of phrase boundary. Dedup offsets affect fingerprints,
// never the alignment grid or playback.
QMap<QString, VersePhrases> survivingVersePhrases(const SongDocument &doc)
{
    QList<int> measureStarts { 0 };
    for (int m = 1; m <= doc.measureCount(); ++m)
        measureStarts.append(measureStarts.last() + doc.expectedTicksForMeasure(m));
    QList<int> breaks;
    for (const PhraseBreak &brk : doc.allPhraseBreaks()) {
        if (brk.measure >= 1 && brk.measure <= doc.measureCount())
            breaks.append(measureStarts.at(brk.measure - 1) + ticks::fromPhraseTicks(brk.tick));
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

    QSet<QString> roles;
    for (const Part &part : doc.parts)
        roles.insert(voicing::normalize(part.choralType.valueOr({})));

    QMap<QString, QList<LyricPhrase>> claimed;
    QMap<QString, VersePhrases> survivors;
    for (const Part *part : doc.partsInDisplayOrder()) {
        bool suppress = false;
        for (const QString &role : part->suppressVersesWhen.valueOr({}))
            suppress |= roles.contains(voicing::normalize(role));

        VersePhrases original;
        VersePhrases &remaining = survivors[part->name];
        const PartAlignment alignment = alignPart(doc, *part);
        for (const AttachedSection &section : alignment.sections) {
            if (section.isChorus || section.isCoda
                || (suppress && part->suppressVerses.valueOr({}).contains(section.verseNumber)))
                continue;

            LyricPhrase lyrics;
            for (int i = 0; i < section.syllables.size(); ++i) {
                const int index = section.slotOffset + i;
                if (index >= alignment.lyricSlots.size())
                    break; // E-SLOTS reports unattachable text separately.
                const Slot &slot = alignment.lyricSlots.at(index);
                const Event &event = part->stream.measures().at(slot.measureIndex)
                                         .events.at(slot.eventIndex);
                lyrics.append({ slot.tick + event.dedupOffset, section.syllables.at(i) });
            }

            QList<LyricPhrase> &phrases = original[section.key];
            LyricPhrase phrase;
            for (int i = 0; i < lyrics.size(); ++i) {
                phrase.append(lyrics.at(i));
                const auto boundary = std::upper_bound(breaks.cbegin(), breaks.cend(), lyrics.at(i).first);
                if (i + 1 == lyrics.size()
                    || (boundary != breaks.cend() && lyrics.at(i + 1).first >= *boundary)) {
                    phrases.append(phrase);
                    phrase.clear();
                }
            }
            QList<LyricPhrase> &kept = remaining[section.key];
            for (const LyricPhrase &fp : phrases) {
                if (!claimed[section.key].contains(fp)) {
                    claimed[section.key].append(fp);
                    kept.append(fp);
                }
            }
        }

        // A shared phrase in all active verses prints only in the first verse
        // when convergence is enabled. Apply this after cross-voice dedup.
        if (doc.effectiveConvergeVerses() && original.size() > 1) {
            QStringList keys = original.keys();
            std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
                return a.toInt() < b.toInt();
            });
            const auto &first = original[keys.first()];
            for (int p = 0; p < first.size(); ++p) {
                const bool shared = std::all_of(std::next(keys.cbegin()), keys.cend(),
                    [&](const QString &key) {
                        const auto &other = original[key];
                        return p < other.size() && other.at(p) == first.at(p);
                    });
                if (shared) {
                    for (int v = 1; v < keys.size(); ++v)
                        remaining[keys.at(v)].removeAll(first.at(p));
                }
            }
        }
    }
    return survivors;
}

} // namespace

QString PartAlignment::syllableAt(const AttachedSection &section, int slot) const
{
    const int index = slot - section.slotOffset;
    if (index < 0 || index >= section.syllables.size())
        return {};
    return section.syllables.at(index);
}

QMap<QString, QString> mergedLyricTexts(
    const SongDocument &doc, const Part &part, QSet<QString> *inheritedKeys)
{
    QMap<QString, QString> merged;
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        merged.insert(it.key(), it->rawText);
        if (inheritedKeys)
            inheritedKeys->insert(it.key());
    }
    for (auto it = part.lyrics.constBegin(); it != part.lyrics.constEnd(); ++it) {
        merged.insert(it.key(), it->rawText);
        if (inheritedKeys)
            inheritedKeys->remove(it.key());
    }
    return merged;
}

QMap<QString, QString> expandSharedLyrics(
    const QMap<QString, QString> &merged, QStringList *errors)
{
    const auto fail = [errors](const QString &message) {
        if (errors)
            errors->append(message);
    };

    QMap<QString, QString> shared;
    QMap<QString, QString> result;
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
        if (SongDocument::isSharedKey(it.key()))
            shared.insert(it.key(), it.value());
        else
            result.insert(it.key(), it.value());
    }

    // A shared section may not reference another shared section.
    for (auto it = shared.constBegin(); it != shared.constEnd(); ++it) {
        for (const QString &token :
            it.value().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            if (token.startsWith(QLatin1String("@s"))) {
                fail(QStringLiteral(
                    "shared lyric section [lyrics.%1] must not reference another shared section")
                        .arg(it.key()));
            }
        }
    }

    for (auto it = result.begin(); it != result.end(); ++it) {
        if (!it.value().contains(QLatin1String("@s")))
            continue;
        QStringList expanded;
        for (const QString &token :
            it.value().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            if (!token.startsWith(u'@')) {
                expanded.append(token);
                continue;
            }
            const QString reference = token.sliced(1);
            if (!SongDocument::isSharedKey(reference)) {
                fail(QStringLiteral("lyrics.%1: malformed shared reference \"%2\" "
                                    "(must be a standalone @sN token)")
                        .arg(it.key(), token));
                continue;
            }
            const auto found = shared.constFind(reference);
            if (found == shared.constEnd()) {
                fail(QStringLiteral("lyrics.%1: reference \"%2\" has no [lyrics.%3] section")
                        .arg(it.key(), token, reference));
                continue;
            }
            expanded.append(
                found->split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts));
        }
        it.value() = expanded.join(u' ');
    }
    return result;
}

PartAlignment alignPart(const SongDocument &doc, const Part &part)
{
    PartAlignment alignment;
    alignment.partName = part.name;

    // -- lyricSlots, in stream order, tracking @c / @e which defer to the next slot.
    SlotCounter counter;
    bool chorusPending = false;
    bool codaPending = false;
    bool inDashed = false;
    int absoluteTick = 0;
    const QList<Measure> &measures = part.stream.measures();
    for (int m = 0; m < measures.size(); ++m) {
        int tickInMeasure = 0;
        const QList<Event> &events = measures.at(m).events;
        for (int e = 0; e < events.size(); ++e) {
            const Event &event = events.at(e);
            const bool isSlot = counter.isSlot(event);
            chorusPending |= event.chorusStart;
            codaPending |= event.codaStart;

            const bool dashedHere = inDashed || event.dashedSlurStart;
            if (event.dashedSlurStart)
                inDashed = true;
            if (event.dashedSlurEnd)
                inDashed = false;

            if (isSlot) {
                if (chorusPending && !alignment.chorusStartSlot)
                    alignment.chorusStartSlot = static_cast<int>(alignment.lyricSlots.size());
                if (codaPending && !alignment.codaStartSlot)
                    alignment.codaStartSlot = static_cast<int>(alignment.lyricSlots.size());
                chorusPending = false;
                codaPending = false;
                alignment.lyricSlots.append(Slot { m, e, absoluteTick + tickInMeasure, tickInMeasure,
                    dashedHere && !event.dashedSlurStart });
            }
            tickInMeasure += event.playedTicks();
        }
        absoluteTick += tickInMeasure;
    }

    // -- lyric texts
    QSet<QString> inherited;
    QMap<QString, QString> texts = mergedLyricTexts(doc, part, &inherited);
    texts = expandSharedLyrics(texts, &alignment.errors);

    const auto isRealVerse = [](const QString &key, const QString &text) {
        return !SongDocument::isChorusKey(key) && !SongDocument::isCodaKey(key)
            && !splitSyllables(text).isEmpty();
    };

    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        if (isRealVerse(it.key(), it.value())) {
            alignment.hasVerseLyrics = true;
            alignment.maxVerseLength = std::max<int>(
                alignment.maxVerseLength, static_cast<int>(splitSyllables(it.value()).size()));
        }
        if (SongDocument::isChorusKey(it.key())) {
            alignment.maxChorusLength = std::max<int>(
                alignment.maxChorusLength, static_cast<int>(splitSyllables(it.value()).size()));
        }
    }
    alignment.chorusFirst = alignment.chorusStartSlot.value_or(-1) == 0;

    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        const QString &key = it.key();
        const bool isChorus = SongDocument::isChorusKey(key);
        const bool isCoda = SongDocument::isCodaKey(key);
        if (!isChorus && !isCoda && !isRealVerse(key, it.value()))
            continue;

        AttachedSection section;
        section.key = key;
        section.isChorus = isChorus;
        section.isCoda = isCoda;
        section.verseNumber = (isChorus || isCoda) ? 1 : std::max(1, key.toInt());
        section.syllables = splitSyllables(it.value());
        section.textInherited = inherited.contains(key);

        if (isCoda) {
            section.slotOffset = alignment.codaStartSlot.value_or(0);
        } else if (isChorus) {
            if (alignment.chorusFirst)
                section.slotOffset = 0;
            else if (alignment.hasVerseLyrics)
                section.slotOffset = alignment.maxVerseLength;
            else
                section.slotOffset = alignment.chorusStartSlot.value_or(0);
        } else {
            section.slotOffset = alignment.chorusFirst ? alignment.maxChorusLength : 0;
        }
        alignment.sections.append(section);
    }

    std::stable_sort(alignment.sections.begin(), alignment.sections.end(),
        [](const AttachedSection &a, const AttachedSection &b) {
            const auto rank = [](const AttachedSection &s) {
                return s.isCoda ? 3 : (s.isChorus ? 2 : 1);
            };
            if (rank(a) != rank(b))
                return rank(a) < rank(b);
            return a.verseNumber < b.verseNumber;
        });

    return alignment;
}

SpliceReport analyseSplice(const SongDocument &doc, const Part &part)
{
    SpliceReport report;
    if (!part.spliceLyricsInto.present() || part.spliceLyricsInto->trimmed().isEmpty())
        return report;

    report.configured = true;
    report.targetChoralType = voicing::normalize(*part.spliceLyricsInto);

    const Part *target = nullptr;
    for (const Part *candidate : doc.partsInDisplayOrder()) {
        if (candidate->name == part.name)
            continue;
        if (voicing::normalize(candidate->choralType.valueOr({})) == report.targetChoralType) {
            target = candidate;
            break;
        }
    }
    if (!target) {
        report.verses.append({ QString(), false,
            QStringLiteral("no part has choral_type \"%1\"").arg(report.targetChoralType) });
        return report;
    }
    report.targetPartName = target->name;

    const auto survivors = survivingVersePhrases(doc);
    const VersePhrases source = survivors.value(part.name);
    const VersePhrases destination = survivors.value(target->name);
    for (auto verse = source.cbegin(); verse != source.cend(); ++verse) {
        LyricPhrase sourceLyrics;
        for (const LyricPhrase &phrase : verse.value())
            sourceLyrics.append(phrase);
        LyricPhrase targetLyrics;
        for (const LyricPhrase &phrase : destination.value(verse.key()))
            targetLyrics.append(phrase);
        // Suppressed/deduplicated verses have no row to splice or hand off from.
        if (sourceLyrics.isEmpty() || targetLyrics.isEmpty())
            continue;

        SpliceReport::VerseResult result;
        result.key = verse.key();
        QMap<int, int> echoesPerAnchor;
        for (const auto &[tick, text] : sourceLyrics) {
            Q_UNUSED(text);
            int anchor = -1;
            bool simultaneous = false;
            for (int i = 0; i < targetLyrics.size(); ++i) {
                if (targetLyrics.at(i).first < tick)
                    anchor = i;
                simultaneous |= targetLyrics.at(i).first == tick;
            }
            if (simultaneous) {
                result.reason = QStringLiteral("surviving syllables at tick %1 coincide with %2's "
                                               "lyrics; isolate echo phrases with phrase boundaries")
                                    .arg(tick).arg(target->name);
                break;
            }
            if (anchor < 0) {
                result.reason = QStringLiteral("echo at tick %1 has no earlier syllable in %2 "
                                               "to hand off from").arg(tick).arg(target->name);
                break;
            }
            ++echoesPerAnchor[anchor];
        }
        if (result.reason.isEmpty()) {
            for (auto echo = echoesPerAnchor.cbegin(); echo != echoesPerAnchor.cend(); ++echo) {
                if (echo.key() != targetLyrics.size() - 1 && echo.value() < 2) {
                    result.reason = QStringLiteral("a mid-verse echo needs at least two syllables "
                                                   "to hand back to %1").arg(target->name);
                    break;
                }
            }
        }
        result.splices = result.reason.isEmpty();
        report.verses.append(result);
    }
    return report;
}

} // namespace ope
