// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "core/Playback.h"
#include "core/Song.h"
#include "core/Validator.h"

#include <QTest>

#include <algorithm>

using namespace ope;

namespace {

QByteArray song(const QString &notes, int bpm = 100)
{
    return "title = 'Tempo marks'\ntime_sig_numerator = 4\ntime_sig_denominator = 4\n"
           "tempo_bpm = " + QByteArray::number(bpm)
        + "\n[parts.Solo]\nnotes = '''" + notes.toUtf8() + "'''\n";
}

} // namespace

class TempoTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void stepMarks_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("label");
        QTest::addColumn<int>("bpm");
        QTest::addColumn<int>("target");
        QTest::newRow("largo") << QStringLiteral("largo") << QStringLiteral("Largo") << 100 << 60;
        QTest::newRow("lento") << QStringLiteral("lento") << QStringLiteral("Lento") << 100 << 70;
        QTest::newRow("adagio") << QStringLiteral("adagio") << QStringLiteral("Adagio") << 100 << 75;
        QTest::newRow("andante") << QStringLiteral("andante") << QStringLiteral("Andante") << 100 << 85;
        QTest::newRow("moderato") << QStringLiteral("moderato") << QStringLiteral("Moderato") << 100 << 100;
        QTest::newRow("allegretto") << QStringLiteral("allegretto") << QStringLiteral("Allegretto") << 100 << 115;
        QTest::newRow("allegro") << QStringLiteral("allegro") << QStringLiteral("Allegro") << 100 << 130;
        QTest::newRow("vivace") << QStringLiteral("vivace") << QStringLiteral("Vivace") << 100 << 145;
        QTest::newRow("presto") << QStringLiteral("presto") << QStringLiteral("Presto") << 100 << 160;
        QTest::newRow("BPM floor") << QStringLiteral("largo") << QStringLiteral("Largo") << 25 << 20;
        QTest::newRow("round BPM") << QStringLiteral("adagio") << QStringLiteral("Adagio") << 90 << 68;
        QTest::newRow("changed base tempo") << QStringLiteral("allegro") << QStringLiteral("Allegro") << 50 << 65;
    }

    void stepMarks()
    {
        QFETCH(QString, name);
        QFETCH(QString, label);
        QFETCH(int, bpm);
        QFETCH(int, target);
        const auto *mark = tempoMark(name);
        QVERIFY(mark);
        QCOMPARE(mark->label, label);
        QCOMPARE(mark->kind, TempoMark::Kind::Step);
        for (const QString &prefix : {QStringLiteral("c'"), QStringLiteral("r"),
                 QStringLiteral("s"), QStringLiteral("<c' e'>")}) {
            const QString token = prefix + "2.\\" + name;
            QList<TokenIssue> issues;
            const auto stream = NoteStream::parse(token, &issues);
            QVERIFY2(issues.isEmpty(), qPrintable(token));
            const auto &event = stream.measures().first().events.first();
            QCOMPARE(event.duration, (Duration{2, 1}));
            QCOMPARE(event.playedTicks(), 144);
            QCOMPARE(event.tempoSpanner, name);
            QCOMPARE(event.text(), token);
            QCOMPARE(event.toSource(), token);
        }

        const QByteArray bytes = song("c'4 c'4\\" + name + " c'4 c'4", bpm);
        const auto doc = io::loadBytes("song.toml", bytes);
        QVERIFY(doc);
        QCOMPARE(io::serialize(*doc), bytes);
        QCOMPARE(countBySeverity(validate(*doc), Severity::Error), 0);
        const auto plan = buildPlan(*doc);
        QCOMPARE(plan.notes.size(), 4);
        const double opening = (60000000 / bpm) / 1000000.0;
        const double held = (60000000 / target) / 1000000.0;
        for (int i = 0; i < 4; ++i) {
            const double expected = i == 0 ? 0.0 : opening + (i - 1) * held;
            QVERIFY(qAbs(plan.notes[i].startSeconds - expected) < 1e-9);
            QVERIFY(qAbs(plan.notes[i].endSeconds - expected - (i == 0 ? opening : held)) < 1e-9);
        }
        QVERIFY(qAbs(plan.totalSeconds - opening - 3 * held) < 1e-9);
        PlaybackOptions options;
        options.tempoScale = 0.5;
        QVERIFY(qAbs(buildPlan(*doc, options).totalSeconds - 2 * plan.totalSeconds) < 1e-9);
    }

    void combinedMarkingsSurviveRegeneration()
    {
        for (const QString &name : tempoSpannerNames()) {
            for (const QString &closing : {QStringLiteral("])"), QStringLiteral("-)"), QString()}) {
                for (const QString &hairpin : {QStringLiteral("\\<"), QStringLiteral("\\>"), QStringLiteral("\\!")}) {
                    const QString source = "<c' e'>2./-24~!-.^^@c@e" + closing
                        + "\\spanend%f\\" + name + hairpin;
                    QList<TokenIssue> issues;
                    const auto before = NoteStream::parse(source, &issues);
                    QVERIFY2(issues.isEmpty(), qPrintable(source));
                    Event event = before.measures().first().events.first();
                    event.dirty = true;
                    event.pitches[0].step = u'D'; // force a real structured edit
                    const auto after = NoteStream::parse(event.text(), &issues);
                    QVERIFY2(issues.isEmpty(), qPrintable(event.text()));
                    const Event &actual = after.measures().first().events.first();
                    QCOMPARE(actual.pitches, event.pitches);
                    QCOMPARE(actual.duration, (Duration{2, 1}));
                    QCOMPARE(actual.dynamic, QStringLiteral("f"));
                    QCOMPARE(actual.tempoSpanner, name);
                    QCOMPARE(actual.hairpin, event.hairpin);
                    QCOMPARE(actual.slurEnd, event.slurEnd);
                    QCOMPARE(actual.beamEnd, event.beamEnd);
                    QCOMPARE(actual.dashedSlurEnd, event.dashedSlurEnd);
                    QCOMPARE(actual.dedupOffset, -24);
                    QVERIFY(actual.spannerEnd && actual.fermata && actual.tie && actual.staccato);
                    QVERIFY(actual.marcato && actual.chorusStart && actual.codaStart);
                }
            }
        }
    }

    void editedTomlKeepsMarkersAndEscapes()
    {
        const QString notes = QStringLiteral("c'4@c%f\\allegro\\< d'4 e'4 f'4\\!");
        const QList<QByteArray> sources {
            song(notes),
            "title = 'Tempo marks'\n[parts.Solo]\nnotes = \"\"\"\n"
                + QString(notes).replace("\\", "\\\\").toUtf8() + "\n\"\"\"\n",
        };
        for (const QByteArray &bytes : sources) {
            auto doc = io::loadBytes("song.toml", bytes);
            QVERIFY(doc);
            QCOMPARE(io::serialize(*doc), bytes);
            auto &event = doc->parts[0].stream.measures()[0].events[0];
            event.pitches[0].step = u'D';
            event.dirty = true;
            const QByteArray edited = io::serialize(*doc);
            const auto reloaded = io::loadBytes("song.toml", edited);
            QVERIFY(reloaded);
            QCOMPARE(io::serialize(*reloaded), edited);
            QVERIFY(reloaded->parts[0].tokenIssues.isEmpty());
            const auto &actual = reloaded->parts[0].stream.measures()[0].events[0];
            QCOMPARE(actual.pitches[0].step, QChar(u'D'));
            QCOMPARE(actual.dynamic, QStringLiteral("f"));
            QCOMPARE(actual.tempoSpanner, QStringLiteral("allegro"));
            QCOMPARE(actual.hairpin, QStringLiteral("crescendo"));
            QVERIFY(actual.chorusStart);
        }
    }

    void misspelledTempoMarksAreErrors()
    {
        for (const QString &token : {QStringLiteral("c'2\\alegro"), QStringLiteral("r2\\prestoo"),
                 QStringLiteral("s2\\andantino"), QStringLiteral("<c' e'>2\\allegrettto"),
                 QStringLiteral("c'2%f\\alegro")}) {
            const auto doc = io::loadBytes("song.toml", song("c'1 | " + token + " c'2"));
            QVERIFY(doc);
            const auto findings = validate(*doc);
            QVERIFY(std::any_of(findings.cbegin(), findings.cend(), [&](const Finding &finding) {
                return finding.severity == Severity::Error && finding.partName == "Solo"
                    && finding.measure == 2 && finding.eventIndex == 0 && finding.message.contains(token);
            }));
        }
    }

    void tempoTransitions_data()
    {
        QTest::addColumn<QString>("notes");
        QTest::addColumn<QList<double>>("durations");
        // Golden quarter-note durations from the upstream MIDI contract:
        // 130 BPM = 461538 us, 85 BPM = 705882 us; gradual spans use eight
        // rounded microsecond events at 1/8 of the span to the end-note onset.
        constexpr double allegro = 0.461538;
        constexpr double andante = 0.705882;
        QTest::newRow("held across measures")
            << QStringLiteral("c4 c4\\allegro c4 c4 | c1")
            << QList<double>{0.6, allegro, allegro, allegro, 4 * allegro};
        QTest::newRow("spanend restores after note")
            << QStringLiteral("c4\\allegro c4 c4\\spanend c4")
            << QList<double>{allegro, allegro, allegro, 0.6};
        QTest::newRow("a tempo restores at onset")
            << QStringLiteral("c4\\allegro c4\\atempo c4 c4")
            << QList<double>{allegro, 0.6, 0.6, 0.6};
        QTest::newRow("replace step")
            << QStringLiteral("c4\\allegro c4\\andante c4 c4")
            << QList<double>{allegro, andante, andante, andante};
        QTest::newRow("mark on terminator takes over at onset")
            << QStringLiteral("c4\\allegro c4 c4\\spanend\\andante c4")
            << QList<double>{allegro, allegro, andante, andante};
        QTest::newRow("rit starts at held tempo")
            << QStringLiteral("c4\\allegro c4\\rit c4 c4\\spanend | c1")
            << QList<double>{allegro, 0.50079375, 0.640427, 0.769231, 2.4};
        QTest::newRow("rit after explicit step ending starts at base tempo")
            << QStringLiteral("c4\\allegro c4\\spanend\\rit c4 c4\\spanend | c1")
            << QList<double>{allegro, 0.651032, 0.832555, 1.0, 2.4};
        QTest::newRow("historical spanend atempo keeps final slow note")
            << QStringLiteral("c4\\rit c4 c4\\spanend\\atempo c4")
            << QList<double>{0.651032, 0.832555, 1.0, 0.6};
        QTest::newRow("gradual ending hands off to step at onset")
            << QStringLiteral("c4\\rit c4 c4\\spanend\\andante c4")
            << QList<double>{0.651032, 0.832555, andante, andante};
    }

    void tempoTransitions()
    {
        QFETCH(QString, notes);
        QFETCH(QList<double>, durations);
        const auto doc = io::loadBytes("song.toml", song(notes));
        QVERIFY(doc);
        QCOMPARE(countBySeverity(validate(*doc), Severity::Error), 0);
        const auto plan = buildPlan(*doc);
        QCOMPARE(plan.notes.size(), durations.size());
        double onset = 0.0;
        for (int i = 0; i < durations.size(); ++i) {
            QVERIFY(qAbs(plan.notes[i].startSeconds - onset) < 1e-9);
            onset += durations[i];
            QVERIFY(qAbs(plan.notes[i].endSeconds - onset) < 1e-9);
        }
        QVERIFY(qAbs(plan.totalSeconds - onset) < 1e-9);
    }

    void restsChordsAndTupletsShareTheTempoTimeline()
    {
        const auto doc = io::loadBytes("song.toml", song(QStringLiteral(
            "c4\\allegro {3 r8 d8\\andante e8} <f a>4 s4\\spanend | c1")));
        QVERIFY(doc);
        const auto plan = buildPlan(*doc);
        QCOMPARE(plan.notes.size(), 6);
        constexpr double allegro = 0.461538;
        constexpr double andante = 0.705882;
        QVERIFY(qAbs(plan.notes[1].startSeconds - 4 * allegro / 3) < 1e-9);
        QVERIFY(qAbs(plan.notes[2].startSeconds - (4 * allegro + andante) / 3) < 1e-9);
        QCOMPARE(plan.notes[3].startSeconds, plan.notes[4].startSeconds); // chord
        const double measureEnd = (4 * allegro + 8 * andante) / 3;
        QVERIFY(qAbs(plan.notes.last().startSeconds - measureEnd) < 1e-9);
        QVERIFY(qAbs(plan.totalSeconds - measureEnd - 2.4) < 1e-9);
    }

    void authoredLeadOwnsStepMarksEvenWhenMuted()
    {
        auto doc = io::loadBytes("song.toml", fixtures::ttbbTempo());
        QVERIFY(doc);
        doc->tempoBpm.set(100);
        doc->part(u"Lead")->notes.set(QStringLiteral("c4 c4\\allegro c4 c4 | c1"));
        doc->part(u"Lead")->reparse();
        doc->part(u"Second")->notes.set(QStringLiteral("c2\\presto g2 | c2 g2"));
        doc->part(u"Second")->reparse();
        const auto findings = validate(*doc);
        QVERIFY(std::any_of(findings.cbegin(), findings.cend(), [](const Finding &finding) {
            return finding.rule == "R5.3" && finding.message.contains("presto");
        }));
        PlaybackOptions options;
        options.mutedParts = {"Lead"};
        const auto solo = buildPlan(*doc, options);
        const auto ensemble = buildPlan(*doc);
        QCOMPARE(solo.notes.size(), 4);
        QCOMPARE(solo.totalSeconds, ensemble.totalSeconds);
        QVERIFY(qAbs(solo.totalSeconds - (0.6 + 7 * 0.461538)) < 1e-9);
        int n = 0;
        for (const auto &note : ensemble.notes) {
            if (note.partIndex == 0) {
                QCOMPARE(note.startSeconds, solo.notes[n].startSeconds);
                QCOMPARE(note.endSeconds, solo.notes[n].endSeconds);
                ++n;
            }
        }
        options.fromMeasure = 1;
        const auto later = buildPlan(*doc, options);
        QCOMPARE(later.notes.size(), 2);
        QCOMPARE(later.notes[0].startSeconds, solo.notes[2].startSeconds);
        QCOMPARE(later.notes[1].endSeconds, solo.notes[3].endSeconds);
    }
};

QTEST_APPLESS_MAIN(TempoTests)
#include "TempoTests.moc"
