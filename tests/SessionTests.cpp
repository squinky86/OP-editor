// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The session's shared editing operations — the ones more than one view calls,
// where a disagreement between the score and the lyrics grid would show up as
// two different files depending on where the user clicked.

#include "Fixtures.h"

#include "app/Session.h"

#include <QTemporaryDir>
#include <QTest>

using namespace ope;
using namespace ope::fixtures;

namespace {

void write(const QDir &dir, const QString &name, const QByteArray &contents)
{
    QFile file(dir.filePath(name));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    file.write(contents);
}

} // namespace

class SessionTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fixingShortMelismas_data()
    {
        QTest::addColumn<QString>("notes");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("count");
        QTest::newRow("eighths") << "c8( d8)" << "c8[ d8]" << 1;
        QTest::newRow("dotted-eighth") << "c8.( d16)" << "c8.[ d16]" << 1;
        QTest::newRow("several") << "c16( d32 e32) f8( g8)" << "c16[ d32 e32] f8[ g8]" << 2;
        QTest::newRow("chords") << "<c e>8( <d f>8)" << "<c e>8[ <d f>8]" << 1;
        QTest::newRow("tuplets") << "{3 c8( d8 e8)}" << "{3 c8[ d8 e8] }" << 1;
        QTest::newRow("long-note") << "c4( d8)" << "c4( d8)" << 0;
        QTest::newRow("double-dot-too-long") << "c8..( d32)" << "c8..( d32)" << 0;
        QTest::newRow("quarter-triplet") << "{3 c4( d4 e4)}" << "{3 c4( d4 e4) }" << 0;
        QTest::newRow("same-pitch-needs-tie") << "c8( c8)" << "c8( c8)" << 0;
        QTest::newRow("already-beamed") << "c8[ d8]" << "c8[ d8]" << 0;
        QTest::newRow("cross-bar") << "c8( | d8)" << "c8( | d8)" << 0;
        QTest::newRow("unclosed") << "c8( d8" << "c8( d8" << 0;
        QTest::newRow("nested") << "c8( d8( e8) f8)" << "c8( d8( e8) f8)" << 0;
        QTest::newRow("stray-close") << "c8) d8( e8)" << "c8) d8( e8)" << 0;
        QTest::newRow("rest") << "c8( r8 d8)" << "c8( r8 d8)" << 0;
        QTest::newRow("spacer") << "c8( s8 d8)" << "c8( s8 d8)" << 0;
        QTest::newRow("overlapping-beam") << "c8[ d8( e8) f8]" << "c8[ d8( e8) f8]" << 0;
        QTest::newRow("dashed-slur") << "c8-( d8-)" << "c8-( d8-)" << 0;
        QTest::newRow("overlapping-dashed-slur") << "c8-( d8( e8) f8-)" << "c8-( d8( e8) f8-)" << 0;
        QTest::newRow("mixed-length-groups") << "c4( d4) e8( f8)" << "c4( d4) e8[ f8]" << 1;
        QTest::newRow("invalid-token") << "c8( d8) e5" << "c8( d8) e5" << 0;
    }

    void fixingShortMelismas()
    {
        QFETCH(QString, notes);
        QFETCH(QString, expected);
        QFETCH(int, count);
        const QByteArray original = "title = 'Melismas'\n[parts.Lead]\nnotes = '''"
            + notes.toUtf8() + "'''\n";
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", original);
        Session session;
        QCOMPARE(session.fixShortMelismas(), 0);
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        QCOMPARE(session.fixShortMelismas(), count);
        QCOMPARE(session.document().parts.first().stream.toSource(), expected);
        QCOMPARE(session.undoStack()->count(), count > 0 ? 1 : 0);
        const auto after = session.currentBytes();
        QCOMPARE(session.fixShortMelismas(), 0);
        QCOMPARE(session.currentBytes(), after);
        if (count > 0) {
            session.undoStack()->undo();
            QCOMPARE(session.currentBytes(), original);
            session.undoStack()->redo();
            QCOMPARE(session.currentBytes(), after);
        } else {
            QCOMPARE(after, original);
        }
    }

    void fixingAllVoicesPreservesMusicLyricsAndSave()
    {
        QTemporaryDir dir;
        const QByteArray original = R"TOML(title = 'Melismas'
# Keep this comment and unfamiliar field.
custom = 'unchanged'
[parts.Lead]
notes = '''c8(%p d8) e4 f2 | {3 g8( a8 b8)} c'2. |
c8.( d16) e4 f2'''
[parts.Bass]
notes = 'c,8( d,8) e,4 f,2'
[lyrics.1]
text = 'one two three four five six seven eight'
)TOML";
        write(QDir(dir.path()), "song.toml", original);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        const SongDocument before = session.document();
        QCOMPARE(session.fixShortMelismas(), 4);
        QCOMPARE(session.undoStack()->count(), 1);
        const QByteArray after = session.currentBytes();
        QVERIFY(after.contains("# Keep this comment and unfamiliar field.\ncustom = 'unchanged'"));
        const auto parsed = io::loadBytes("song.toml", after);
        QVERIFY(parsed);
        for (const Part &part : before.parts) {
            const Part *updated = parsed->part(part.name);
            QVERIFY(updated);
            QCOMPARE(updated->stream.lineLayout(), part.stream.lineLayout());
            for (int m = 0; m < part.stream.measureCount(); ++m) {
                const auto &events = part.stream.measures().at(m).events;
                const auto &newEvents = updated->stream.measures().at(m).events;
                QCOMPARE(newEvents.size(), events.size());
                for (int e = 0; e < events.size(); ++e) {
                    const Event &old = events.at(e);
                    Event restored = newEvents.at(e);
                    QCOMPARE(restored.slotIndex, old.slotIndex);
                    QCOMPARE(restored.playedTicks(), old.playedTicks());
                    restored.slurStart = old.slurStart;
                    restored.slurEnd = old.slurEnd;
                    restored.beamStart = old.beamStart;
                    restored.beamEnd = old.beamEnd;
                    QCOMPARE(restored.toSource(), old.toSource());
                }
            }
        }
        QCOMPARE(parsed->lyrics.value("1").rawText, before.lyrics.value("1").rawText);
        for (const Finding &finding : session.findings())
            QVERIFY(finding.rule != "R1.1");
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), original);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), after);
        QVERIFY(session.save());
        QVERIFY(!session.isDirty());
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        QCOMPARE(session.currentBytes(), after);
    }

    void fixingTranslationOnlyOverridesChangedNotes()
    {
        QTemporaryDir dir;
        const QByteArray base = "title = 'Base'\n[parts.Lead]\nclef = 'treble'\nnotes = 'c8( d8)'\n"
            "[parts.Bass]\nnotes = 'c1'\n";
        const QByteArray overlay = "title = 'Translation'\n";
        write(QDir(dir.path()), "song.toml", base);
        write(QDir(dir.path()), "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song_es.toml")));
        QCOMPARE(session.fixShortMelismas(), 1);
        QCOMPARE(session.document().parts.size(), 1);
        QCOMPARE(session.document().parts.first().name, QString("Lead"));
        QCOMPARE(session.document().parts.first().stream.toSource(), QString("c8[ d8]"));
        QVERIFY(!session.currentBytes().contains("clef"));
        QCOMPARE(io::serialize(*session.baseDocument()), base);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
    }

    void transposeSpellsIntervalsAndOctaves_data()
    {
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::addColumn<QString>("note");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("direction");
        const auto row = [](const char *name, QString from, QString to, QString note,
                             QString expected, int direction = 0) {
            QTest::newRow(name) << from << to << note << expected << direction;
        };
        row("flat-to-natural", "Bb", "C", "bes", "c'");
        row("down-across-c", "C", "B", "c'", "b");
        row("up-instead", "C", "B", "c'", "b'", 1);
        row("down-instead", "Bb", "C", "bes", "c", 2);
        row("sharp-to-flat", "C#", "Db", "cis'", "des'");
        row("b-to-c-flat", "B", "Cb", "b", "ces'");
        row("c-flat-to-b", "Cb", "B", "ces'", "b");
        row("minor", "Am", "Cm", "a", "c'");
        row("chromatic-spelling", "C", "D", "ees'", "f'");
        row("double-accidental", "C", "D", "fisis'", "gisis'");
        row("triple-respelled", "E", "F#", "bisis'", "dis''");
    }

    void transposeSpellsIntervalsAndOctaves()
    {
        QFETCH(QString, from);
        QFETCH(QString, to);
        QFETCH(QString, note);
        QFETCH(QString, expected);
        QFETCH(int, direction);
        const auto doc = io::loadBytes("song.toml", "title = 'T'\nkey_signature = '" + from.toUtf8()
            + "'\n[parts.Lead]\nnotes = \"" + note.toUtf8() + "1\"\n");
        QVERIFY(doc);
        const auto result = transposeSong(*doc, *doc, to, static_cast<TransposeDirection>(direction));
        QVERIFY2(result, result ? "" : qPrintable(result.error()));
        QCOMPARE(result->parts.first().stream.measures().first().events.first().pitches.first().toToken(), expected);
        const auto reloaded = io::loadBytes("song.toml", io::serialize(*result));
        QVERIFY(reloaded);
        QCOMPARE(reloaded->keySignature.valueOr({}), to);
        QCOMPARE(reloaded->parts.first().stream.measures().first().events.first().pitches.first().toToken(), expected);
    }

    void transposingEveryVoicePreservesRhythmMarkingsAndUndo()
    {
        QTemporaryDir dir;
        const QByteArray original = R"TOML(title = 'Transpose fixture'
key_signature = 'Bb'
phrase_breaks = ['2:64']
[parts.Lead]
notes = '''{3 bes8[(%p\< c'8 d'8])\!} <ees' g'>4~\rit <ees' g'>4\spanend r4 |
s2 f'4!@c-.^^/2 bes4@e'''
[parts.Bass]
notes = 'bes,1 | ees1'
[lyrics.1]
text = 'one two three four'
)TOML";
        write(QDir(dir.path()), "song.toml", original);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        const SongDocument before = session.document();
        const auto result = session.transposeTo("C");
        QVERIFY2(result, result ? "" : qPrintable(result.error()));
        const QByteArray transposed = session.currentBytes();
        const auto parsed = io::loadBytes("song.toml", transposed);
        QVERIFY2(parsed, parsed ? "" : qPrintable(parsed.error().formatted()));
        for (const Part &part : before.parts) {
            const Part *after = parsed->part(part.name);
            QVERIFY(after);
            QCOMPARE(after->stream.lineLayout(), part.stream.lineLayout());
            QCOMPARE(after->stream.measureCount(), part.stream.measureCount());
            for (int m = 0; m < part.stream.measureCount(); ++m) {
                const auto &events = part.stream.measures().at(m).events;
                const auto &newEvents = after->stream.measures().at(m).events;
                QCOMPARE(newEvents.size(), events.size());
                for (int e = 0; e < events.size(); ++e) {
                    Event restored = newEvents.at(e);
                    const Event &old = events.at(e);
                    QCOMPARE(restored.playedTicks(), old.playedTicks());
                    QCOMPARE(restored.slotIndex, old.slotIndex);
                    QCOMPARE(restored.pitches.size(), old.pitches.size());
                    for (int p = 0; p < old.pitches.size(); ++p)
                        QCOMPARE(restored.pitches.at(p).midiNote(), old.pitches.at(p).midiNote() + 2);
                    restored.pitches = old.pitches;
                    QCOMPARE(restored.toSource(), old.toSource());
                    QCOMPARE(restored.tuplet.has_value(), old.tuplet.has_value());
                }
            }
        }
        QCOMPARE(parsed->lyrics.value("1").rawText, before.lyrics.value("1").rawText);
        QCOMPARE(parsed->phraseBreaks.opt(), before.phraseBreaks.opt());
        QCOMPARE(session.undoStack()->count(), 1);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), original);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), transposed);
        QVERIFY(session.save());
        QCOMPARE(session.currentBytes(), transposed);
        QVERIFY(!session.isDirty());
    }

    void transposingTranslationMaterializesOnlyNotesAndKey()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, "song.toml", ttbbSong());
        const QByteArray overlay = "title = 'Traducción'\n[parts.Tenor1]\nnotes = 'bes2 d4 | bes2.'\n";
        write(root, "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        QVERIFY(session.transposeTo("C"));
        const auto bytes = session.currentBytes();
        QVERIFY(bytes.contains("key_signature = \"C\""));
        QVERIFY(!bytes.contains("clef ="));
        QVERIFY(!bytes.contains("choral_type ="));
        QVERIFY(!bytes.contains("phrase_breaks ="));
        QVERIFY(!bytes.contains("[lyrics."));
        QCOMPARE(session.document().parts.size(), 4);
        QCOMPARE(io::serialize(*session.baseDocument()), ttbbSong());
        QCOMPARE(session.effectiveDocument().part(u"Tenor1")->stream.measures()[0].events[1].pitches[0].toToken(), QString("e"));
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
        session.undoStack()->redo();
        QVERIFY(session.save());
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        QCOMPARE(session.currentBytes(), bytes);
        QCOMPARE(io::serialize(*session.baseDocument()), ttbbSong());
    }

    void unsafeOrNoopTranspositionDoesNotChangeTheDocument()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        QVERIFY(session.transposeTo("Bb"));
        QCOMPARE(session.undoStack()->count(), 0);
        QVERIFY(!session.transposeTo("Cm"));
        QVERIFY(!session.transposeTo("unknown"));
        QCOMPARE(session.currentBytes(), ttbbSong());
        session.mutate("Invalid notation", [](SongDocument &doc) {
            doc.part(u"Bass")->notes.set("c5");
        });
        const auto invalid = session.currentBytes();
        QVERIFY(!session.transposeTo("C"));
        QCOMPARE(session.currentBytes(), invalid);
    }

    void standardizationIsOneUndoableEditAndKeepsDiskBaseline()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        session.mutate("Add optional break", [](SongDocument &doc) {
            doc.optionalPhraseBreaks.set({ { 1, 24 } });
        });
        const QByteArray before = session.currentBytes();
        QVERIFY(session.standardizeToml());
        const QByteArray standard = session.currentBytes();
        QVERIFY(standard != before);
        QCOMPARE(session.undoStack()->undoText(), QString("Standardize TOML"));
        QVERIFY(session.isDirty());
        QCOMPARE(session.diskBytes(), ttbbSong());
        QVERIFY(session.standardizeToml());
        QCOMPARE(session.undoStack()->count(), 2);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), before);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), standard);
        session.mutate("Edit after ordering", [](SongDocument &doc) { doc.tempoBpm.set(120); });
        QVERIFY(session.save());
        QCOMPARE(session.document().tempoBpm.valueOr(0), 120);
        QVERIFY(session.currentBytes().contains("phrase_breaks = [\"1:48\"]\noptional_phrase_breaks = [\"1:24\"]"));
    }

    void inheritedTtbbLyricsCreateOnlyTheRequiredOverlayMap()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QByteArray base = ttbbSong() + "\n[parts.Tenor1.lyrics.1]\ntext = \"one two three\"\n"
            "[parts.Tenor1.lyrics.2]\ntext = \"four five six\"\n"
            "[parts.Tenor1.lyrics.3]\ntext = \"sing a song\"\n";
        write(root, "song.toml", base);
        const QByteArray overlay = "title = \"Traducción\"\n";
        write(root, "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        const SongDocument merged = session.effectiveDocument();
        session.mutate("Edit inherited lyrics", [&](SongDocument &doc) {
            materialiseOverlayLyrics(doc, merged, "Tenor1");
            SongDocument::setLyric(doc.part(u"Tenor1")->lyrics, "1", "uno dos tres");
        });
        const QByteArray edited = session.currentBytes();
        QVERIFY(edited.contains("[parts.Tenor1.lyrics.1]"));
        QVERIFY(edited.contains("[parts.Tenor1.lyrics.2]"));
        QVERIFY(edited.contains("[parts.Tenor1.lyrics.3]"));
        QVERIFY(!edited.contains("notes ="));
        QVERIFY(!edited.contains("clef ="));
        QVERIFY(!edited.contains("choral_type"));
        QCOMPARE(session.effectiveDocument().part(u"Tenor1")->lyrics.size(), 3);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), edited);
        QVERIFY(session.save());
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        QCOMPARE(session.currentBytes(), edited);
        QCOMPARE(io::serialize(*session.baseDocument()), base);
    }

    void structuredEditsAfterANotesReplacementAreNotReparsedAway()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        session.mutate("Replace notes", [](SongDocument &doc) {
            doc.part(u"Tenor1")->notes.set("c'2 d'4 | e'2.");
        });
        const QByteArray replaced = session.currentBytes();
        session.mutate("Raise opening note", [](SongDocument &doc) {
            auto &event = doc.part(u"Tenor1")->stream.measures()[0].events[0];
            event.pitches[0].octave += 1;
            event.dirty = true;
        });
        QVERIFY(session.currentBytes().contains("c''2"));
        QCOMPARE(session.effectiveDocument().part(u"Tenor1")->stream.measures()[0].events[0].pitches[0].midiNote(), 72);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), replaced);
    }

    void revertingAdjacentChorusOverrides_data()
    {
        QTest::addColumn<QStringList>("order");
        QTest::newRow("tenor-then-bass") << QStringList { "Tenor", "Bass" };
        QTest::newRow("bass-then-tenor") << QStringList { "Bass", "Tenor" };
    }

    void revertingAdjacentChorusOverrides()
    {
        QFETCH(QStringList, order);
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QByteArray defaults = baseSong()
            + "\n[parts.Tenor]\nnotes = \"c1\"\n"
              "\n[parts.Bass]\nnotes = \"c1\"\n"
              "\n[lyrics.chorus]\ntext = \"song default\"\n";
        const QByteArray original = defaults
            + "\n[parts.Tenor.lyrics.chorus]\ntext = \"tenor chorus\"\n"
              "\n[parts.Bass.lyrics.chorus]\ntext = \"bass chorus\"\n";
        write(root, QStringLiteral("song.toml"), original);
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));

        for (const QString &name : order) {
            session.mutate(QStringLiteral("Revert lyrics"), [&](SongDocument &doc) {
                doc.removePartLyric(*doc.part(name), QStringLiteral("chorus"));
            });
            QVERIFY(!session.document().part(name)->lyrics.contains(QStringLiteral("chorus")));
            QVERIFY(!session.currentBytes().contains(
                "[parts." + name.toUtf8() + ".lyrics.chorus]"));
        }
        QCOMPARE(session.currentBytes(), defaults);
        session.undoStack()->undo();
        QVERIFY(session.document().part(order.last())->lyrics.contains(QStringLiteral("chorus")));
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), original);
        session.undoStack()->redo();
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), defaults);
        QVERIFY(session.save());
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        QCOMPARE(session.currentBytes(), defaults);
    }

    void exactSourceEditsJoinTheSessionUndoAndSaveModel()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QString path = root.filePath(QStringLiteral("song.toml"));
        const QByteArray original = baseSong();
        write(root, QStringLiteral("song.toml"), original);
        Session session;
        QVERIFY(session.openSong(path));

        QByteArray edited = original;
        edited.prepend("# edited directly in Source\n");
        edited.replace("Face to Face", "Source title");
        QVERIFY(session.replaceSource(QStringLiteral("en"), edited));
        QCOMPARE(session.document().title.valueOr(QString()), QStringLiteral("Source title"));
        QCOMPARE(session.currentBytes(), edited);
        QCOMPARE(session.diskBytes(), original);
        QVERIFY(session.isDirty());
        QCOMPARE(session.undoStack()->count(), 1);

        session.mutate(QStringLiteral("Structured title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Structured title")); });
        QVERIFY(session.currentBytes().startsWith("# edited directly in Source\n"));
        QVERIFY(session.currentBytes().contains("title = \"Structured title\""));

        QVERIFY(session.save());
        QCOMPARE(session.diskBytes(), session.currentBytes());
        QVERIFY(!session.isDirty());
    }

    void invalidSourceNeverReplacesTheStructuredDocument()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        const QByteArray before = session.currentBytes();

        const auto replaced
            = session.replaceSource(QStringLiteral("en"), QByteArray("title = \"unfinished\n"));
        QVERIFY(!replaced);
        QVERIFY(replaced.error().parse.line > 0);
        QCOMPARE(session.currentBytes(), before);
        QVERIFY(!session.isDirty());
    }

    void openedBytesSurviveSaveForContributionDiffs()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QByteArray original = baseSong();
        write(root, QStringLiteral("song.toml"), original);
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        QCOMPARE(session.openedBytes(), original);

        session.mutate(QStringLiteral("Title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Edited title")); });
        QVERIFY(session.save());
        QCOMPARE(session.openedBytes(), original);
        QVERIFY(session.document().originalBytes != original);
    }

    void aBreakLivesInExactlyOneLane()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(QDir(dir.path()).filePath(QStringLiteral("song.toml"))));

        const PhraseBreak position { 1, 32 };
        QVERIFY(!session.phraseBreakAt(position));

        session.togglePhraseBreak(position, BreakKind::Required);
        QCOMPARE(session.phraseBreakAt(position), BreakKind::Required);

        // Moving it to another lane must take it out of the first one, not
        // leave the same "M:T" in two arrays.
        session.setPhraseBreak(position, BreakKind::Optional);
        QCOMPARE(session.phraseBreakAt(position), BreakKind::Optional);
        QVERIFY(!session.document().phraseBreaks->contains(position));
        QCOMPARE(session.document().optionalPhraseBreaks.valueOr({}).size(), 1);

        // Toggling the lane it is already in removes it.
        session.togglePhraseBreak(position, BreakKind::Optional);
        QVERIFY(!session.phraseBreakAt(position));
        QVERIFY(!session.document().optionalPhraseBreaks.present());
        // The break the file came with is still there and still sorted.
        QCOMPARE(session.document().phraseBreaks.valueOr({}),
            QList<PhraseBreak>({ PhraseBreak { 2, 64 } }));
    }

    void breaksStaySortedAndTheUntouchedLaneIsNotRewritten()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(QDir(dir.path()).filePath(QStringLiteral("song.toml"))));

        session.togglePhraseBreak(PhraseBreak { 1, 16 }, BreakKind::Required);
        QCOMPARE(session.document().phraseBreaks.valueOr({}),
            QList<PhraseBreak>({ PhraseBreak { 1, 16 }, PhraseBreak { 2, 64 } }));
        // Nothing was written to the lanes the user did not touch.
        QVERIFY(!session.document().optionalPhraseBreaks.present());
        QVERIFY(!session.document().nonBreakingPhraseBreaks.present());
    }

    void aTranslationEditsTheMergedListAndOnlyTheLaneItTouches()
    {
        // phrase_breaks in an overlay replaces the base's array whole. Adding
        // one break to a translation that defines none must therefore carry the
        // inherited breaks with it, and must not freeze the other two lanes.
        QTemporaryDir dir;
        write(QDir(dir.path()), QStringLiteral("song.toml"), baseSong());
        write(QDir(dir.path()), QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(QDir(dir.path()).filePath(QStringLiteral("song.toml"))));
        session.setCurrentLanguage(QStringLiteral("es"));
        QVERIFY(session.document().isOverlay);
        QVERIFY(!session.document().phraseBreaks.present());
        QCOMPARE(session.effectiveDocument().phraseBreaks.valueOr({}).size(), 1);

        session.togglePhraseBreak(PhraseBreak { 1, 16 }, BreakKind::Required);

        const SongDocument &overlay = session.document();
        QCOMPARE(overlay.phraseBreaks.valueOr({}),
            QList<PhraseBreak>({ PhraseBreak { 1, 16 }, PhraseBreak { 2, 64 } }));
        QVERIFY(!overlay.optionalPhraseBreaks.present());
        QVERIFY(!overlay.nonBreakingPhraseBreaks.present());
        // The base is untouched.
        QCOMPARE(session.baseDocument()->phraseBreaks.valueOr({}).size(), 1);
    }

    void aTranslationTouchingOneLaneLeavesTheOthersInherited()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), QStringLiteral("song.toml"), baseSong());
        write(QDir(dir.path()), QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(QDir(dir.path()).filePath(QStringLiteral("song.toml"))));
        session.setCurrentLanguage(QStringLiteral("es"));

        session.togglePhraseBreak(PhraseBreak { 1, 16 }, BreakKind::Optional);
        QVERIFY(!session.document().phraseBreaks.present());  // still inherited
        QCOMPARE(session.document().optionalPhraseBreaks.valueOr({}),
            QList<PhraseBreak>({ PhraseBreak { 1, 16 } }));
        QCOMPARE(session.effectiveDocument().phraseBreaks.valueOr({}).size(), 1);
    }

    void everyBreakEditIsOneUndoStep()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(QDir(dir.path()).filePath(QStringLiteral("song.toml"))));

        const PhraseBreak position { 1, 16 };
        session.togglePhraseBreak(position, BreakKind::Required);
        QCOMPARE(session.undoStack()->count(), 1);
        // A no-op does not land on the stack.
        session.setPhraseBreak(position, BreakKind::Required);
        QCOMPARE(session.undoStack()->count(), 1);

        session.undoStack()->undo();
        QVERIFY(!session.phraseBreakAt(position));
        QCOMPARE(io::serialize(session.document()), baseSong());
    }


    void eachLanguageHasIndependentDirtyAndUndoState()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        write(root, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));

        session.mutate(QStringLiteral("Base title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Base edit")); });
        session.setCurrentLanguage(QStringLiteral("es"));
        session.mutate(QStringLiteral("Spanish title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Edición")); });
        QCOMPARE(session.dirtyLanguages(), QStringList({ QStringLiteral("en"), QStringLiteral("es") }));

        session.undoStack()->undo();
        QVERIFY(session.isDirty(QStringLiteral("en")));
        QVERIFY(!session.isDirty(QStringLiteral("es")));
        QCOMPARE(session.currentLanguage(), QStringLiteral("es"));
        QCOMPARE(session.document().title.valueOr(QString()), QStringLiteral("Cara a cara"));
    }

    void savingOneLanguageDoesNotCleanAnother()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        write(root, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        session.mutate(QStringLiteral("Base title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Base edit")); });
        session.setCurrentLanguage(QStringLiteral("es"));
        session.mutate(QStringLiteral("Spanish title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Edición")); });

        QVERIFY(session.save(QStringLiteral("en")));
        QVERIFY(!session.isDirty(QStringLiteral("en")));
        QVERIFY(session.isDirty(QStringLiteral("es")));
        QCOMPARE(session.currentLanguage(), QStringLiteral("es"));
    }

    void externalChangesAreNeverSilentlyOverwritten()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QString path = root.filePath(QStringLiteral("song.toml"));
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(path));
        session.mutate(QStringLiteral("Title"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("OPE edit")); });

        const QByteArray external = QByteArray("title = \"External edit\"\n");
        write(root, QStringLiteral("song.toml"), external);
        const auto refused = session.save();
        QVERIFY(!refused);
        QCOMPARE(refused.error().kind, Session::SaveError::Kind::Conflict);
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), external);

        QVERIFY(session.save(true));
        QVERIFY(!session.isDirty());
        QFile overwritten(path);
        QVERIFY(overwritten.open(QIODevice::ReadOnly));
        QVERIFY(overwritten.readAll().contains("OPE edit"));
    }

    void newTranslationStaysInMemoryUntilExplicitlySaved()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));

        SongDocument overlay;
        overlay.isOverlay = true;
        overlay.language = QStringLiteral("fr");
        overlay.path = root.filePath(QStringLiteral("song_fr.toml"));
        overlay.title.set(QStringLiteral("Face à face"));
        QVERIFY(session.adoptNewOverlay(overlay));
        QVERIFY(session.isNewFile());
        QVERIFY(!QFileInfo::exists(overlay.path));
        QVERIFY(!session.adoptNewOverlay(overlay));

        QVERIFY(session.save());
        QVERIFY(QFileInfo::exists(overlay.path));
        QVERIFY(!session.isNewFile());
    }

    void openingATranslationPathSelectsItsSiblingOverlay()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        const QString overlayPath = root.filePath(QStringLiteral("song_es.toml"));
        write(root, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(overlayPath));
        QCOMPARE(session.currentLanguage(), QStringLiteral("es"));
        QCOMPARE(session.languages().size(), 2);
        QCOMPARE(session.baseDocument()->path, root.filePath(QStringLiteral("song.toml")));
    }

    void duplicateLanguageFilesCannotReplaceTheBaseInMemory()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        write(root, QStringLiteral("song_en.toml"), "title = \"Duplicate\"\n");
        Session session;
        const auto opened = session.openSong(root.filePath(QStringLiteral("song.toml")));
        QVERIFY(!opened);
        QVERIFY(opened.error().message.contains(QStringLiteral("already used")));
        QVERIFY(!session.isOpen());
    }

    void aNewSongDirectoryIsCreatedOnlyWhenSaved()
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("42/song.toml"));
        SongDocument document;
        document.path = path;
        document.language = QStringLiteral("en");
        document.title.set(QStringLiteral("New song"));
        Session session;
        session.adoptNewDocument(document);
        QVERIFY(!QFileInfo::exists(QFileInfo(path).path()));
        QVERIFY(session.save());
        QVERIFY(QFileInfo::exists(path));
    }

    void aNewSongRefusesADirectoryOccupiedAfterTheWizard()
    {
        QTemporaryDir dir;
        const QString directory = QDir(dir.path()).filePath(QStringLiteral("42"));
        const QString path = QDir(directory).filePath(QStringLiteral("song.toml"));
        SongDocument document;
        document.path = path;
        document.language = QStringLiteral("en");
        document.title.set(QStringLiteral("New song"));
        Session session;
        session.adoptNewDocument(document);

        QVERIFY(QDir().mkpath(directory));
        write(QDir(directory), QStringLiteral("README.txt"), "occupied\n");
        const auto saved = session.save();
        QVERIFY(!saved);
        QCOMPARE(saved.error().kind, Session::SaveError::Kind::Conflict);
        QVERIFY(!QFileInfo::exists(path));
    }
};

QTEST_GUILESS_MAIN(SessionTests)
#include "SessionTests.moc"
