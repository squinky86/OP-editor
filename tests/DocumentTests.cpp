// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Lyric slots, overlay merging, and saving.
//
// The slot and overlay cases are ports of the tests in OpenPsalm's
// src/seed/importer.rs and src/seed/data.rs.

#include "Fixtures.h"

#include "core/Lyrics.h"
#include "core/Playback.h"
#include "core/Song.h"
#include "core/Validator.h"
#include "core/Voicing.h"

#include <QTemporaryDir>
#include <QTest>

using namespace ope;

namespace {

/// Write `contents` to a temp file and load it, so tests exercise the real
/// loader rather than a hand-built document.
SongDocument loadFrom(QTemporaryDir &dir, const QString &name, const QByteArray &contents)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(contents);
    file.close();
    auto doc = io::load(path);
    Q_ASSERT(doc.has_value());
    return *doc;
}

using namespace ope::fixtures;

} // namespace

class DocumentTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void ttbbRoundTripsAndClefEditsNeverTranspose()
    {
        const auto loaded = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(loaded);
        SongDocument doc = *loaded;
        QCOMPARE(io::serialize(doc), ttbbSong());
        const auto findings = validate(doc);
        QCOMPARE(countBySeverity(findings, Severity::Error), 0);
        for (const auto &finding : findings) {
            QVERIFY(finding.rule != "W-CLEF");
            QVERIFY(finding.rule != "W-CT");
        }
        const auto plan = buildPlan(doc);
        const QList<int> opening {46, 53, 50, 58};
        const QList<int> lastBeat {51, 57, 53, 60};
        const QList<int> final {46, 58, 53, 62};
        for (int i = 0; i < 4; ++i) {
            QList<int> pitches;
            for (const auto &note : plan.notes) {
                if (note.partIndex == i)
                    pitches.append(note.midiNote);
            }
            QCOMPARE(pitches, QList<int>({opening.at(i), lastBeat.at(i), final.at(i)}));
        }
        for (const QString &clef : validClefs()) {
            doc.parts[3].clef.set(clef);
            const auto changed = buildPlan(doc);
            QCOMPARE(changed.notes.size(), plan.notes.size());
            for (int i = 0; i < plan.notes.size(); ++i) {
                QCOMPARE(changed.notes.at(i).midiNote, plan.notes.at(i).midiNote);
                QCOMPARE(changed.notes.at(i).startSeconds, plan.notes.at(i).startSeconds);
            }
            QByteArray expected = ttbbSong();
            const int start = expected.indexOf("[parts.Tenor1]");
            const int value = expected.indexOf("clef = \"tenor\"", start);
            expected.replace(value, QByteArray("clef = \"tenor\"").size(), "clef = \"" + clef.toUtf8() + "\"");
            QCOMPARE(io::serialize(doc), expected);
        }
    }

    void normalizedMetadataIsInterpretedWithoutChangingBytes()
    {
        QByteArray bytes = ttbbSong();
        bytes.replace("\"tenor1\"", "\" TeNoR1 \"");
        bytes.replace("\"tenor\"", "\" TeNoR \"");
        const auto doc = io::loadBytes("song.toml", bytes);
        QVERIFY(doc);
        QCOMPARE(io::serialize(*doc), bytes);
        QCOMPARE(countBySeverity(validate(*doc), Severity::Error), 0);
        QCOMPARE(voicing::lead(*doc)->name, QStringLiteral("Tenor1"));
        QVERIFY(!doc->isDirty());
    }

    void invalidClefsAndStavesAreErrors_data()
    {
        QTest::addColumn<QString>("clef");
        QTest::addColumn<int>("staff");
        QTest::addColumn<QString>("rule");
        QTest::newRow("bare C") << QStringLiteral("C") << 1 << QStringLiteral("E-CLEF");
        QTest::newRow("role used as clef") << QStringLiteral("baritone") << 1 << QStringLiteral("E-CLEF");
        QTest::newRow("empty clef") << QString() << 1 << QStringLiteral("E-CLEF");
        QTest::newRow("zero staff") << QStringLiteral("tenor") << 0 << QStringLiteral("E-STAFF");
        QTest::newRow("negative staff") << QStringLiteral("tenor") << -42 << QStringLiteral("E-STAFF");
        QTest::newRow("conflict") << QStringLiteral("alto") << 1 << QStringLiteral("E-STAFF-CLEF");
    }

    void invalidClefsAndStavesAreErrors()
    {
        QFETCH(QString, clef);
        QFETCH(int, staff);
        QFETCH(QString, rule);
        auto doc = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(doc);
        doc->parts[3].clef.set(clef);
        doc->parts[3].staffNumber.set(staff);
        const auto findings = validate(*doc);
        const auto found = std::find_if(findings.cbegin(), findings.cend(), [&](const Finding &f) {
            return f.rule == rule && f.severity == Severity::Error;
        });
        QVERIFY(found != findings.cend());
        QVERIFY(found->message.contains("TTBB fixture"));
        QVERIFY(found->message.contains("Tenor1"));
        const QByteArray bytes = io::serialize(*doc);
        const auto reparsed = io::loadBytes("song.toml", bytes);
        QVERIFY(reparsed);
        QCOMPARE(io::serialize(*reparsed), bytes); // invalid drafts are still editable
    }

    void omittedClefHasAnEffectiveTrebleDefaultOnSharedStaves()
    {
        auto doc = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(doc);
        doc->parts[0].clef.clear(); // Bass now conflicts with Baritone
        const auto findings = validate(*doc);
        QVERIFY(std::any_of(findings.cbegin(), findings.cend(), [](const Finding &f) {
            return f.rule == "E-STAFF-CLEF" && f.message.contains("treble");
        }));
    }

    void tempoOwnershipIsByLeadIdentityIncludingRepeatedRoles()
    {
        auto doc = io::loadBytes("song.toml", ttbbTempo());
        QVERIFY(doc);
        const auto count = [](const SongDocument &d) {
            int result = 0;
            for (const auto &f : validate(d))
                result += f.rule == "R5.3";
            return result;
        };
        QCOMPARE(count(*doc), 0);
        Part extra = *doc->part(u"Lead");
        extra.name = "Lead2";
        doc->parts.prepend(extra);
        QCOMPARE(count(*doc), 3); // start, end, and a tempo all belong to Lead
        doc->parts.removeFirst();
        doc->part(u"Lead")->choralType.set("bass"); // Second becomes lead
        QCOMPARE(count(*doc), 3);
        doc->parts.removeFirst(); // lone bass is a valid lead
        QCOMPARE(count(*doc), 0);
    }

    void mutedLeadKeepsTheExactTempoMapWithDifferentRhythms()
    {
        auto doc = io::loadBytes("song.toml", ttbbTempo());
        QVERIFY(doc);
        PlaybackOptions solo;
        solo.mutedParts = {"Lead"};
        const auto plan = buildPlan(*doc, solo);
        QCOMPARE(plan.notes.size(), 4);
        // Independently decoded current OpenPsalm MIDI: 480 PPQ, eight ramp
        // steps at 180..1440, then tempo 120 restored at tick 1920.
        const QList<double> onsets {0.0, 1.1314985, 2.687817125, 3.687817125};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(plan.notes.at(i).partIndex, 0);
            QVERIFY(qAbs(plan.notes.at(i).startSeconds - onsets.at(i)) < 0.0000001);
        }
        QVERIFY(qAbs(plan.totalSeconds - 4.687817125) < 0.0000001);
        const auto ensemble = buildPlan(*doc);
        for (const auto &note : ensemble.notes) {
            if (note.partIndex == 0) {
                const auto found = std::find_if(plan.notes.cbegin(), plan.notes.cend(), [&](const auto &n) {
                    return n.startTick == note.startTick;
                });
                QVERIFY(found != plan.notes.cend());
                QCOMPARE(found->startSeconds, note.startSeconds);
                QCOMPARE(found->endSeconds, note.endSeconds);
            }
        }
        solo.mutedParts.append("Second");
        QVERIFY(buildPlan(*doc, solo).notes.isEmpty());
        // Invalid markers on a non-lead must not acquire ownership.
        doc->part(u"Lead")->notes.set("c1 | c1");
        doc->part(u"Lead")->reparse();
        doc->part(u"Second")->notes.set(QStringLiteral("c2\\rit g2\\spanend | c2 g2"));
        doc->part(u"Second")->reparse();
        QCOMPARE(buildPlan(*doc).totalSeconds, 4.0);
        doc->parts.removeLast(); // missing T1: deterministic T2 lead fallback
        QVERIFY(buildPlan(*doc).totalSeconds > 4.0);
    }

    void tiesAndTupletsKeepTheirPitchesUnderEveryClef()
    {
        for (const QString &clef : validClefs()) {
            auto doc = io::loadBytes("song.toml", QByteArray("title = \"T\"\ntempo_bpm = 120\n[parts.Solo]\nnotes = \"c'2~ c'2 | {3 c'8 d'8 e'8} r2.\"\n"));
            QVERIFY(doc);
            doc->parts[0].clef.set(clef);
            const auto plan = buildPlan(*doc);
            QCOMPARE(plan.notes.size(), 4);
            QCOMPARE(plan.notes[0].midiNote, 60);
            QCOMPARE(plan.notes[0].endSeconds, 2.0);
            QCOMPARE(plan.notes[1].midiNote, 60);
            QCOMPARE(plan.notes[2].midiNote, 62);
            QCOMPARE(plan.notes[3].midiNote, 64);
            QVERIFY(qAbs(plan.notes[1].endSeconds - 2.1666666667) < 0.000001);
        }
    }

    void allTtbbVoiceSubsetsKeepIndependentIdentities()
    {
        const auto doc = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(doc);
        for (int mask = 0; mask < 16; ++mask) {
            PlaybackOptions options;
            int audible = 0;
            for (int i = 0; i < 4; ++i) {
                if (!(mask & (1 << i)))
                    options.mutedParts.append(doc->parts.at(i).name);
                else
                    ++audible;
            }
            const auto plan = buildPlan(*doc, options);
            QCOMPARE(plan.notes.size(), audible * 3);
            for (const auto &note : plan.notes)
                QVERIFY(mask & (1 << note.partIndex));
        }
    }

    void ttbbOverlaysInheritFieldsWithoutMaterializingNotes()
    {
        const auto base = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(base);
        const QList<QByteArray> changes {
            "[parts.Tenor1]\nclef = \"alto\"\n",
            "[parts.Tenor1]\nchoral_type = \"tenor2\"\n",
            "[parts.Tenor1]\nnotes = \"c'2 d'4 | e'2.\"\n",
            "[parts.Tenor1.lyrics.1]\ntext = \"uno dos tres\"\n"
        };
        for (int i = 0; i < changes.size(); ++i) {
            const QByteArray bytes = "title = \"Traducción\"\n" + changes.at(i);
            auto overlay = io::loadBytes("song_es.toml", bytes);
            QVERIFY(overlay);
            const auto merged = mergeOverlay(*base, *overlay);
            QCOMPARE(merged.parts.size(), 4);
            QCOMPARE(merged.part(u"Tenor1")->staffNumber.valueOr(0), 1);
            QCOMPARE(merged.part(u"Tenor1")->notesInherited, i != 2);
            QCOMPARE(countBySeverity(validate(merged), Severity::Error), i == 0 ? 1 : 0);
            QCOMPARE(io::serialize(*overlay), bytes);
            overlay->title.set("Editada");
            QByteArray expected = bytes;
            expected.replace("Traducción", "Editada");
            QCOMPARE(io::serialize(*overlay), expected);
        }
        QCOMPARE(io::serialize(*base), ttbbSong());
    }

    void spliceTargetsMatchNormalizedExactRolesInDeterministicOrder()
    {
        auto doc = io::loadBytes("song.toml", ttbbSong());
        QVERIFY(doc);
        Part &echo = *doc->part(u"Baritone");
        echo.spliceLyricsInto.set(" TeNoR1 ");
        QCOMPARE(analyseSplice(*doc, echo).targetPartName, QStringLiteral("Tenor1"));
        echo.spliceLyricsInto.set("tenor");
        QVERIFY(analyseSplice(*doc, echo).targetPartName.isEmpty());
        echo.suppressVerses.set({2, 3});
        echo.suppressVersesWhen.set({" tenor1 ", "TENOR2"});
        for (const auto &f : validate(*doc))
            QVERIFY(f.rule != "W-SUPPRESS-TARGET");
        echo.spliceLyricsInto.set("tenor1");
        Part repeated = *doc->part(u"Tenor1");
        repeated.name = "Upper10";
        doc->parts.prepend(repeated);
        repeated.name = "Upper2";
        doc->parts.prepend(repeated);
        // Canonical Tenor1 has suffix 1, ahead of 2 and 10, regardless of tables.
        QCOMPARE(analyseSplice(*doc, *doc->part(u"Baritone")).targetPartName, QStringLiteral("Tenor1"));
        QCOMPARE(doc->part(u"Baritone")->lyrics.size(), 0);
    }

    // -- slot counting --------------------------------------------------------

    void slurAndBeamContinuationsTakeNoSlot()
    {
        // A beam is a melisma in this format: the group takes one syllable.
        const NoteStream stream
            = NoteStream::parse(QStringLiteral("c'4 d'8[ e'8] f'4( g'4) | a'4~ a'4 r4 s4"));
        QList<int> slotted;
        int index = 0;
        for (const Measure &measure : stream.measures()) {
            for (const Event &event : measure.events) {
                if (event.slotIndex >= 0)
                    slotted.append(index);
                ++index;
            }
        }
        // c'4, the beam group's first note, the slur group's first note, the
        // first tied note: four slots out of eight events.
        QCOMPARE(slotted, QList<int>({ 0, 1, 3, 5 }));
    }

    void dashedSlursKeepEverySlot()
    {
        const NoteStream stream = NoteStream::parse(QStringLiteral("d'8-( e'8-) f'4 g'4 a'4"));
        int slots = 0;
        for (const Event &event : stream.measures().first().events) {
            if (event.slotIndex >= 0)
                ++slots;
        }
        QCOMPARE(slots, 5);
    }

    void emptyVerseDoesNotShiftTheChorusToSlotZero()
    {
        // Regression ported from importer.rs: an empty verse key is not a verse,
        // so the chorus must still attach at its @c marker.
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), emptyVerse());
        const PartAlignment alignment = alignPart(doc, doc.parts.first());
        QVERIFY(!alignment.hasVerseLyrics);
        QCOMPARE(alignment.sections.size(), 1);
        QVERIFY(alignment.sections.first().isChorus);
        QCOMPARE(alignment.chorusStartSlot.value_or(-1), 4);
        QCOMPARE(alignment.sections.first().slotOffset, 4);
    }

    void chorusFollowsTheLongestVerse()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), chorusAfterVerses());
        const PartAlignment alignment = alignPart(doc, doc.parts.first());
        QCOMPARE(alignment.maxVerseLength, 4);
        for (const AttachedSection &section : alignment.sections)
            QCOMPARE(section.slotOffset, section.isChorus ? 4 : 0);
    }

    void refrainFirstShiftsVersesPastTheChorus()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), refrainFirst());
        const PartAlignment alignment = alignPart(doc, doc.parts.first());
        QVERIFY(alignment.chorusFirst);
        for (const AttachedSection &section : alignment.sections)
            QCOMPARE(section.slotOffset, section.isChorus ? 0 : 4);
    }

    void sharedSectionsExpandWithPerPartOverrides()
    {
        // Ported from importer.rs: a part overriding [lyrics.sN] re-targets the
        // @sN references inside verse texts it inherits.
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), sharedSections());
        const PartAlignment soprano = alignPart(doc, *doc.part(QStringLiteral("Soprano")));
        QCOMPARE(soprano.sections.size(), 1);
        QCOMPARE(soprano.sections.first().syllables,
            QStringList({ "verse", "one", "text", "goes", "call", "line", "here", "tail" }));

        const PartAlignment tenor = alignPart(doc, *doc.part(QStringLiteral("Tenor")));
        QCOMPARE(tenor.sections.first().syllables,
            QStringList({ "verse", "one", "text", "goes", "echo", "line", "here", "tail" }));
        QVERIFY(soprano.errors.isEmpty());
    }

    void missingSharedReferenceIsAnError()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), brokenSharedReference());
        const PartAlignment alignment = alignPart(doc, doc.parts.first());
        QVERIFY(!alignment.errors.isEmpty());
        QVERIFY(alignment.errors.first().contains(QStringLiteral("no [lyrics.s9] section")));
    }

    // -- overlay merging ------------------------------------------------------

    void absentOverlayFieldsAreInherited()
    {
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay = loadFrom(dir, QStringLiteral("song_es.toml"),
            "title = \"En presencia estar de Cristo\"\n");
        const SongDocument merged = mergeOverlay(base, overlay);

        QCOMPARE(*merged.title, QStringLiteral("En presencia estar de Cristo"));
        QCOMPARE(merged.tempoBpm.valueOr(0), 96);
        QCOMPARE(merged.verseCount.valueOr(0), 4);
        QCOMPARE(merged.keySignature.valueOr(QString()), QStringLiteral("Bb"));
        QCOMPARE(merged.subtitle.valueOr(QString()), QStringLiteral("Original"));
        QCOMPARE(merged.parts.size(), 2);
        QCOMPARE(merged.part(QStringLiteral("Soprano"))->notes.valueOr(QString()).trimmed(),
            QStringLiteral("f'1 | g'1"));
    }

    void presentScalarsReplace()
    {
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay = loadFrom(dir, QStringLiteral("song_es.toml"),
            "tempo_bpm = 84\nverse_count = 3\nsubtitle = \"Traducci\xc3\xb3n\"\n");
        const SongDocument merged = mergeOverlay(base, overlay);
        QCOMPARE(merged.tempoBpm.valueOr(0), 84);
        QCOMPARE(merged.verseCount.valueOr(0), 3);
        QCOMPARE(merged.keySignature.valueOr(QString()), QStringLiteral("Bb"));
    }

    void defaultVersesLoadNormalizeAndMerge()
    {
        QTemporaryDir dir;
        const SongDocument implicit = loadFrom(dir, QStringLiteral("implicit.toml"), baseSong());
        QCOMPARE(implicit.effectiveDefaultVerses(), QList<int>({ 1, 2, 3, 4 }));

        QByteArray withDefaults = baseSong();
        withDefaults.replace("verse_count = 4",
            "verse_count = 4\ndefault_verses = [3, 1, 3, 99]");
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), withDefaults);
        QCOMPARE(base.effectiveDefaultVerses(), QList<int>({ 1, 3 }));
        QVERIFY(!base.unknownKeys.contains(QStringLiteral("default_verses")));

        const SongDocument inherited = mergeOverlay(base,
            loadFrom(dir, QStringLiteral("song_es.toml"), "title = \"Heredado\"\n"));
        QCOMPARE(inherited.effectiveDefaultVerses(), QList<int>({ 1, 3 }));

        const SongDocument replaced = mergeOverlay(base,
            loadFrom(dir, QStringLiteral("song_fr.toml"), "default_verses = [2, 4]\n"));
        QCOMPARE(replaced.effectiveDefaultVerses(), QList<int>({ 2, 4 }));
    }

    void emptyOverlayTitleInheritsRatherThanBlanking()
    {
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay
            = loadFrom(dir, QStringLiteral("song_es.toml"), "tempo_bpm = 84\n");
        QCOMPARE(*mergeOverlay(base, overlay).title, QStringLiteral("Face to Face"));
    }

    void partOverrideIsFieldWise()
    {
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay = loadFrom(dir, QStringLiteral("song_es.toml"),
            sopranoNotesOverride());
        const SongDocument merged = mergeOverlay(base, overlay);
        const Part *soprano = merged.part(QStringLiteral("Soprano"));
        QCOMPARE(soprano->notes.valueOr(QString()).trimmed(), QStringLiteral("f'2 f'2 | g'1"));
        QCOMPARE(soprano->choralType.valueOr(QString()), QStringLiteral("soprano"));
        QCOMPARE(soprano->clef.valueOr(QString()), QStringLiteral("treble"));
        QCOMPARE(soprano->staffNumber.valueOr(0), 1);
        QVERIFY(!soprano->notesInherited);
        // A part the overlay never mentions is inherited whole.
        QCOMPARE(merged.part(QStringLiteral("Alto"))->notes.valueOr(QString()).trimmed(),
            QStringLiteral("d'1 | ees'1"));
        QVERIFY(merged.part(QStringLiteral("Alto"))->notesInherited);
    }

    void anyLyricEntryReplacesTheWholeMap()
    {
        // Deliberately not a per-verse merge: supplying only verse 1 must not
        // leave the base language's verse 2 on a translated page.
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay = loadFrom(dir, QStringLiteral("song_es.toml"),
            "[lyrics.1]\ntext = \"uno dos\"\n");
        const SongDocument merged = mergeOverlay(base, overlay);
        QCOMPARE(merged.lyrics.size(), 1);
        QCOMPARE(merged.lyrics.value(QStringLiteral("1")).rawText, QStringLiteral("uno dos"));
        QVERIFY(!merged.lyrics.contains(QStringLiteral("2")));
    }

    void editingOneVerseOfATranslationKeepsTheOthers()
    {
        // The overlay lyric map replaces wholesale. Typing in verse 2 of a
        // translation that has no lyrics of its own must not delete verses 1,
        // 3, and 4 — which is what writing only the edited key would do.
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        SongDocument overlay
            = loadFrom(dir, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        QVERIFY(overlay.isOverlay);
        QVERIFY(overlay.lyrics.isEmpty());

        const SongDocument merged = mergeOverlay(base, overlay);
        materialiseOverlayLyrics(overlay, merged, QString());
        SongDocument::setLyric(overlay.lyrics, QStringLiteral("2"), QStringLiteral("tres cuatro"));

        QCOMPARE(overlay.lyrics.size(), base.lyrics.size());
        QCOMPARE(overlay.lyrics.value(QStringLiteral("1")).rawText, QStringLiteral("one two"));
        QCOMPARE(overlay.lyrics.value(QStringLiteral("2")).rawText, QStringLiteral("tres cuatro"));

        const SongDocument remerged = mergeOverlay(base, overlay);
        QCOMPARE(remerged.lyrics.size(), 2);
        QCOMPARE(remerged.lyrics.value(QStringLiteral("1")).rawText, QStringLiteral("one two"));
    }

    void materialisingIsANoOpOnABaseSongOrAnAlreadyDefinedMap()
    {
        QTemporaryDir dir;
        SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument other = base;
        materialiseOverlayLyrics(base, other, QString());
        QCOMPARE(base.lyrics.size(), 2);
        QVERIFY(!base.isDirty());

        SongDocument overlay = loadFrom(dir, QStringLiteral("song_es.toml"),
            "title = \"Cara a cara\"\n\n[lyrics.1]\ntext = \"uno dos\"\n");
        materialiseOverlayLyrics(overlay, mergeOverlay(base, overlay), QString());
        QCOMPARE(overlay.lyrics.size(), 1);
        QVERIFY(!overlay.isDirty());
    }

    // -- saving ---------------------------------------------------------------

    void savingAnUntouchedDocumentIsByteIdentical()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        QCOMPARE(io::serialize(doc), baseSong());
    }

    void editingOneFieldTouchesOnlyThatLine()
    {
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        doc.tempoBpm.set(120);
        const QByteArray written = io::serialize(doc);

        const QStringList before = QString::fromUtf8(baseSong()).split(u'\n');
        const QStringList after = QString::fromUtf8(written).split(u'\n');
        QCOMPARE(after.size(), before.size());
        int changed = 0;
        for (qsizetype i = 0; i < before.size(); ++i) {
            if (before.at(i) != after.at(i))
                ++changed;
        }
        QCOMPARE(changed, 1);
        QVERIFY(written.contains("tempo_bpm = 120"));
    }

    void appendingACopyrightLineKeepsTheArrayStyle()
    {
        // Song 204's shape: a multi-line copyrights array gaining the OpenPsalm
        // arrangement line at the end.
        QTemporaryDir dir;
        SongDocument doc
            = loadFrom(dir, QStringLiteral("song.toml"), fixtures::partsAndMultilineStrings());
        doc.copyrights.set({ QStringLiteral("one"), QStringLiteral("two"),
            QStringLiteral(
                "Arrangement by OpenPsalm, 2026 and released under the CC-BY 4.0 license") });
        const QByteArray written = io::serialize(doc);
        QVERIFY(written.contains("CC-BY 4.0"));
        QVERIFY(written.contains("[parts.Soprano]"));

        const SongDocument reloaded
            = loadFrom(dir, QStringLiteral("again.toml"), written);
        QCOMPARE(reloaded.copyrights.valueOr({}).size(), 3);
        QCOMPARE(reloaded.copyrights.valueOr({}).at(2),
            QStringLiteral(
                "Arrangement by OpenPsalm, 2026 and released under the CC-BY 4.0 license"));
    }

    void addingAKeyInsertsItBeforeTheFirstTable()
    {
        // Every root key must precede the first [table] header, or TOML would
        // assign it to that table.
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        doc.commentary.set(QStringLiteral("<p>Notes.</p>"));
        const QByteArray written = io::serialize(doc);
        QVERIFY(written.indexOf("commentary") < written.indexOf("[parts.Soprano]"));

        const auto reparsed = toml::parse(written);
        QVERIFY(reparsed.has_value());
        QCOMPARE(reparsed->rootPair(QStringLiteral("commentary"))->value.string,
            QStringLiteral("<p>Notes.</p>"));
    }

    void newBreakLanesStayTogetherEvenInAShuffledHeader()
    {
        auto doc = io::loadBytes("song.toml", baseSong());
        QVERIFY(doc);
        doc->optionalPhraseBreaks.set({ { 1, 32 } });
        doc->nonBreakingPhraseBreaks.set({ { 1, 48 } });
        doc->commentary.set("Preserve this commentary");
        const QByteArray bytes = io::serialize(*doc);
        QVERIFY(bytes.contains("phrase_breaks = [\"2:64\"]\n"
            "optional_phrase_breaks = [\"1:32\"]\n"
            "non_breaking_phrase_breaks = [\"1:48\"]\n"));
        const auto again = io::loadBytes("song.toml", bytes);
        QVERIFY(again);
        QCOMPARE(again->optionalPhraseBreaks.valueOr({}), QList<PhraseBreak>({ { 1, 32 } }));
        QVERIFY(bytes.indexOf("commentary =") < bytes.indexOf("[parts."));
    }

    void generatedFieldsAndSectionsUseCanonicalOrder()
    {
        auto doc = io::loadBytes("song.toml", "title = \"T\"\n[parts.Soprano]\nnotes = \"c'1\"\n"
            "[lyrics.10]\ntext = \"ten\"\n");
        QVERIFY(doc);
        doc->active.set(false);
        doc->copyrights.set({ "Public domain" });
        doc->keySignature.set("C");
        doc->phraseBreaks.set({ { 1, 64 } });
        doc->optionalPhraseBreaks.set({ { 1, 32 } });
        doc->part(u"Soprano")->clef.set("treble");
        doc->part(u"Soprano")->choralType.set("soprano");
        SongDocument::setLyric(doc->lyrics, "2", "two");
        SongDocument::setLyric(doc->lyrics, "1", "one");
        SongDocument::setLyric(doc->lyrics, "s10", "template ten");
        SongDocument::setLyric(doc->lyrics, "s2", "template two");
        doc->timeSigChanges.set({ { 1, 2, 2, 1 } });
        const QByteArray bytes = io::serialize(*doc);
        QVERIFY(toml::parse(bytes));
        QVERIFY(bytes.indexOf("active =") < bytes.indexOf("copyrights ="));
        QVERIFY(bytes.indexOf("copyrights =") < bytes.indexOf("key_signature ="));
        QVERIFY(bytes.indexOf("phrase_breaks =") < bytes.indexOf("optional_phrase_breaks ="));
        QVERIFY(bytes.indexOf("[[time_sig_changes]]") < bytes.indexOf("[parts.Soprano]"));
        QVERIFY(bytes.indexOf("choral_type =") < bytes.indexOf("clef ="));
        QVERIFY(bytes.indexOf("clef =") < bytes.indexOf("notes ="));
        QVERIFY(bytes.indexOf("[lyrics.1]") < bytes.indexOf("[lyrics.2]"));
        QVERIFY(bytes.indexOf("[lyrics.2]") < bytes.indexOf("[lyrics.10]"));
        QVERIFY(bytes.indexOf("[lyrics.s2]") < bytes.indexOf("[lyrics.s10]"));
        const auto standard = io::standardize(*doc);
        QVERIFY(standard);
        const auto reloaded = io::loadBytes("song.toml", *standard);
        QVERIFY(reloaded);
        QCOMPARE(io::serializeFresh(*doc), io::serializeFresh(*reloaded));
    }

    void standardizationPreservesCommentsValuesAndArrayOwnership()
    {
        const QByteArray source = R"TOML(# File preamble
optional_phrase_breaks = ["1:32"] # optional lane
future = { a = 1, b = "keep" }
# Title comment
title  = 'Keep spacing'
commentary = '''first
second'''
# Required lane
phrase_breaks = [
  "1:64", # end
]

# Bass comment
[parts.Bass]
notes = '''c,1'''
clef = 'bass'
choral_type = 'bass'
future_date = 2026-09-25

[lyrics.10]
text = 'ten'
[parts."Lead Voice".lyrics.2]
text = 'override'
[lyrics.2]
text = 'two'
# Lead comment
[parts."Lead Voice"] # header comment
notes = '''c'1'''
choral_type = 'soprano'

[[extensions]]
id = 'first'
[extensions.detail]
value = 1
[[time_sig_changes]]
duration = 1
denominator = 4
numerator = 4
measure = 1
[time_sig_changes.extra]
note = 'keep this with the first change'
[[extensions]]
id = 'second'
[extensions.detail]
value = 2
[[time_sig_changes]]
measure = 2
numerator = 3
denominator = 4
duration = 1
# File footer
)TOML";
        const auto doc = io::loadBytes("song.toml", source);
        QVERIFY(doc);
        const auto standard = io::standardize(*doc);
        QVERIFY(standard);
        QVERIFY(standard->startsWith("# File preamble\n# Title comment\ntitle  = 'Keep spacing'"));
        QVERIFY(standard->contains("# Required lane\nphrase_breaks = [\n  \"1:64\", # end\n]\n"
            "optional_phrase_breaks = [\"1:32\"] # optional lane\n"));
        QVERIFY(standard->contains("future = { a = 1, b = \"keep\" }"));
        QVERIFY(standard->contains("commentary = '''first\nsecond'''"));
        QVERIFY(standard->contains("# Lead comment\n[parts.\"Lead Voice\"] # header comment\n"
            "choral_type = 'soprano'\nnotes = '''c'1'''"));
        QVERIFY(standard->contains("future_date = 2026-09-25"));
        QVERIFY(standard->indexOf("[parts.\"Lead Voice\"]") < standard->indexOf("[parts.Bass]"));
        QVERIFY(standard->indexOf("[parts.\"Lead Voice\".lyrics.2]") < standard->indexOf("[parts.Bass]"));
        QVERIFY(standard->indexOf("[lyrics.2]") < standard->indexOf("[lyrics.10]"));
        QVERIFY(standard->contains("id = 'first'\n\n[extensions.detail]\nvalue = 1\n\n"
            "[[extensions]]\nid = 'second'\n\n[extensions.detail]\nvalue = 2"));
        QVERIFY(standard->contains("note = 'keep this with the first change'\n\n[[time_sig_changes]]\nmeasure = 2"));
        QVERIFY(standard->endsWith("# File footer\n"));
        const auto reloaded = io::loadBytes("song.toml", *standard);
        QVERIFY(reloaded);
        QCOMPARE(*io::standardize(*reloaded), *standard);
        QCOMPARE(reloaded->part(u"Lead Voice")->notes.opt(), doc->part(u"Lead Voice")->notes.opt());
        QCOMPARE(reloaded->phraseBreaks.opt(), doc->phraseBreaks.opt());
        QCOMPARE(reloaded->timeSigChanges.opt(), doc->timeSigChanges.opt());
        QCOMPARE(io::serialize(*doc), source); // Normal saves retain authored order.
    }

    void standardizationAndInsertionsHandleMissingNewlinesAndBom()
    {
        auto doc = io::loadBytes("song.toml", "title = 'T'");
        QVERIFY(doc);
        doc->optionalPhraseBreaks.set({ { 1, 16 } });
        QVERIFY(io::loadBytes("song.toml", io::serialize(*doc)));
        const QByteArray bytes = QByteArray("\xEF\xBB\xBF")
            + "tempo_bpm = 90\r\ntitle = 'T'\r\n[parts.Lead]\r\nnotes = 'c1'";
        doc = io::loadBytes("song.toml", bytes);
        QVERIFY(doc);
        const auto standard = io::standardize(*doc);
        QVERIFY(standard);
        QVERIFY(standard->startsWith(QByteArray("\xEF\xBB\xBF") + "title = 'T'\r\ntempo_bpm = 90"));
        doc = io::loadBytes("song.toml", *standard);
        QVERIFY(doc);
        QCOMPARE(*io::standardize(*doc), *standard);
    }

    void defaultVersesRoundTripAndInsertBeforeTables()
    {
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        doc.defaultVerses.set({ 1, 3 });
        const QByteArray written = io::serialize(doc);
        QVERIFY(written.contains("default_verses = [1, 3]"));
        QVERIFY(written.indexOf("default_verses") < written.indexOf("[parts.Soprano]"));

        const SongDocument reloaded = loadFrom(dir, QStringLiteral("again.toml"), written);
        QCOMPARE(reloaded.defaultVerses.valueOr({}), QList<int>({ 1, 3 }));
        QCOMPARE(io::serialize(reloaded), written);
    }

    void editedNotesKeepTheOtherPartsUntouched()
    {
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        Part *soprano = doc.part(QStringLiteral("Soprano"));
        soprano->stream.measures()[0].events[0].pitches[0].octave = 5;
        soprano->stream.measures()[0].events[0].dirty = true;
        const QByteArray written = io::serialize(doc);
        QVERIFY(written.contains("f''1"));
        QVERIFY(written.contains("d'1 | ees'1"));  // the alto is untouched
    }

    void aPerPartOverrideIsWrittenBesideItsPart()
    {
        // The editor's "give one voice its own text" button. A new table has to
        // land next to the part it belongs to, not at the bottom of the file:
        // these diffs are read by people.
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        Part *alto = doc.part(QStringLiteral("Alto"));
        SongDocument::setLyric(alto->lyrics, QStringLiteral("1"), QStringLiteral("one two more"));
        const QByteArray written = io::serialize(doc);

        QVERIFY(written.contains("[parts.Alto.lyrics.1]"));
        QVERIFY(written.indexOf("[parts.Alto.lyrics.1]") > written.indexOf("[parts.Alto]"));
        QVERIFY(written.indexOf("[parts.Alto.lyrics.1]") < written.indexOf("[lyrics.1]"));

        const auto reparsed = toml::parse(written);
        QVERIFY(reparsed.has_value());
        const toml::Table *table = reparsed->table({ QStringLiteral("parts"),
            QStringLiteral("Alto"), QStringLiteral("lyrics"), QStringLiteral("1") });
        QVERIFY(table);
        QCOMPARE(table->find(QStringLiteral("text"))->value.string,
            QStringLiteral("one two more"));
        // Nothing else moved.
        QVERIFY(written.contains("[lyrics.2]\ntext = \"three four\""));
    }

    void removingAPerPartOverrideRestoresTheOriginalBytes()
    {
        // Adding an override and taking it away again must leave the file
        // exactly as it was; otherwise "revert to song default" is a trap.
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        Part *alto = doc.part(QStringLiteral("Alto"));
        SongDocument::setLyric(alto->lyrics, QStringLiteral("1"), QStringLiteral("one two more"));
        const QByteArray withOverride = io::serialize(doc);

        // Round-trip through the file, as saving does, then delete the block.
        SongDocument reloaded = loadFrom(dir, QStringLiteral("song2.toml"), withOverride);
        Part *reloadedAlto = reloaded.part(QStringLiteral("Alto"));
        QVERIFY(reloadedAlto->lyrics.contains(QStringLiteral("1")));
        reloaded.removePartLyric(*reloadedAlto, QStringLiteral("1"));
        QVERIFY(reloaded.isDirty());
        QCOMPARE(io::serialize(reloaded), baseSong());
    }

    void deletingAGlobalSectionTakesItsWholeTable()
    {
        QTemporaryDir dir;
        SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        doc.removeGlobalLyric(QStringLiteral("2"));
        const QByteArray written = io::serialize(doc);
        QVERIFY(!written.contains("[lyrics.2]"));
        QVERIFY(!written.contains("three four"));
        QVERIFY(written.contains("[lyrics.1]\ntext = \"one two\""));
        QVERIFY(toml::parse(written).has_value());
    }

    void aMergedViewIsNeverWritten()
    {
        // Its inherited parts carry spans into the base file, so splicing the
        // overlay's bytes at those offsets would corrupt the file.
        QTemporaryDir dir;
        const SongDocument base = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        const SongDocument overlay
            = loadFrom(dir, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        const SongDocument merged = mergeOverlay(base, overlay);
        QVERIFY(merged.isMergedView);
        QCOMPARE(io::serialize(merged), merged.originalBytes);
    }

    // -- playback -------------------------------------------------------------

    void playbackTimingFollowsTheTempo()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), timing());
        const PlaybackPlan plan = buildPlan(doc);
        // Two 4/4 measures at 120 bpm is eight beats, four seconds.
        QCOMPARE(plan.notes.size(), 5);
        QVERIFY(qAbs(plan.totalSeconds - 4.0) < 0.01);
        QVERIFY(qAbs(plan.notes.at(1).startSeconds - 0.5) < 0.01);
        QVERIFY(qAbs(plan.notes.at(4).endSeconds - 4.0) < 0.01);
    }

    void tiedNotesBecomeOneSound()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), tied());
        const PlaybackPlan plan = buildPlan(doc);
        QCOMPARE(plan.notes.size(), 1);
        QVERIFY(qAbs(plan.notes.first().endSeconds - 4.0) < 0.01);
    }

    void dynamicsSetVelocityLikeTheMidiExport()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), dynamics());
        const PlaybackPlan plan = buildPlan(doc);
        QCOMPARE(plan.notes.at(0).velocity, 50);
        QCOMPARE(plan.notes.at(1).velocity, 50);  // carries forward
        QCOMPARE(plan.notes.at(2).velocity, 96);
    }

    void mutedPartsAreSilent()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), baseSong());
        PlaybackOptions options;
        options.mutedParts = { QStringLiteral("Alto") };
        const PlaybackPlan plan = buildPlan(doc, options);
        for (const PlaybackNote &note : plan.notes)
            QVERIFY(doc.parts.at(note.partIndex).name != QLatin1String("Alto"));
    }

    // -- validation -----------------------------------------------------------

    void measureMismatchIsReportedLikeTheSeeder()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), shortMeasure());
        const QList<Finding> findings = validate(doc);
        const auto measure = std::find_if(findings.begin(), findings.end(),
            [](const Finding &finding) { return finding.rule == QLatin1String("E-MEASURE"); });
        QVERIFY(measure != findings.end());
        QVERIFY(measure->message.contains(QStringLiteral("expected 4/4 (192 ticks), got 144")));
    }

    void timeSignatureChangesAreHonoured()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), timeSignatureChange());
        QCOMPARE(doc.timeSigForMeasure(2), std::make_pair(3, 4));
        QCOMPARE(doc.timeSigForMeasure(3), std::make_pair(4, 4));
        const QList<Finding> findings = validate(doc);
        QCOMPARE(countBySeverity(findings, Severity::Error), 0);
    }

    void removingAllTimeSignatureChangesRemovesTheTables()
    {
        QTemporaryDir dir;
        SongDocument doc
            = loadFrom(dir, QStringLiteral("song.toml"), timeSignatureChange());
        doc.timeSigChanges.clear();
        const QByteArray written = io::serialize(doc);
        QVERIFY(!written.contains("[[time_sig_changes]]"));
        QVERIFY(written.contains("[parts.Soprano]"));
        QVERIFY(toml::parse(written).has_value());
    }

    void unsupportedTimeSignatureDenominatorsAreErrors()
    {
        QTemporaryDir dir;
        QByteArray source = baseSong();
        source.prepend("time_sig_numerator = 4\ntime_sig_denominator = 3\n");
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), source);
        const QList<Finding> findings = validate(doc);
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("E-METRE")
                && finding.message.contains(QStringLiteral("denominator 3"));
        }));
    }

    void accentsAndMarcatosAreRequiredOnEverySoundingVoice()
    {
        QTemporaryDir dir;
        QByteArray source = baseSong();
        source.replace("f'1 | g'1", "f'1^ | g'1");
        source.replace("d'1 | ees'1", "d'1^^ | ees'1");
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), source);
        const QList<Finding> findings = validate(doc);
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("R6.1-accent")
                && finding.message.contains(QStringLiteral("Alto"));
        }));
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("R6.1-marcato")
                && finding.message.contains(QStringLiteral("Soprano"));
        }));
    }

    void nonPositiveTempoAndVerseCountAreErrors()
    {
        QTemporaryDir dir;
        QByteArray source = baseSong();
        source.replace("tempo_bpm = 96", "tempo_bpm = 0");
        source.replace("verse_count = 4", "verse_count = -1");
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), source);
        const QList<Finding> findings = validate(doc);
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("E-TEMPO");
        }));
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("E-VERSE-COUNT");
        }));
    }

    void overlappingTimeSignatureChangesAreErrors()
    {
        QTemporaryDir dir;
        QByteArray source = baseSong();
        source.prepend(R"TOML([[time_sig_changes]]
measure = 1
numerator = 4
denominator = 4
duration = 2

[[time_sig_changes]]
measure = 2
numerator = 3
denominator = 4
duration = 1

)TOML");
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), source);
        const QList<Finding> findings = validate(doc);
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("E-METRE-OVERLAP");
        }));
    }

    void truncatedVerseIsAnError()
    {
        QTemporaryDir dir;
        const SongDocument doc = loadFrom(dir, QStringLiteral("song.toml"), overlongVerse());
        const QList<Finding> findings = validate(doc);
        QVERIFY(std::any_of(findings.begin(), findings.end(), [](const Finding &finding) {
            return finding.rule == QLatin1String("E-SLOTS");
        }));
    }
};

QTEST_APPLESS_MAIN(DocumentTests)
#include "DocumentTests.moc"
