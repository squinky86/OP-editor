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
