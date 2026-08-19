// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// TOML span handling and note tokenizing.
//
// The tokenizer cases are ports of the tests in OpenPsalm's
// src/seed/parser.rs — if this file and that one ever disagree, the seeder is
// right and OPE has a bug.

#include "Fixtures.h"

#include "core/Notation.h"
#include "core/Song.h"
#include "core/Toml.h"

#include <QTest>

using namespace ope;

namespace {

Event firstEvent(const QString &notes)
{
    const NoteStream stream = NoteStream::parse(notes);
    Q_ASSERT(!stream.measures().isEmpty());
    Q_ASSERT(!stream.measures().first().events.isEmpty());
    return stream.measures().first().events.first();
}

} // namespace

class FormatTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // -- TOML ----------------------------------------------------------------

    void parsesScalarsAndRecordsSpans()
    {
        const QByteArray source = "title = \"Jesus Loves Me\"\ntempo_bpm = 120\nactive = true\n";
        const auto doc = toml::parse(source);
        QVERIFY(doc.has_value());
        const toml::KeyValue *title = doc->rootPair(QStringLiteral("title"));
        QVERIFY(title);
        QCOMPARE(title->value.string, QStringLiteral("Jesus Loves Me"));
        QCOMPARE(doc->textOf(title->value.span), QByteArray("\"Jesus Loves Me\""));
        QCOMPARE(doc->rootPair(QStringLiteral("tempo_bpm"))->value.integer, 120);
        QCOMPARE(doc->rootPair(QStringLiteral("active"))->value.boolean, true);
    }

    void parsesTablesArraysAndMultilineStrings()
    {
        const QByteArray source = ope::fixtures::partsAndMultilineStrings();
        const auto doc = toml::parse(source);
        QVERIFY(doc.has_value());
        const toml::KeyValue *copyrights = doc->rootPair(QStringLiteral("copyrights"));
        QVERIFY(copyrights);
        QCOMPARE(copyrights->value.toStringList(), QStringList({ "one", "two" }));
        QVERIFY(copyrights->value.multilineLayout);

        const toml::Table *part = doc->table({ QStringLiteral("parts"), QStringLiteral("Soprano") });
        QVERIFY(part);
        QCOMPARE(part->find(QStringLiteral("clef"))->value.string, QStringLiteral("treble"));
        QCOMPARE(part->find(QStringLiteral("notes"))->value.string,
            QStringLiteral("c'4 d'4 | e'2\n"));
    }

    void keepsCommentsAndUnknownConstructs()
    {
        // A file OPE does not fully understand must still open and still be
        // rewritable without loss.
        const QByteArray source = "# a comment\ntitle = \"T\"\nmystery = 1979-05-27\n";
        const auto doc = toml::parse(source);
        QVERIFY(doc.has_value());
        QCOMPARE(doc->rootPair(QStringLiteral("mystery"))->value.kind, toml::ValueKind::Opaque);
    }

    void reportsSyntaxErrorsWithAPosition()
    {
        const auto doc = toml::parse("title = \"unterminated\ntempo = 4\n");
        QVERIFY(!doc.has_value());
        QCOMPARE(doc.error().line, 1);
        QVERIFY(doc.error().message.contains(QStringLiteral("unterminated")));
    }

    void editSplicesOnlyTheGivenSpans()
    {
        const QByteArray source = "title = \"Old\"\ntempo_bpm = 100\n";
        const auto doc = toml::parse(source);
        QVERIFY(doc.has_value());
        toml::Edit edit;
        edit.replace(doc->rootPair(QStringLiteral("title"))->value.span, "\"New\"");
        QCOMPARE(edit.apply(source), QByteArray("title = \"New\"\ntempo_bpm = 100\n"));
    }

    void emptyEditReturnsTheOriginalBytes()
    {
        const QByteArray source = "# comment\n\ntitle  =   \"spaced out\"\n";
        const toml::Edit edit;
        QCOMPARE(edit.apply(source), source);
    }

    // -- pitches and durations ------------------------------------------------

    void pitchesFollowTheSeedersOctaveConvention()
    {
        // No octave mark is octave 3, so c' is middle C (MIDI 60).
        QCOMPARE(Pitch::fromToken(u"c").octave, 3);
        QCOMPARE(Pitch::fromToken(u"c'").midiNote(), 60);
        QCOMPARE(Pitch::fromToken(u"bes'").alter, -1);
        QCOMPARE(Pitch::fromToken(u"fis").alter, 1);
        QCOMPARE(Pitch::fromToken(u"c,").octave, 2);
        QCOMPARE(Pitch::fromToken(u"c''").octave, 5);
        QCOMPARE(Pitch::fromToken(u"beses").alter, -2);
        QCOMPARE(Pitch::fromToken(u"fisis").alter, 2);
    }

    void pitchTokensRoundTrip()
    {
        for (const char *token : { "c", "bes'", "fis,,", "aes''", "g", "cisis'" })
            QCOMPARE(Pitch::fromToken(QString::fromLatin1(token)).toToken(),
                QString::fromLatin1(token));
    }

    void durationTicksMatchTheSeeder()
    {
        QCOMPARE((Duration { 4, 0 }).notatedTicks(), ticks::Quarter);
        QCOMPARE((Duration { 2, 1 }).notatedTicks(), 144);   // dotted half
        QCOMPARE((Duration { 4, 2 }).notatedTicks(), 84);    // double-dotted quarter
        QCOMPARE(ticks::forTimeSignature(4, 4), 192);
        QCOMPARE(ticks::forTimeSignature(3, 4), 144);
        QCOMPARE(ticks::forTimeSignature(6, 8), 144);
        QCOMPARE(ticks::forTimeSignature(3, 32), 18);
        QCOMPARE(ticks::forTimeSignature(3, 64), 9);
        QVERIFY(ticks::isSupportedTimeSignatureDenominator(32));
        QVERIFY(!ticks::isSupportedTimeSignatureDenominator(3));
    }

    // -- note tokens ----------------------------------------------------------

    void suffixMarkersAreOrderIndependent()
    {
        for (const char *token : { "c''4)!", "c''4!)" }) {
            const Event event = firstEvent(QString::fromLatin1(token));
            QCOMPARE(event.duration.base, 4);
            QVERIFY(event.fermata);
            QVERIFY(event.slurEnd);
        }
        for (const char *token : { "c''4(~", "c''4~(" }) {
            const Event event = firstEvent(QString::fromLatin1(token));
            QVERIFY(event.tie);
            QVERIFY(event.slurStart);
        }
        for (const char *token : { "c''4@c-.", "c''4-.@c" }) {
            const Event event = firstEvent(QString::fromLatin1(token));
            QVERIFY(event.staccato);
            QVERIFY(event.chorusStart);
        }
    }

    void dashedSlurSurvivesATrailingStaccato()
    {
        const Event start = firstEvent(QStringLiteral("c''4-(-."));
        QVERIFY(start.staccato);
        QVERIFY(start.dashedSlurStart);
        const Event end = firstEvent(QStringLiteral("c''4-)-."));
        QVERIFY(end.staccato);
        QVERIFY(end.dashedSlurEnd);
    }

    void accentsAndMarcatosAreDistinctOrderIndependentMarkers()
    {
        for (const char *token : { "c''4@c^", "c''4^@c", "c''4-.^", "c''4^-.",
                 "c''4^])" }) {
            const Event event = firstEvent(QString::fromLatin1(token));
            QVERIFY(event.accent);
            QVERIFY(!event.marcato);
        }
        for (const char *token : { "c''4@c^^", "c''4^^@c", "c''4-.^^", "c''4^^-.",
                 "c''4^^])" }) {
            const Event event = firstEvent(QString::fromLatin1(token));
            QVERIFY(event.marcato);
            QVERIFY(!event.accent);
        }

        const Event accentedStaccato = firstEvent(QStringLiteral("c''8-.^"));
        QVERIFY(accentedStaccato.staccato);
        QVERIFY(accentedStaccato.accent);
        const Event marcatoChord = firstEvent(QStringLiteral("<g g,>4^^"));
        QCOMPARE(marcatoChord.pitches.size(), 2);
        QVERIFY(marcatoChord.marcato);
    }

    void hairpinsAndDynamics()
    {
        QCOMPARE(firstEvent(QStringLiteral("c''4\\<")).hairpin, QStringLiteral("crescendo"));
        QCOMPARE(firstEvent(QStringLiteral("c''4\\>")).hairpin, QStringLiteral("diminuendo"));
        const Event both = firstEvent(QStringLiteral("c''4%p\\<"));
        QCOMPARE(both.dynamic, QStringLiteral("p"));
        QCOMPARE(both.hairpin, QStringLiteral("crescendo"));
        const Event endThenDynamic = firstEvent(QStringLiteral("e''4\\!%f"));
        QCOMPARE(endThenDynamic.dynamic, QStringLiteral("f"));
        QCOMPARE(endThenDynamic.hairpin, QStringLiteral("end"));
    }

    void tempoSpanners()
    {
        QCOMPARE(firstEvent(QStringLiteral("aes'2.\\rit")).tempoSpanner, QStringLiteral("rit"));
        QCOMPARE(firstEvent(QStringLiteral("aes'4\\ritard")).tempoSpanner,
            QStringLiteral("ritard"));  // longest match wins
        QVERIFY(firstEvent(QStringLiteral("aes'2.\\spanend")).spannerEnd);
        const Event stacked = firstEvent(QStringLiteral("c'4\\spanend\\atempo"));
        QVERIFY(stacked.spannerEnd);
        QCOMPARE(stacked.tempoSpanner, QStringLiteral("atempo"));
        const Event withHairpin = firstEvent(QStringLiteral("aes'4\\<\\rit"));
        QCOMPARE(withHairpin.hairpin, QStringLiteral("crescendo"));
        QCOMPARE(withHairpin.tempoSpanner, QStringLiteral("rit"));
    }

    void chordsCarryMarkingsOnTheFirstPitchOnly()
    {
        const Event chord = firstEvent(QStringLiteral("<g g,>4!@c"));
        QCOMPARE(chord.pitches.size(), 2);
        QVERIFY(chord.fermata);
        QVERIFY(chord.chorusStart);
        QCOMPARE(chord.kind, EventKind::Chord);
    }

    void dedupOffsetIsExtractedBeforeOtherFlags()
    {
        const Event event = firstEvent(QStringLiteral("a4/+24("));
        QCOMPARE(event.dedupOffset, 24);
        QVERIFY(event.slurStart);
        QCOMPARE(event.duration.base, 4);
        QCOMPARE(firstEvent(QStringLiteral("f8/-48")).dedupOffset, -48);
    }

    void tupletsScaleWithTruncatingDivision()
    {
        const NoteStream stream = NoteStream::parse(QStringLiteral("{3 c'8 d'8 e'8 } c'4 c'4 c'4"));
        const Measure &measure = stream.measures().first();
        QCOMPARE(measure.events.size(), 6);
        QVERIFY(measure.events.first().tuplet.has_value());
        QCOMPARE(measure.events.first().tuplet->actual, 3);
        QCOMPARE(measure.events.first().tuplet->normal, 2);
        // A triplet eighth is 24 * 2 / 3 = 16 ticks.
        QCOMPARE(measure.events.first().playedTicks(), 16);
        QCOMPARE(measure.playedTicks(), 192);
    }

    void quintupletUsesTheExplicitRatio()
    {
        const NoteStream stream = NoteStream::parse(QStringLiteral("{5:4 c'16 d'16 e'16 f'16 g'16 }"));
        const Event &first = stream.measures().first().events.first();
        QCOMPARE(first.tuplet->actual, 5);
        QCOMPARE(first.tuplet->normal, 4);
    }

    void restsAndSpacers()
    {
        QCOMPARE(firstEvent(QStringLiteral("r4")).kind, EventKind::Rest);
        QCOMPARE(firstEvent(QStringLiteral("s2.")).kind, EventKind::Spacer);
        QCOMPARE(firstEvent(QStringLiteral("s2.")).duration.dots, 1);
        QVERIFY(firstEvent(QStringLiteral("s4!")).fermata);
        // The seeder clears a tie flag written on a rest.
        QVERIFY(!firstEvent(QStringLiteral("r4~")).tie);
    }

    void measuresSplitOnBarsAndNewlinesAlike()
    {
        const NoteStream stream = NoteStream::parse(QStringLiteral("c'4 c'4 c'4 c'4 |\nd'1"));
        QCOMPARE(stream.measureCount(), 2);
        QCOMPARE(stream.measures().at(1).playedTicks(), 192);
    }

    void silentlyDefaultedDurationsAreReported()
    {
        QList<TokenIssue> issues;
        NoteStream::parse(QStringLiteral("aes, aes,4 c'9"), &issues);
        QVERIFY(std::any_of(issues.begin(), issues.end(), [](const TokenIssue &issue) {
            return issue.code == TokenIssue::Code::MissingDuration;
        }));
        QVERIFY(std::any_of(issues.begin(), issues.end(), [](const TokenIssue &issue) {
            return issue.code == TokenIssue::Code::UnknownDuration;
        }));
    }

    void brokenChorusMarkerIsReported()
    {
        // The real defect found in songs/107: `@c` truncated to a bare `@`.
        QList<TokenIssue> issues;
        const NoteStream stream = NoteStream::parse(QStringLiteral("f'4@"), &issues);
        QVERIFY(!stream.measures().first().events.first().chorusStart);
        QVERIFY(!issues.isEmpty());
    }

    // -- emission -------------------------------------------------------------

    void editedTokensReEmitLosslessly()
    {
        const QString source = QStringLiteral(
            "bes'4( g'8[ g'8]) f'4~ | g'4%mf\\< bes'4! bes'2-. | "
            "<c' e'>4@c^ r4 s2^^");
        NoteStream stream = NoteStream::parse(source);
        for (Measure &measure : stream.measures()) {
            for (Event &event : measure.events)
                event.dirty = true;  // force regeneration rather than raw reuse
        }
        const NoteStream again = NoteStream::parse(stream.toSource());
        QCOMPARE(again.measureCount(), stream.measureCount());
        for (qsizetype m = 0; m < again.measureCount(); ++m) {
            const QList<Event> &before = stream.measures().at(m).events;
            const QList<Event> &after = again.measures().at(m).events;
            QCOMPARE(after.size(), before.size());
            for (qsizetype e = 0; e < after.size(); ++e) {
                QCOMPARE(after.at(e).duration, before.at(e).duration);
                QCOMPARE(after.at(e).pitches, before.at(e).pitches);
                QCOMPARE(after.at(e).kind, before.at(e).kind);
                QCOMPARE(after.at(e).fermata, before.at(e).fermata);
                QCOMPARE(after.at(e).staccato, before.at(e).staccato);
                QCOMPARE(after.at(e).accent, before.at(e).accent);
                QCOMPARE(after.at(e).marcato, before.at(e).marcato);
                QCOMPARE(after.at(e).tie, before.at(e).tie);
                QCOMPARE(after.at(e).dynamic, before.at(e).dynamic);
                QCOMPARE(after.at(e).hairpin, before.at(e).hairpin);
                QCOMPARE(after.at(e).chorusStart, before.at(e).chorusStart);
            }
        }
    }

    void untouchedTokensKeepTheirExactSourceText()
    {
        const QString source = QStringLiteral("c''4!)  d'8[   e'8]");
        const NoteStream stream = NoteStream::parse(source);
        QCOMPARE(stream.measures().first().events.first().text(), QStringLiteral("c''4!)"));
    }

    void lineLayoutIsPreservedAcrossEmission()
    {
        const QString source
            = QStringLiteral("c'4 c'4 c'4 c'4 |\nd'4 d'4 d'4 d'4 |\ne'1");
        const NoteStream stream = NoteStream::parse(source);
        QCOMPARE(stream.lineLayout(), QList<int>({ 1, 1, 1 }));
        QCOMPARE(stream.toSource().count(u'\n'), 2);
    }
};

QTEST_APPLESS_MAIN(FormatTests)
#include "FormatTests.moc"
