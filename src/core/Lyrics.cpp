// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Lyrics.h"

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
    report.targetChoralType = part.spliceLyricsInto->toLower();

    const Part *target = nullptr;
    for (const Part &candidate : doc.parts) {
        if (candidate.name == part.name)
            continue;
        if (candidate.choralType.valueOr(QString()).toLower() == report.targetChoralType) {
            target = &candidate;
            break;
        }
    }
    if (!target) {
        report.verses.append({ QString(), false,
            QStringLiteral("no part has choral_type \"%1\"").arg(report.targetChoralType) });
        return report;
    }
    report.targetPartName = target->name;

    const PartAlignment source = alignPart(doc, part);
    const PartAlignment destination = alignPart(doc, *target);

    // The splice only fires for a verse whose syllables are a pure tail: every
    // one of this part's syllables must start after the target's last one.
    for (const AttachedSection &section : source.sections) {
        const auto match = std::find_if(destination.sections.begin(), destination.sections.end(),
            [&](const AttachedSection &other) { return other.key == section.key; });
        SpliceReport::VerseResult result;
        result.key = section.key;
        if (match == destination.sections.end()) {
            result.splices = false;
            result.reason = QStringLiteral("target part has no lyrics for this verse");
            report.verses.append(result);
            continue;
        }

        const int sourceFirstSlot = section.slotOffset;
        const int targetLastSlot = match->slotOffset + static_cast<int>(match->syllables.size()) - 1;
        const int sourceFirstTick = sourceFirstSlot < source.lyricSlots.size()
            ? source.lyricSlots.at(sourceFirstSlot).tick
            : 0;
        const int targetLastTick = targetLastSlot >= 0 && targetLastSlot < destination.lyricSlots.size()
            ? destination.lyricSlots.at(targetLastSlot).tick
            : 0;

        // Compare by tick rather than slot index: the two parts have their own
        // slot numbering, and it is musical position that decides a tail.
        int firstInterleavingTick = -1;
        for (int slot = sourceFirstSlot;
            slot < sourceFirstSlot + section.syllables.size() && slot < source.lyricSlots.size();
            ++slot) {
            if (source.lyricSlots.at(slot).tick <= targetLastTick) {
                firstInterleavingTick = source.lyricSlots.at(slot).tick;
            }
        }
        if (firstInterleavingTick < 0) {
            result.splices = true;
        } else {
            result.splices = false;
            result.reason = QStringLiteral(
                "syllables at tick %1 are not after %2's last syllable (tick %3), so this verse "
                "prints on its own row")
                                .arg(firstInterleavingTick)
                                .arg(target->name)
                                .arg(targetLastTick);
        }
        Q_UNUSED(sourceFirstTick);
        report.verses.append(result);
    }
    return report;
}

} // namespace ope
