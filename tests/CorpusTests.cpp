// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The gate that matters: every song in a real OP-songs checkout must load, must
// save back byte for byte when nothing was changed, and must survive having
// every one of its note tokens regenerated.
//
// Point OPE_SONGS_DIR at a songs directory to run it; without one the test
// skips, so the suite still passes on a machine that only has this repo.

#include "core/Library.h"
#include "core/Lyrics.h"
#include "core/Song.h"
#include "core/Validator.h"

#include <QTemporaryDir>
#include <QTest>

using namespace ope;

namespace {

QString songsDir()
{
    const QByteArray fromEnvironment = qgetenv("OPE_SONGS_DIR");
    if (!fromEnvironment.isEmpty())
        return QString::fromLocal8Bit(fromEnvironment);
    // The common local layout: OpenPsalm checked out beside this repo.
    const QString sibling = QStringLiteral(OPE_SOURCE_DIR "/../OpenPsalm/songs");
    return Library::looksLikeSongsRoot(sibling) ? sibling : QString();
}

} // namespace

class CorpusTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_root = songsDir();
        if (m_root.isEmpty())
            QSKIP("No songs directory; set OPE_SONGS_DIR to run the corpus tests.");
        m_library.setRoot(m_root);
        m_library.rescan();
        QVERIFY2(!m_library.entries().isEmpty(), "songs directory has no numbered song folders");
    }

    void everySongParses()
    {
        QStringList failures;
        for (const QString &path : allPaths()) {
            if (auto doc = io::load(path); !doc)
                failures.append(doc.error().formatted());
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }

    void savingAnUnmodifiedSongChangesNothing()
    {
        QStringList failures;
        for (const QString &path : allPaths()) {
            const auto doc = io::load(path);
            if (!doc)
                continue;
            const QByteArray written = io::serialize(*doc);
            if (written != doc->originalBytes) {
                failures.append(QStringLiteral("%1: %2 bytes in, %3 out")
                                    .arg(path)
                                    .arg(doc->originalBytes.size())
                                    .arg(written.size()));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }

    void addingThenRemovingAPerPartOverrideRestoresEveryFile()
    {
        // The lyrics panel's two new buttons, run over the whole corpus. Adding
        // a `[parts.X.lyrics.KEY]` block and taking it away again has to leave
        // every one of these files byte for byte as it was.
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QStringList failures;
        int exercised = 0;
        for (const QString &path : allPaths()) {
            auto doc = io::load(path);
            if (!doc || doc->parts.isEmpty() || doc->lyrics.isEmpty())
                continue;
            const QString key = doc->lyrics.constBegin().key();
            Part *part = nullptr;
            for (Part &candidate : doc->parts) {
                if (!candidate.lyrics.contains(key)) {
                    part = &candidate;
                    break;
                }
            }
            if (!part)
                continue;
            const QString partName = part->name;
            SongDocument::setLyric(part->lyrics, key, QStringLiteral("te -- st syl la bles"));

            const QByteArray withOverride = io::serialize(*doc);
            const QString scratch = temp.filePath(QStringLiteral("scratch.toml"));
            if (auto written = io::writeAtomically(scratch, withOverride); !written) {
                failures.append(QStringLiteral("%1: %2").arg(path, written.error()));
                continue;
            }
            auto reloaded = io::load(scratch);
            if (!reloaded) {
                failures.append(QStringLiteral("%1: adding an override broke the file: %2")
                                    .arg(path, reloaded.error().formatted()));
                continue;
            }
            Part *reloadedPart = reloaded->part(partName);
            if (!reloadedPart || !reloadedPart->lyrics.contains(key)) {
                failures.append(QStringLiteral("%1: the override did not survive the round trip")
                                    .arg(path));
                continue;
            }
            reloaded->removePartLyric(*reloadedPart, key);
            if (io::serialize(*reloaded) != doc->originalBytes)
                failures.append(QStringLiteral("%1: removing the override did not restore it")
                                    .arg(path));
            ++exercised;
        }
        QVERIFY2(failures.isEmpty(),
            qPrintable(failures.first(std::min<qsizetype>(10, failures.size())).join(u'\n')));
        QVERIFY(exercised > 0);
    }

    void everyNoteTokenSurvivesRegeneration()
    {
        QStringList failures;
        for (const QString &path : allPaths()) {
            const auto doc = io::load(path);
            if (!doc)
                continue;
            for (const Part &part : doc->parts) {
                NoteStream copy = part.stream;
                for (Measure &measure : copy.measures()) {
                    for (Event &event : measure.events)
                        event.dirty = true;
                }
                const NoteStream again = NoteStream::parse(copy.toSource());
                if (again.measureCount() != part.stream.measureCount()) {
                    failures.append(QStringLiteral("%1 %2: measure count changed")
                                        .arg(path, part.name));
                    continue;
                }
                for (qsizetype m = 0; m < again.measureCount(); ++m) {
                    const QList<Event> &before = part.stream.measures().at(m).events;
                    const QList<Event> &after = again.measures().at(m).events;
                    if (before.size() != after.size()) {
                        failures.append(QStringLiteral("%1 %2 m%3: event count changed")
                                            .arg(path, part.name)
                                            .arg(m + 1));
                        continue;
                    }
                    for (qsizetype e = 0; e < before.size(); ++e) {
                        if (before.at(e).pitches != after.at(e).pitches
                            || !(before.at(e).duration == after.at(e).duration)
                            || before.at(e).kind != after.at(e).kind
                            || before.at(e).dynamic != after.at(e).dynamic
                            || before.at(e).hairpin != after.at(e).hairpin
                            || before.at(e).fermata != after.at(e).fermata
                            || before.at(e).staccato != after.at(e).staccato
                            || before.at(e).accent != after.at(e).accent
                            || before.at(e).marcato != after.at(e).marcato
                            || before.at(e).tie != after.at(e).tie
                            || before.at(e).chorusStart != after.at(e).chorusStart) {
                            failures.append(QStringLiteral("%1 %2 m%3: `%4` became `%5`")
                                                .arg(path, part.name)
                                                .arg(m + 1)
                                                .arg(before.at(e).raw, after.at(e).text()));
                        }
                    }
                }
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.first(std::min<qsizetype>(
                                        10, failures.size())).join(u'\n')));
    }

    void translationsMergeIntoCompleteSongs()
    {
        for (const SongEntry &entry : m_library.entries()) {
            if (entry.translationPaths.isEmpty())
                continue;
            const auto base = io::load(entry.basePath);
            QVERIFY(base.has_value());
            for (const QString &path : entry.translationPaths) {
                const auto overlay = io::load(path);
                QVERIFY(overlay.has_value());
                const SongDocument merged = mergeOverlay(*base, *overlay);
                QVERIFY2(!merged.parts.isEmpty(), qPrintable(path));
                QVERIFY2(!merged.title.valueOr(QString()).isEmpty(), qPrintable(path));
                // A lyrics-only translation must still align against the tune.
                for (const Part &part : merged.parts) {
                    const PartAlignment alignment = alignPart(merged, part);
                    QVERIFY2(alignment.errors.isEmpty(), qPrintable(path));
                }
            }
        }
    }

    void theCorpusHasNoMeasureErrors()
    {
        // Every song in the corpus seeds today, so any E-MEASURE finding here
        // would mean OPE's tick arithmetic disagrees with the seeder's.
        QStringList failures;
        for (const QString &path : allPaths()) {
            const auto doc = io::load(path);
            if (!doc)
                continue;
            const SongDocument &effective = *doc;
            for (const Finding &finding : validate(effective)) {
                if (finding.rule == QLatin1String("E-MEASURE")
                    || finding.rule == QLatin1String("E-MEASURE-COUNT"))
                    failures.append(QStringLiteral("%1: %2").arg(path, finding.formatted()));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }

private:
    [[nodiscard]] QStringList allPaths() const
    {
        QStringList paths;
        for (const SongEntry &entry : m_library.entries()) {
            paths.append(entry.basePath);
            paths.append(entry.translationPaths);
        }
        return paths;
    }

    QString m_root;
    Library m_library;
};

QTEST_APPLESS_MAIN(CorpusTests)
#include "CorpusTests.moc"
