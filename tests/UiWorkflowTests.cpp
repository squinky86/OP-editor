// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "app/Session.h"
#include "core/CorpusSnapshot.h"
#include "ui/CorpusBackupDialog.h"
#include "ui/Dialogs.h"
#include "ui/LyricsPanel.h"
#include "ui/MainWindow.h"
#include "ui/Panels.h"
#include "ui/ScoreView.h"
#include "ui/SongBrowser.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QSpinBox>
#include <QSettings>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTabWidget>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QToolButton>
#include <QUndoStack>
#include <QtMath>

using namespace ope;
using namespace ope::fixtures;
using namespace ope::ui;

namespace {

void write(const QDir &dir, const QString &name, const QByteArray &contents)
{
    QFile file(dir.filePath(name));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

QPlainTextEdit *editorWithText(QWidget &parent, const QString &text)
{
    for (QPlainTextEdit *editor : parent.findChildren<QPlainTextEdit *>()) {
        if (editor->toPlainText() == text)
            return editor;
    }
    return nullptr;
}

QLineEdit *lineEditWithText(QWidget &parent, const QString &text)
{
    for (QLineEdit *editor : parent.findChildren<QLineEdit *>()) {
        if (editor->text() == text)
            return editor;
    }
    return nullptr;
}

} // namespace

class UiWorkflowTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void localCatalogAcceptance()
    {
        const QString catalog = qEnvironmentVariable("OPE_ACCEPTANCE_SONGS_DIR");
        if (catalog.isEmpty())
            QSKIP("Set OPE_ACCEPTANCE_SONGS_DIR for the explicit local song-369 and SATB/translation gate");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir source(catalog);
        const QDir root(temporary.path());
        QMap<QString, QByteArray> originals;
        const QStringList files {"369/song.toml", "8/song.toml", "13/song.toml", "101/song.toml", "103/song.toml", "273/song.toml", "162/song.toml", "162/song_es.toml"};
        for (const auto &name : files) {
            QFile file(source.filePath(name));
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
            const QByteArray bytes = file.readAll();
            originals.insert(name, bytes);
            QVERIFY(root.mkpath(QFileInfo(name).path()));
            write(root, name, bytes);
        }
        for (const auto &name : files) {
            Session session;
            QVERIFY(session.openSong(root.filePath(name)));
            QCOMPARE(session.currentBytes(), originals[name]);
            QVERIFY(session.save());
            const auto before = session.buildPlaybackPlan({});
            session.mutate("Metadata acceptance", [](SongDocument &doc) {
                doc.subtitle.set("Local metadata acceptance");
            });
            QCOMPARE(session.buildPlaybackPlan({}).notes.size(), before.notes.size());
            QVERIFY(session.save());
            QVERIFY(session.openSong(root.filePath(name)));
            const auto after = session.buildPlaybackPlan({});
            QCOMPARE(after.notes.size(), before.notes.size());
            for (int i = 0; i < before.notes.size(); ++i) {
                QCOMPARE(after.notes[i].midiNote, before.notes[i].midiNote);
                QCOMPARE(after.notes[i].startSeconds, before.notes[i].startSeconds);
                QCOMPARE(after.notes[i].endSeconds, before.notes[i].endSeconds);
            }
            write(root, name, originals[name]);
        }

        Session session;
        const QString path = root.filePath("369/song.toml");
        QVERIFY(session.openSong(path));
        const auto &doc = session.effectiveDocument();
        QCOMPARE(doc.measureCount(), 32);
        QCOMPARE(doc.timeSigNumerator.valueOr(0), 3);
        QCOMPARE(doc.timeSigDenominator.valueOr(0), 4);
        QCOMPARE(doc.tempoBpm.valueOr(0), 96);
        QCOMPARE(doc.keySignature.valueOr({}), QStringLiteral("Bb"));
        QCOMPARE(doc.verseCount.valueOr(0), 3);
        const auto breaks = doc.allPhraseBreaks();
        QCOMPARE(breaks.size(), 7);
        for (int i = 0; i < 7; ++i)
            QCOMPARE(breaks.at(i), (PhraseBreak{(i + 1) * 4, 48}));
        QCOMPARE(countBySeverity(session.findings(), Severity::Error), 0);
        QCOMPARE(countBySeverity(session.findings(), Severity::Warning), 0);
        const QStringList voices {"Tenor1", "Tenor2", "Baritone", "Bass"};
        const QList<int> openings {58, 53, 50, 46};
        const QList<int> endings {62, 58, 53, 46};
        const QList<int> low {58, 53, 50, 41};
        const QList<int> high {67, 62, 60, 58};
        const auto ensemble = session.buildPlaybackPlan({});
        for (int i = 0; i < 4; ++i) {
            const Part *part = doc.part(voices[i]);
            QVERIFY(part);
            QCOMPARE(part->stream.measures().first().events.first().pitches.first().midiNote(), openings[i]);
            const int ending = part->stream.measures().last().events.last().pitches.first().midiNote();
            int lowest = 127, highest = 0;
            for (const auto &measure : part->stream.measures()) {
                for (const auto &event : measure.events) {
                    for (const auto &pitch : event.pitches) {
                        lowest = std::min(lowest, pitch.midiNote());
                        highest = std::max(highest, pitch.midiNote());
                    }
                }
            }
            qInfo().noquote() << voices[i] << "opening" << openings[i] << "ending" << ending
                              << "range" << lowest << highest;
            if (ending != endings[i] || lowest != low[i] || highest != high[i])
                qWarning().noquote() << "Catalog differs from the planning snapshot for" << voices[i]
                    << "— preservation checked against the supplied source; no catalog repair applied.";
            QCOMPARE(session.alignment(voices[i]).sections.size(), 3);
        }
        for (int mask = 0; mask < 16; ++mask) {
            PlaybackOptions options;
            for (int i = 0; i < 4; ++i) {
                if (!(mask & (1 << i)))
                    options.mutedParts.append(voices[i]);
            }
            const auto plan = session.buildPlaybackPlan(options);
            for (const auto &note : plan.notes) {
                QVERIFY(!options.mutedParts.contains(doc.parts[note.partIndex].name));
                const auto original = std::find_if(ensemble.notes.cbegin(), ensemble.notes.cend(), [&](const auto &n) {
                    return n.partIndex == note.partIndex && n.startTick == note.startTick;
                });
                QVERIFY(original != ensemble.notes.cend());
                QCOMPARE(note.midiNote, original->midiNote);
                QCOMPARE(note.startSeconds, original->startSeconds);
            }
        }
        ScoreView score(&session);
        score.setShowAllVerses(true);
        score.resize(1280, 960);
        score.show();
        QCoreApplication::processEvents();
        score.viewport()->grab();
        const QString review = qEnvironmentVariable("OPE_REVIEW_DIR");
        if (!review.isEmpty()) {
            QVERIFY(QDir().mkpath(review));
            QVERIFY(score.grab().save(QDir(review).filePath("369-score-all-verses.png")));
        }
        QVERIFY(!score.eventBoxes().isEmpty());
        session.setSelection({0, 0, 0});
        QTest::keyClick(&score, Qt::Key_Up);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), originals["369/song.toml"]);
        score.viewport()->grab();
        const auto beforeZoom = score.eventBoxes().first().head;
        QWheelEvent wheel(QPointF(200, 100), score.mapToGlobal(QPoint(200, 100)),
            QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(score.viewport(), &wheel);
        score.viewport()->grab();
        QVERIFY(score.eventBoxes().first().head != beforeZoom);
        score.verticalScrollBar()->setValue(score.verticalScrollBar()->maximum());
        score.viewport()->grab();
        QVERIFY(std::any_of(score.eventBoxes().cbegin(), score.eventBoxes().cend(), [](const EventBox &b) {
            return b.measureIndex == 31;
        }));
        if (!review.isEmpty())
            QVERIFY(score.grab().save(QDir(review).filePath("369-score-final-zoomed.png")));

        MainWindow window;
        window.openPath(path);
        window.show();
        auto *inspector = window.findChild<InspectorPanel *>();
        auto *details = window.findChild<QTabWidget *>("detailsTabs");
        QVERIFY(inspector && details);
        details->setCurrentWidget(inspector);
        auto *selector = inspector->findChild<QComboBox *>("inspectorPart");
        QVERIFY(selector);
        selector->setCurrentIndex(0);
        for (const QSize &size : {QSize(760, 760), QSize(1440, 1000)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QCOMPARE(window.size(), size);
            if (!review.isEmpty())
                QVERIFY(window.grab().save(QDir(review).filePath(QStringLiteral("369-window-%1.png").arg(size.width()))));
        }
        // The acceptance run reads the live catalog but writes only disposable copies.
        for (const auto &name : files) {
            QFile file(source.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), originals[name]);
        }
    }

    void inspectorTempoMarksSaveUndoAndSynchronizeSource()
    {
        QTemporaryDir dir;
        const QByteArray bytes = "title = 'Tempo editing'\ntempo_bpm = 100\n"
            "[parts.Solo]\nnotes = \"\"\"\n"
            "c'4@c%f\\\\rit\\\\< d'4 e'4 f'4\\\\! | c'1\n\"\"\"\n";
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        InspectorPanel inspector(&session);
        SourcePanel source(&session);
        auto *mark = inspector.findChild<QComboBox *>("noteTempoMark");
        auto *end = inspector.findChild<QCheckBox *>("noteTempoEnd");
        auto *editor = source.findChild<QPlainTextEdit *>();
        QVERIFY(mark && end && editor);
        QVERIFY(!mark->isEnabled() && !end->isEnabled());
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        session.setSelection({0, 0, 0});
        QCOMPARE(mark->currentData().toString(), QStringLiteral("rit"));
        QCOMPARE(session.currentBytes(), bytes);
        for (const QString &name : {QStringLiteral("largo"), QStringLiteral("lento"),
                 QStringLiteral("adagio"), QStringLiteral("andante"), QStringLiteral("moderato"),
                 QStringLiteral("allegretto"), QStringLiteral("allegro"), QStringLiteral("vivace"),
                 QStringLiteral("presto")}) {
            const int index = mark->findData(name);
            QVERIFY(index > 0);
            mark->setCurrentIndex(index);
            QCOMPARE(session.selectedEvent()->tempoSpanner, name);
            QCOMPARE(session.selectedEvent()->dynamic, QStringLiteral("f"));
            QCOMPARE(session.selectedEvent()->hairpin, QStringLiteral("crescendo"));
            QVERIFY(session.selectedEvent()->chorusStart);
            QCOMPARE(editor->toPlainText().toUtf8(), session.currentBytes());
            const auto reloaded = io::loadBytes("song.toml", session.currentBytes());
            QVERIFY(reloaded);
            QCOMPARE(countBySeverity(validate(*reloaded), Severity::Error), 0);
        }
        session.setSelection({0, 0, 2});
        QCOMPARE(mark->currentIndex(), 0);
        mark->setCurrentIndex(mark->findData("andante"));
        end->setChecked(true);
        QVERIFY(session.selectedEvent()->spannerEnd);
        const QByteArray changed = session.currentBytes();
        session.undoStack()->undo();
        QVERIFY(!end->isChecked());
        session.undoStack()->redo();
        QVERIFY(end->isChecked());
        QCOMPARE(session.currentBytes(), changed);
        while (session.undoStack()->canUndo())
            session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), bytes);
        while (session.undoStack()->canRedo())
            session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), changed);
        QVERIFY(session.save());
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        session.setSelection({0, 0, 2});
        QCOMPARE(mark->currentData().toString(), QStringLiteral("andante"));
        QVERIFY(end->isChecked());
        editor->setPlainText(QString::fromUtf8(changed).replace("andante", "allegretto"));
        QVERIFY(source.commitPendingEdits());
        QCOMPARE(mark->currentData().toString(), QStringLiteral("allegretto"));
        // Removing a start leaves the terminator in place, and can be undone.
        mark->setCurrentIndex(0);
        QVERIFY(session.selectedEvent()->tempoSpanner.isEmpty());
        QVERIFY(session.selectedEvent()->spannerEnd);
        session.undoStack()->undo();
        QCOMPARE(mark->currentData().toString(), QStringLiteral("allegretto"));
        session.setSelection({0, 0, -1});
        QVERIFY(!mark->isEnabled() && !end->isEnabled());
    }

    void inspectorTempoEditMaterializesOnlyTheInheritedVoice()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, "song.toml", ttbbTempo());
        const QByteArray overlay = "title = 'Translation'\n";
        write(root, "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        InspectorPanel inspector(&session);
        session.setSelection({1, 0, 0});
        auto *mark = inspector.findChild<QComboBox *>("noteTempoMark");
        QVERIFY(mark);
        QCOMPARE(mark->currentData().toString(), QStringLiteral("rit"));
        mark->setCurrentIndex(mark->findData("allegro"));
        QCOMPARE(session.selectedEvent()->tempoSpanner, QStringLiteral("allegro"));
        const QByteArray changed = session.currentBytes();
        QVERIFY(changed.contains("[parts.Lead]"));
        QVERIFY(changed.contains("allegro"));
        QVERIFY(!changed.contains("[parts.Second]"));
        QVERIFY(!changed.contains("choral_type"));
        QVERIFY(!changed.contains("tempo_bpm"));
        QCOMPARE(session.currentBytes(QStringLiteral("en")), ttbbTempo());
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
        QCOMPARE(mark->currentData().toString(), QStringLiteral("rit"));
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), changed);
        QVERIFY(session.save());
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        session.setSelection({1, 0, 0});
        QCOMPARE(session.selectedEvent()->tempoSpanner, QStringLiteral("allegro"));
    }

    void tempoControlsFitAndRespectInvalidSourceDrafts()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", R"TOML(title = 'Tempo marks'
tempo_bpm = 100
[parts.Solo]
notes = '''c'4@c%f\allegro d'4 e'4 f'4 | r4\andante c'4 c'4 c'4\spanend'''
)TOML");
        QSettings().remove(QStringLiteral("workspace"));
        MainWindow window;
        window.openPath(dir.filePath("song.toml"));
        window.show();
        auto *session = window.findChild<Session *>();
        auto *inspector = window.findChild<InspectorPanel *>();
        auto *tabs = window.findChild<QTabWidget *>("detailsTabs");
        auto *source = window.findChild<SourcePanel *>();
        QVERIFY(session && inspector && tabs && source);
        tabs->setCurrentWidget(inspector);
        session->setSelection({0, 0, 0});
        auto *mark = inspector->findChild<QComboBox *>("noteTempoMark");
        auto *end = inspector->findChild<QCheckBox *>("noteTempoEnd");
        QVERIFY(mark && end);
        const QString review = qEnvironmentVariable("OPE_REVIEW_DIR");
        for (const QSize &size : {QSize(760, 760), QSize(1440, 1000)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QCOMPARE(window.size(), size);
            QVERIFY(mark->isVisibleTo(&window) && end->isVisibleTo(&window));
            QVERIFY(inspector->rect().contains(mark->mapTo(inspector, mark->rect().bottomRight())));
            QVERIFY(inspector->rect().contains(end->mapTo(inspector, end->rect().bottomRight())));
            if (!review.isEmpty()) {
                QVERIFY(QDir().mkpath(review));
                QVERIFY(window.grab().save(QDir(review).filePath(QString("tempo-%1.png").arg(size.width()))));
            }
        }
        auto *editor = source->findChild<QPlainTextEdit *>();
        editor->setPlainText("title = 'unfinished\n");
        QVERIFY(!source->commitPendingEdits());
        QVERIFY(!mark->isEnabled() && !end->isEnabled());
        source->discardPendingEdits();
        QVERIFY(mark->isEnabled() && end->isEnabled());
        QCOMPARE(mark->currentData().toString(), QStringLiteral("allegro"));
    }

    void inspectorRetainsUnknownMetadataAndEditsOnlyTheChosenField()
    {
        QTemporaryDir dir;
        QByteArray bytes = ttbbSong();
        bytes.replace("choral_type = \"tenor1\"", "choral_type = \" CuStOm \"");
        const int start = bytes.indexOf("[parts.Tenor1]");
        bytes.replace(bytes.indexOf("clef = \"tenor\"", start), 14, "clef = \"C\"");
        bytes.insert(bytes.indexOf("notes =", start), "splice_lyrics_into = \" FutureRole \"\n");
        bytes.replace(bytes.indexOf("staff_number = 1", start), 16, "staff_number = -42");
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        InspectorPanel panel(&session);
        session.setSelection({3, 0, 0});
        auto *clef = panel.findChild<QComboBox *>("partClef");
        auto *role = panel.findChild<QComboBox *>("partRole");
        auto *splice = panel.findChild<QComboBox *>("partSplice");
        auto *staff = panel.findChild<QLineEdit *>("partStaff");
        QVERIFY(clef && role && splice && staff);
        QVERIFY(clef->currentText().contains("C"));
        QCOMPARE(clef->currentData().toString(), QStringLiteral("c"));
        QCOMPARE(role->currentData().toString(), QStringLiteral("custom"));
        QCOMPARE(splice->currentData().toString(), QStringLiteral("futurerole"));
        QCOMPARE(staff->text(), QStringLiteral("-42"));
        QCOMPARE(session.currentBytes(), bytes);
        staff->setText("42");
        QMetaObject::invokeMethod(staff, "editingFinished");
        QByteArray expected = bytes;
        expected.replace("staff_number = -42", "staff_number = 42");
        QCOMPARE(session.currentBytes(), expected);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), bytes);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), expected);
        session.setSelection({0, 0, 0});
        session.setSelection({3, 0, 0});
        QCOMPARE(clef->currentData().toString(), QStringLiteral("c"));
        QVERIFY(clef->currentText().contains("unrecognized"));
        QCOMPARE(session.currentBytes(), expected);
    }

    void inspectorChoicesPreserveOmissionNormalizationAndSoundingPitch()
    {
        QTemporaryDir dir;
        QByteArray bytes = ttbbSong();
        bytes.replace("\"tenor1\"", "\" TeNoR1 \"");
        bytes.replace("\"tenor\"", "\" TeNoR \"");
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        InspectorPanel panel(&session);
        session.setSelection({3, 0, 0});
        auto *clef = panel.findChild<QComboBox *>("partClef");
        auto *role = panel.findChild<QComboBox *>("partRole");
        QVERIFY(clef && role);
        QCOMPARE(clef->currentData().toString(), QStringLiteral("tenor"));
        QCOMPARE(role->currentText(), QStringLiteral("Tenor I"));
        panel.refresh();
        QCOMPARE(session.currentBytes(), bytes);
        QCOMPARE(session.undoStack()->count(), 0);
        for (const QString &value : validClefs()) {
            clef->setCurrentIndex(clef->findData(value));
            QCOMPARE(voicing::normalize(session.effectiveDocument().parts[3].clef.valueOr({})), value);
            QCOMPARE(session.effectiveDocument().parts[3].stream.measures()[0].events[0].pitches[0].midiNote(), 58);
            QCOMPARE(session.effectiveDocument().parts[3].choralType.valueOr({}), QStringLiteral(" TeNoR1 "));
        }
        for (const QString &value : validChoralTypes()) {
            role->setCurrentIndex(role->findData(value));
            QMetaObject::invokeMethod(role, "activated", Q_ARG(int, role->currentIndex()));
            QCOMPARE(voicing::normalize(session.effectiveDocument().parts[3].choralType.valueOr({})), value);
        }
        while (session.undoStack()->canUndo())
            session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), bytes);
        // Source drafts refresh the Inspector even for unsupported values.
        QByteArray draft = bytes;
        draft.replace("\" TeNoR \"", "\"unsupported\"");
        QVERIFY(session.replaceSource(session.currentLanguage(), draft));
        QVERIFY(clef->currentText().contains("unsupported"));
        QCOMPARE(session.currentBytes(), draft);
    }

    void inspectorCanClearAStaffAndKeepsOmittedFieldsAbsent()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        InspectorPanel panel(&session);
        panel.show();
        session.setSelection({3, 0, 0});
        auto *staff = panel.findChild<QLineEdit *>("partStaff");
        QVERIFY(staff);
        staff->setFocus();
        staff->selectAll();
        QTest::keyClick(staff, Qt::Key_Backspace);
        QTest::keyClick(staff, Qt::Key_Return);
        QVERIFY(!session.document().parts[3].staffNumber.present());
        QCOMPARE(voicing::staves(session.effectiveDocument()).size(), 3);
        const QByteArray cleared = session.currentBytes();
        panel.refresh();
        QCOMPARE(session.currentBytes(), cleared);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), ttbbSong());
    }

    void inspectorEditsAnInheritedClefWithoutCopyingTheTune()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, "song.toml", ttbbSong());
        const QByteArray overlay = "title = \"Traducción\"\n";
        write(root, "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        InspectorPanel panel(&session);
        session.setSelection({3, 0, 0});
        auto *clef = panel.findChild<QComboBox *>("partClef");
        QVERIFY(clef);
        QCOMPARE(session.currentBytes(), overlay);
        clef->setCurrentIndex(clef->findData("alto"));
        const QByteArray changed = session.currentBytes();
        QVERIFY(changed.contains("[parts.Tenor1]\nclef = \"alto\""));
        QVERIFY(!changed.contains("notes ="));
        QVERIFY(!changed.contains("choral_type"));
        QVERIFY(!changed.contains("staff_number"));
        QCOMPARE(session.effectiveDocument().parts[3].stream.measures()[0].events[0].pitches[0].midiNote(), 58);
        QVERIFY(session.effectiveDocument().parts[3].notesInherited);
        QCOMPARE(countBySeverity(session.findings(), Severity::Error), 1); // partner unchanged
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
        session.undoStack()->redo();
        QVERIFY(session.save());
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        QCOMPARE(session.currentBytes(), changed);
        QCOMPARE(io::serialize(*session.baseDocument()), ttbbSong());
    }

    void inheritedPitchEditingMaterializesOnlyThatVoiceAndUndoes()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, "song.toml", ttbbSong());
        const QByteArray overlay = "title = \"Traducción\"\n";
        write(root, "song_es.toml", overlay);
        Session session;
        QVERIFY(session.openSong(root.filePath("song_es.toml")));
        ScoreView score(&session);
        session.setSelection({3, 0, 0});
        QTest::keyClick(&score, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(session.selectedEvent()->pitches[0].midiNote(), 70);
        const QByteArray changed = session.currentBytes();
        QVERIFY(changed.contains("[parts.Tenor1]"));
        QVERIFY(changed.contains("notes ="));
        QVERIFY(!changed.contains("clef ="));
        QVERIFY(!changed.contains("choral_type"));
        QVERIFY(!changed.contains("[parts.Tenor2]"));
        QCOMPARE(session.effectiveDocument().parts[1].stream.measures()[0].events[0].pitches[0].midiNote(), 53);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), overlay);
        QCOMPARE(session.selectedEvent()->pitches[0].midiNote(), 58);
        session.undoStack()->redo();
        QCOMPARE(session.currentBytes(), changed);
    }

    void ttbbCreationAndTransportKeepAllVoiceIdentities()
    {
        QTemporaryDir dir;
        Library library;
        library.setRoot(dir.path());
        NewSongDialog dialog(&library);
        auto *preset = dialog.findChild<QComboBox *>("newSongArrangement");
        QVERIFY(preset);
        QCOMPARE(preset->currentData().toString(), QStringLiteral("satb"));
        QCOMPARE(dialog.buildDocument().parts[2].choralType.valueOr({}), QStringLiteral("tenor"));
        preset->setCurrentIndex(preset->findData("ttbb"));
        auto *numerator = dialog.findChild<QSpinBox *>("newSongNumerator");
        QVERIFY(numerator);
        numerator->setValue(3);
        const auto doc = dialog.buildDocument();
        const QStringList names {"Tenor1", "Tenor2", "Baritone", "Bass"};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(doc.parts[i].name, names.at(i));
            QCOMPARE(doc.parts[i].staffNumber.valueOr(0), i < 2 ? 1 : 2);
            QCOMPARE(doc.parts[i].clef.valueOr({}), i < 2 ? QStringLiteral("tenor") : QStringLiteral("bass"));
            for (const auto &measure : doc.parts[i].stream.measures())
                QCOMPARE(measure.playedTicks(), 144);
        }
        const QByteArray bytes = io::serialize(doc);
        QVERIFY(!bytes.contains("arrangement ="));
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        TransportBar transport(&session);
        const auto checks = transport.findChildren<QCheckBox *>();
        QCOMPARE(checks.size(), 4);
        const QStringList labels {"T1", "T2", "Bar", "Bass"};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(checks.at(i)->text(), labels.at(i));
            QCOMPARE(checks.at(i)->property("partName").toString(), names.at(i));
            QVERIFY(checks.at(i)->accessibleName().contains(names.at(i)));
        }
        for (int mask = 0; mask < 16; ++mask) {
            int count = 0;
            for (int i = 0; i < 4; ++i) {
                const bool audible = mask & (1 << i);
                checks.at(i)->setChecked(audible);
                count += audible;
            }
            QCOMPARE(session.buildPlaybackPlan(transport.options()).notes.size(), 3 * count);
        }
        for (int i = 0; i < 4; ++i)
            checks[i]->setChecked(i == 0);
        transport.refresh();
        QCOMPARE(transport.options().mutedParts.size(), 3);
        QVERIFY(!transport.options().mutedParts.contains("Tenor1"));
        preset->setCurrentIndex(preset->findData("single"));
        QCOMPARE(dialog.buildDocument().parts.size(), 1);
        QCOMPARE(dialog.buildDocument().parts.first().name, QStringLiteral("Soprano"));
    }

    void scoreUsesSoundingClefGeometry_data()
    {
        QTest::addColumn<QString>("clef");
        QTest::addColumn<double>("spacesBelowTop");
        QTest::newRow("treble C4 below staff") << QStringLiteral("treble") << 5.0;
        QTest::newRow("bass C4 above staff") << QStringLiteral("bass") << -1.0;
        QTest::newRow("octave treble C4") << QStringLiteral("treble_8") << 1.5;
        QTest::newRow("alto middle C") << QStringLiteral("alto") << 2.0;
        QTest::newRow("tenor fourth line C") << QStringLiteral("tenor") << 1.0;
    }

    void scoreUsesSoundingClefGeometry()
    {
        QFETCH(QString, clef);
        QFETCH(double, spacesBelowTop);
        QTemporaryDir dir;
        const QByteArray bytes = "title = \"Geometry\"\n[parts.Solo]\nclef = \"" + clef.toUtf8() + "\"\nnotes = \"c'4 d'4 e'4 f'4\"\n";
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        ScoreView score(&session);
        score.resize(700, 400);
        score.show();
        QCoreApplication::processEvents();
        const QImage image = score.viewport()->grab().toImage();
        QVERIFY(!score.eventBoxes().isEmpty());
        const auto box = score.eventBoxes().first();
        const auto staff = score.systems().first().staves.first();
        QCOMPARE(box.head.y(), staff.top + spacesBelowTop * 7.5);
        QCOMPARE(box.stemUp, spacesBelowTop > 2.0); // free stems about the middle line
        QVERIFY(!box.stem.isNull());
        QCOMPARE(qAbs(box.stem.dy()), 24.75);
        QVERIFY(box.stem.p2().y() >= 0); // the staff reserves room for high notes
        if (clef == "treble" || clef == "bass") {
            // A half-pixel staff offset can put the antialiased stroke in the
            // neighboring row. Sample its width, beyond the notehead itself.
            const qreal ratio = image.devicePixelRatio();
            const int x = qRound((box.head.x() + 6.5) * ratio);
            int darkest = 255;
            for (int y = qFloor((box.head.y() - 0.5) * ratio);
                 y <= qCeil((box.head.y() + 0.5) * ratio); ++y)
                darkest = std::min(darkest, image.pixelColor(x, y).red());
            QVERIFY(darkest < 200); // the C4 ledger line is visible
        }
        QTest::mouseClick(score.viewport(), Qt::LeftButton, Qt::NoModifier, box.head.toPoint());
        QCOMPARE(session.selection().partIndex, 0);
        QCOMPARE(session.selectedEvent()->pitches[0].midiNote(), 60);
        QCOMPARE(session.currentBytes(), bytes);
    }

    void sharedUnisonsCrossingsAndVisibleNavigationEditTheIntendedVoice()
    {
        QTemporaryDir dir;
        QByteArray bytes = ttbbSong();
        bytes.replace("bes2 c'4 | d'2.", "c'4 bes4 a4 | d'2.");
        bytes.replace("f2 a4 | bes2.", "c'4 d'4 e'4 | bes2.");
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        ScoreView score(&session);
        score.resize(1000, 800);
        score.show();
        QCoreApplication::processEvents();
        score.viewport()->grab();
        const auto findBox = [&](int part, int event) {
            for (const auto &box : score.eventBoxes()) {
                if (box.partIndex == part && box.measureIndex == 0 && box.eventIndex == event)
                    return box;
            }
            return EventBox{};
        };
        const auto up = findBox(3, 0);
        const auto down = findBox(1, 0);
        QCOMPARE(up.head, down.head);
        QVERIFY(up.stemUp);
        QVERIFY(!down.stemUp);
        // Each stem remains a separate editing target at a shared unison.
        QTest::mouseClick(score.viewport(), Qt::LeftButton, Qt::NoModifier, up.stem.pointAt(0.8).toPoint());
        QCOMPARE(session.selection().partIndex, 3);
        QTest::mouseClick(score.viewport(), Qt::LeftButton, Qt::NoModifier, down.stem.pointAt(0.8).toPoint());
        QCOMPARE(session.selection().partIndex, 1);
        QTest::mouseClick(score.viewport(), Qt::LeftButton, Qt::NoModifier, up.head.toPoint());
        QCOMPARE(session.selection().partIndex, 3); // repeated heads cycle voices
        QTest::keyClick(&score, Qt::Key_Up);
        QCOMPARE(session.selectedEvent()->pitches[0].midiNote(), 62);
        QCOMPARE(session.effectiveDocument().parts[1].stream.measures()[0].events[0].pitches[0].midiNote(), 60);
        session.undoStack()->undo();
        QCOMPARE(session.currentBytes(), bytes);
        for (int index : {1, 2, 0}) {
            QTest::keyClick(&score, Qt::Key_Down, Qt::AltModifier);
            QCOMPARE(session.selection().partIndex, index);
        }
        score.viewport()->grab();
        QVERIFY(findBox(3, 2).stemUp); // lower pitch still the upper voice
        QVERIFY(!findBox(1, 2).stemUp);
        QVERIFY(findBox(3, 2).head.y() > findBox(1, 2).head.y());
        score.setShowAllVerses(true);
        score.viewport()->grab();
        QVERIFY(score.systems().first().rulerTop > score.systems().first().staves.last().lyricTop);
    }

    void unsupportedAndConflictingClefsNeverRenderAsTreble()
    {
        QTemporaryDir dir;
        QByteArray bytes = ttbbSong();
        bytes.replace("clef = \"tenor\"", "clef = \"C\"");
        write(QDir(dir.path()), "song.toml", bytes);
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        ScoreView score(&session);
        InspectorPanel inspector(&session);
        score.resize(700, 500);
        score.show();
        QCoreApplication::processEvents();
        score.viewport()->grab();
        QVERIFY(!score.systems().first().staves.first().clef);
        QVERIFY(score.systems().first().staves.first().problem.contains("unsupported"));
        for (const auto &box : score.eventBoxes())
            QVERIFY(box.partIndex != 1 && box.partIndex != 3);
        auto *parts = inspector.findChild<QComboBox *>("inspectorPart");
        QVERIFY(parts);
        parts->setCurrentIndex(parts->findData(3));
        QCOMPARE(session.selection().partIndex, 3);
        QCOMPARE(session.currentBytes(), bytes);
    }

    void pendingLyricsSurviveRoleReorderingAndLabelRefresh()
    {
        QTemporaryDir dir;
        write(QDir(dir.path()), "song.toml", ttbbSong());
        Session session;
        QVERIFY(session.openSong(dir.filePath("song.toml")));
        LyricsPanel lyrics(&session);
        auto *editor = editorWithText(lyrics, "one two three");
        QVERIFY(editor);
        editor->setPlainText("new words here");
        session.mutate("Change role", [](SongDocument &doc) {
            doc.part(u"Tenor1")->choralType.set("bass");
        });
        QVERIFY(editorWithText(lyrics, "new words here"));
        lyrics.commitPendingEdits();
        QCOMPARE(session.effectiveDocument().lyrics["1"].rawText, QStringLiteral("new words here"));
        QCOMPARE(session.effectiveDocument().parts[3].name, QStringLiteral("Tenor1"));
    }

    void lyricsTabAddsVerseAndPreservesTyping()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        LyricsPanel panel(&session);
        auto *add = panel.findChild<QToolButton *>(QStringLiteral("addVerseButton"));
        QVERIFY(add);
        QVERIFY(!add->isEnabled());
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        QVERIFY(add->isEnabled());
        panel.resize(420, 320);
        panel.show();

        auto *first = editorWithText(panel, QStringLiteral("one two"));
        QVERIFY(first);
        first->setPlainText(QStringLiteral("new words"));
        QVERIFY(panel.hasPendingEdits());
        auto *tabs = panel.findChild<QTabWidget *>();
        tabs->setCurrentIndex(1);
        QTest::mouseClick(add, Qt::LeftButton);
        QCoreApplication::processEvents();

        QCOMPARE(session.document().verseCount.valueOr(0), 5);
        QVERIFY(session.document().lyrics.contains(QStringLiteral("5")));
        QCOMPARE(session.document().lyrics.value(QStringLiteral("1")).rawText,
            QStringLiteral("new words"));
        QVERIFY(!panel.hasPendingEdits());
        QCOMPARE(tabs->currentIndex(), 0);
        auto *editor = qobject_cast<QPlainTextEdit *>(panel.focusWidget());
        QVERIFY(editor);
        QVERIFY(editor->toPlainText().isEmpty());
        auto *scroll = panel.findChild<QScrollArea *>();
        QVERIFY(scroll->viewport()->rect().intersects(
            QRect(editor->mapTo(scroll->viewport(), QPoint()), editor->size())));

        session.undoStack()->undo();
        QCOMPARE(session.document().verseCount.valueOr(0), 4);
        QVERIFY(!session.document().lyrics.contains(QStringLiteral("5")));
        QCOMPARE(session.document().lyrics.value(QStringLiteral("1")).rawText,
            QStringLiteral("new words"));
        session.undoStack()->redo();
        QVERIFY(session.document().lyrics.contains(QStringLiteral("5")));
        QVERIFY(session.currentBytes().contains("[lyrics.5]"));
    }

    void lyricsTabNumbersNewVerses_data()
    {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<int>("expected");
        QTest::newRow("empty") << QByteArray("title = \"Empty\"\n") << 1;
        QTest::newRow("declared") << baseSong() << 5;
        QTest::newRow("existing")
            << baseSong() + "\n[lyrics.7]\ntext = \"existing words\"\n" << 8;
        QTest::newRow("voice-only")
            << baseSong() + "\n[parts.Alto.lyrics.8]\ntext = \"alto words\"\n" << 9;
    }

    void lyricsTabNumbersNewVerses()
    {
        QFETCH(QByteArray, source);
        QFETCH(int, expected);
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), source);
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);
        panel.findChild<QToolButton *>(QStringLiteral("addVerseButton"))->click();
        QCOMPARE(session.document().verseCount.valueOr(0), expected);
        QVERIFY(session.document().lyrics.contains(QString::number(expected)));
    }

    void lyricsTabAddsVerseToTranslation()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        write(root, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        session.setCurrentLanguage(QStringLiteral("es"));
        LyricsPanel panel(&session);
        panel.findChild<QToolButton *>(QStringLiteral("addVerseButton"))->click();
        QCOMPARE(session.effectiveDocument().verseCount.valueOr(0), 5);
        QCOMPARE(session.effectiveDocument().lyrics.value(QStringLiteral("1")).rawText,
            QStringLiteral("one two"));
        QVERIFY(session.document(QStringLiteral("es"))->lyrics.contains(QStringLiteral("5")));
        QCOMPARE(session.document(QStringLiteral("en"))->verseCount.valueOr(0), 4);
        QVERIFY(!session.document(QStringLiteral("en"))->lyrics.contains(QStringLiteral("5")));
    }

    void lyricsTabEditsDefaultVerses()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);

        for (int verse = 1; verse <= 4; ++verse) {
            QCheckBox *box = panel.findChild<QCheckBox *>(
                QStringLiteral("defaultVerseCheckBox_%1").arg(verse));
            QVERIFY(box);
            QVERIFY(box->isChecked());
        }

        QCheckBox *four
            = panel.findChild<QCheckBox *>(QStringLiteral("defaultVerseCheckBox_4"));
        four->click();
        QCOMPARE(session.document().defaultVerses.valueOr({}), QList<int>({ 1, 2, 3 }));
        QVERIFY(session.currentBytes().contains("default_verses = [1, 2, 3]"));

        four = panel.findChild<QCheckBox *>(QStringLiteral("defaultVerseCheckBox_4"));
        QVERIFY(four);
        four->click();
        QVERIFY(!session.document().defaultVerses.present());
        QVERIFY(!session.currentBytes().contains("default_verses"));
    }

    void scoreSelectionHighlightsAndRevealsTheSourceToken()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        QByteArray source;
        for (int line = 0; line < 40; ++line)
            source += "# enough leading source to require scrolling\n";
        source += baseSong();
        write(root, QStringLiteral("song.toml"), source);
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        SourcePanel panel(&session);
        panel.resize(420, 160);
        panel.show();
        QCoreApplication::processEvents();

        QPlainTextEdit *editor = panel.findChild<QPlainTextEdit *>();
        QVERIFY(editor);
        editor->clearFocus();
        QCOMPARE(editor->verticalScrollBar()->value(), 0);

        session.setSelection(Selection { 0, 1, 0 });
        QCoreApplication::processEvents();

        const QList<QTextEdit::ExtraSelection> highlights = editor->extraSelections();
        QCOMPARE(highlights.size(), 1);
        QCOMPARE(highlights.first().cursor.selectedText(), QStringLiteral("g'1"));
        QVERIFY(editor->verticalScrollBar()->value() > 0);
    }

    void sourceValidationDoesNotRestoreTheScoreSelectionWhileTyping()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        SourcePanel panel(&session);
        panel.resize(420, 240);
        panel.show();
        QCoreApplication::processEvents();

        QPlainTextEdit *editor = panel.findChild<QPlainTextEdit *>();
        QVERIFY(editor);

        editor->clearFocus();
        session.setSelection(Selection { 0, 1, 0 });
        QCOMPARE(editor->extraSelections().size(), 1);

        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        QVERIFY(editor->extraSelections().isEmpty());

        QTextCursor cursor = editor->document()->find(QStringLiteral("Face to Face"));
        QVERIFY(!cursor.isNull());
        cursor.clearSelection();
        editor->setTextCursor(cursor);
        QTest::keyClicks(editor, QStringLiteral("!"));
        const int positionAfterFirstEdit = editor->textCursor().position();

        QVERIFY(panel.hasPendingEdits());
        QVERIFY(panel.commitPendingEdits());
        QCOMPARE(editor->textCursor().position(), positionAfterFirstEdit);
        QCOMPARE(editor->textCursor().anchor(), positionAfterFirstEdit);
        QVERIFY(editor->extraSelections().isEmpty());

        // Continuing after validation must append at the typing cursor, not
        // replace the note that remains selected in the score.
        QTest::keyClicks(editor, QStringLiteral("?"));
        QVERIFY(panel.commitPendingEdits());
        QCOMPARE(session.document().title.valueOr(QString()),
            QStringLiteral("Face to Face!?"));
        QCOMPARE(session.document().parts.at(0).stream.measures().at(1).events.at(0).raw,
            QStringLiteral("g'1"));
    }

    void narrowLyricsHeaderStaysOneLineTall()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);
        panel.resize(300, 420);
        panel.show();
        QCoreApplication::processEvents();

        QLabel *legend = panel.findChild<QLabel *>(QStringLiteral("lyricsLegend"));
        QToolButton *add
            = panel.findChild<QToolButton *>(QStringLiteral("addLyricsSectionButton"));
        QTabWidget *tabs = panel.findChild<QTabWidget *>();
        QVERIFY(legend);
        QVERIFY(add);
        QVERIFY(tabs);
        QVERIFY(legend->height() <= add->height());
        QVERIFY(tabs->height() > add->height() * 5);
    }

    void selectedScoreNoteHasAVisibleHighlight()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        ScoreView score(&session);
        score.resize(600, 400);
        score.show();
        QCoreApplication::processEvents();

        const QImage before = score.viewport()->grab().toImage();
        session.setSelection(Selection { 0, 0, 0 });
        QCoreApplication::processEvents();
        const QImage selected = score.viewport()->grab().toImage();

        int changedPixels = 0;
        for (int y = 0; y < selected.height(); ++y) {
            for (int x = 0; x < selected.width(); ++x) {
                if (selected.pixelColor(x, y) != before.pixelColor(x, y))
                    ++changedPixels;
            }
        }
        // The filled selection halo changes a meaningful area around the
        // notehead, rather than relying on a few recoloured glyph pixels.
        QVERIFY(changedPixels > 200);
    }

    void scoreShortcutsToggleExclusiveAccentAndMarcatoFlags()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        ScoreView score(&session);
        session.setSelection(Selection { 0, 0, 0 });

        QTest::keyClick(&score, Qt::Key_A);
        QVERIFY(session.selectedEvent()->accent);
        QVERIFY(!session.selectedEvent()->marcato);
        QVERIFY(session.currentBytes().contains("f'1^"));

        QTest::keyClick(&score, Qt::Key_M);
        QVERIFY(!session.selectedEvent()->accent);
        QVERIFY(session.selectedEvent()->marcato);
        QVERIFY(session.currentBytes().contains("f'1^^"));

        QTest::keyClick(&score, Qt::Key_Return, Qt::ControlModifier);
        const Event &alto
            = session.document().parts.at(1).stream.measures().at(0).events.at(0);
        QVERIFY(alto.marcato);
        QVERIFY(!alto.accent);
        QVERIFY(session.currentBytes().contains("d'1^^"));

        QTest::keyClick(&score, Qt::Key_M);
        QVERIFY(!session.selectedEvent()->accent);
        QVERIFY(!session.selectedEvent()->marcato);
    }

    void editingASelectedNoteKeepsANarrowWindowWidth()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QString path = root.filePath(QStringLiteral("song.toml"));
        write(root, QStringLiteral("song.toml"), baseSong());
        MainWindow window;
        window.openPath(path);
        window.resize(760, 760);
        window.show();
        QCoreApplication::processEvents();

        ScoreView *score = window.findChild<ScoreView *>();
        QLabel *noteInfo
            = window.findChild<QLabel *>(QStringLiteral("selectedNoteInfo"));
        QScrollArea *transport
            = window.findChild<QScrollArea *>(QStringLiteral("transportScroll"));
        SourcePanel *sourcePanel = window.findChild<SourcePanel *>();
        QVERIFY(score);
        QVERIFY(noteInfo);
        QVERIFY(transport);
        QVERIFY(sourcePanel);
        QPlainTextEdit *sourceEditor = sourcePanel->findChild<QPlainTextEdit *>();
        QVERIFY(sourceEditor);
        QCOMPARE(window.width(), 760);
        QVERIFY(transport->horizontalScrollBar()->maximum() > 0);
        QTest::mouseClick(score->viewport(), Qt::LeftButton, Qt::NoModifier,
            score->eventBoxes().first().head.toPoint());
        QVERIFY(noteInfo->text().startsWith(QStringLiteral("token")));
        QCOMPARE(sourceEditor->extraSelections().size(), 1);
        QCOMPARE(sourceEditor->extraSelections().constFirst().cursor.selectedText(),
            QStringLiteral("f'1"));
        const int widthBeforeEdit = window.width();
        QTest::keyClick(score, Qt::Key_Up);
        QCoreApplication::processEvents();

        QCOMPARE(window.width(), widthBeforeEdit);
        QCOMPARE(sourceEditor->extraSelections().size(), 1);
        QCOMPARE(sourceEditor->extraSelections().constFirst().cursor.selectedText(),
            QStringLiteral("g'1"));
    }

    void mainWindowUsesThreeCollapsibleWorkspacePanesAndRightDetailsTabs()
    {
        QSettings().remove(QStringLiteral("workspace"));
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QCoreApplication::processEvents();

        QSplitter *workspace = window.findChild<QSplitter *>(QStringLiteral("workspaceSplitter"));
        QVERIFY(workspace);
        QCOMPARE(workspace->orientation(), Qt::Vertical);
        QCOMPARE(workspace->count(), 3);
        for (const QString &name : { QStringLiteral("score"), QStringLiteral("lyrics"),
                 QStringLiteral("source") }) {
            QToolButton *toggle
                = window.findChild<QToolButton *>(name + QStringLiteral("WorkspaceToggle"));
            QVERIFY(toggle);
            QVERIFY(toggle->isChecked());
        }

        QTabWidget *details = window.findChild<QTabWidget *>(QStringLiteral("detailsTabs"));
        QVERIFY(details);
        QCOMPARE(details->count(), 3);
        QCOMPARE(details->tabText(0), QStringLiteral("Song"));
        QCOMPARE(details->tabText(1), QStringLiteral("Inspector"));
        QVERIFY(details->tabText(2).startsWith(QStringLiteral("Problems")));
        QDockWidget *dock = window.findChild<QDockWidget *>(QStringLiteral("detailsDock"));
        QVERIFY(dock);
        QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);

        QToolButton *sourceToggle
            = window.findChild<QToolButton *>(QStringLiteral("sourceWorkspaceToggle"));
        sourceToggle->click();
        QVERIFY(!sourceToggle->isChecked());
        sourceToggle->click();
        QVERIFY(sourceToggle->isChecked());
    }

    void sourcePanelSynchronizesValidTomlAndRetainsInvalidDrafts()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        HeaderPanel header(&session);
        SourcePanel source(&session);
        ProblemsPanel problems(&session);
        connect(&source, &SourcePanel::sourceErrorChanged,
            &problems, &ProblemsPanel::setSourceError);
        QPlainTextEdit *editor = source.findChild<QPlainTextEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QVERIFY(!editor->document()->findChildren<QSyntaxHighlighter *>().isEmpty());

        QString valid = editor->toPlainText();
        valid.replace(QStringLiteral("Face to Face"), QStringLiteral("Edited in Source"));
        editor->setPlainText(valid);
        QVERIFY(source.hasPendingEdits());
        QVERIFY(source.commitPendingEdits());
        QCOMPARE(session.document().title.valueOr(QString()), QStringLiteral("Edited in Source"));
        QVERIFY(lineEditWithText(header, QStringLiteral("Edited in Source")));

        session.mutate(QStringLiteral("Structured edit"),
            [](SongDocument &doc) { doc.title.set(QStringLiteral("Edited in Song pane")); });
        QVERIFY(editor->toPlainText().contains(QStringLiteral("Edited in Song pane")));

        editor->setPlainText(QStringLiteral("title = \"unfinished\n"));
        QVERIFY(!source.commitPendingEdits());
        QVERIFY(source.hasPendingEdits());
        QVERIFY(source.hasParseError());
        QCOMPARE(session.document().title.valueOr(QString()), QStringLiteral("Edited in Song pane"));
        QTreeWidget *problemTree = problems.findChild<QTreeWidget *>();
        QVERIFY(problemTree);
        QVERIFY(problemTree->topLevelItemCount() > 0);
        QCOMPARE(problemTree->topLevelItem(0)->text(1), QStringLiteral("E-TOML"));

        source.discardPendingEdits();
        QVERIFY(!source.hasPendingEdits());
        QVERIFY(editor->toPlainText().contains(QStringLiteral("Edited in Song pane")));
    }

    void invalidMainWindowSourceDisablesSaveAndRaisesAProblemsError()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        const QString path = root.filePath(QStringLiteral("song.toml"));
        write(root, QStringLiteral("song.toml"), baseSong());
        MainWindow window;
        window.openPath(path);
        SourcePanel *source = window.findChild<SourcePanel *>();
        HeaderPanel *header = window.findChild<HeaderPanel *>();
        QTabWidget *details = window.findChild<QTabWidget *>(QStringLiteral("detailsTabs"));
        QVERIFY(source);
        QVERIFY(header);
        QVERIFY(details);

        QPlainTextEdit *editor = source->findChild<QPlainTextEdit *>();
        editor->setPlainText(QStringLiteral("title = \"unfinished\n"));
        QVERIFY(!source->commitPendingEdits());
        QVERIFY(!header->isEnabled());
        QVERIFY(source->isEnabled());
        QVERIFY(details->tabText(2).startsWith(QStringLiteral("Problems (")));
        QVERIFY(!details->tabToolTip(2).startsWith(QStringLiteral("0 error")));

        QAction *save = nullptr;
        for (QAction *action : window.findChildren<QAction *>()) {
            if (action->shortcut() == QKeySequence::Save) {
                save = action;
                break;
            }
        }
        QVERIFY(save);
        QVERIFY(!save->isEnabled());

        source->discardPendingEdits();
        QVERIFY(header->isEnabled());
        QVERIFY(save->isEnabled());
    }

    void songBrowserSortsNumericIdsDescendingByDefault()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        for (const int id : { 1, 2, 10 }) {
            QVERIFY(root.mkpath(QString::number(id)));
            write(QDir(root.filePath(QString::number(id))), QStringLiteral("song.toml"),
                QStringLiteral("title = \"Song %1\"\nlanguage = \"en\"\n").arg(id).toUtf8());
        }

        Library library;
        library.setRoot(dir.path());
        library.rescan();
        SongBrowser browser(&library);
        QTreeWidget *tree = browser.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QVERIFY(tree->isSortingEnabled());
        QVERIFY(tree->header()->sectionsClickable());
        QCOMPARE(tree->header()->sortIndicatorSection(), 0);
        QCOMPARE(tree->header()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(tree->topLevelItemCount(), 3);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("10"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("2"));
        QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("1"));

        tree->sortItems(0, Qt::AscendingOrder);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("1"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("2"));
        QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("10"));
    }

    void phraseBreakEditKeepsTheAlignmentScrollPosition()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"),
            "title = \"Long alignment\"\n"
            "time_sig_numerator = 4\n"
            "time_sig_denominator = 4\n\n"
            "[parts.Soprano]\n"
            "choral_type = \"soprano\"\n"
            "notes = \"\"\"\n"
            "c'4 d'4 e'4 f'4 | g'4 a'4 b'4 c''4 | c''4 b'4 a'4 g'4 |\n"
            "f'4 e'4 d'4 c'4 | c'4 d'4 e'4 f'4 | g'4 a'4 b'4 c''4\n"
            "\"\"\"\n\n"
            "[lyrics.1]\n"
            "text = \"one two three four five six seven eight nine ten eleven twelve "
            "thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty "
            "twentyone twentytwo twentythree twentyfour\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);
        panel.resize(360, 420);
        panel.show();

        QTabWidget *tabs = panel.findChild<QTabWidget *>();
        QTableWidget *grid = panel.findChild<QTableWidget *>();
        QVERIFY(tabs);
        QVERIFY(grid);
        tabs->setCurrentIndex(1);
        QCoreApplication::processEvents();

        QVERIFY(grid->columnCount() >= 20);
        grid->setCurrentCell(0, 18);
        grid->scrollToItem(grid->item(0, 18));
        QCoreApplication::processEvents();
        const int before = grid->horizontalScrollBar()->value();
        QVERIFY(before > 0);
        QSignalSpy scrollChanges(grid->horizontalScrollBar(), &QScrollBar::valueChanged);

        const QRect breakCell = grid->visualItemRect(grid->item(0, 18));
        QVERIFY(grid->viewport()->rect().contains(breakCell.center()));
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier,
            breakCell.center());
        QCoreApplication::processEvents();

        QVERIFY(session.phraseBreakAt(PhraseBreak { 5, 48 }).has_value());
        QCOMPARE(grid->horizontalScrollBar()->value(), before);
        QCOMPARE(grid->currentColumn(), 18);

        const QRect sameBreakCell = grid->visualItemRect(grid->item(0, 18));
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier,
            sameBreakCell.center());
        QCoreApplication::processEvents();

        QVERIFY(!session.phraseBreakAt(PhraseBreak { 5, 48 }).has_value());
        QCOMPARE(grid->horizontalScrollBar()->value(), before);
        QCOMPARE(grid->currentColumn(), 18);
        bool jumpedToStart = false;
        for (const QList<QVariant> &arguments : scrollChanges)
            jumpedToStart = jumpedToStart || arguments.constFirst().toInt() == 0;
        QVERIFY(!jumpedToStart);
    }

    void restsAppearInTheAlignmentGridAndTakePhraseBreaks()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"),
            "title = \"Rest boundaries\"\n"
            "time_sig_numerator = 4\n"
            "time_sig_denominator = 4\n\n"
            "[parts.Soprano]\n"
            "choral_type = \"soprano\"\n"
            "notes = \"c'2 r8 d'8 e'4\"\n\n"
            "[lyrics.1]\n"
            "text = \"one two three\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);

        QTableWidget *grid = panel.findChild<QTableWidget *>();
        QVERIFY(grid);
        QCOMPARE(grid->columnCount(), 4);
        QCOMPARE(grid->item(1, 0)->text(), QStringLiteral("C4/2"));
        QCOMPARE(grid->item(1, 1)->text(), QStringLiteral("rest/8"));
        QCOMPARE(grid->item(1, 2)->text(), QStringLiteral("D4/8"));
        QCOMPARE(grid->item(2, 0)->text(), QStringLiteral("one"));
        QVERIFY(grid->item(2, 1)->text().isEmpty());
        QCOMPARE(grid->item(2, 2)->text(), QStringLiteral("two"));

        // The note before the rest and the rest itself expose distinct break
        // boundaries, so the latter can place a break after the silent beat.
        const QModelIndex restBreak = grid->model()->index(0, 1);
        QVERIFY(QMetaObject::invokeMethod(grid, "cellClicked", Qt::DirectConnection,
            Q_ARG(int, restBreak.row()), Q_ARG(int, restBreak.column())));
        QVERIFY(session.phraseBreakAt(PhraseBreak { 1, 40 }).has_value());
        QVERIFY(!session.phraseBreakAt(PhraseBreak { 1, 32 }).has_value());

        const QModelIndex precedingNoteBreak = grid->model()->index(0, 0);
        QVERIFY(QMetaObject::invokeMethod(grid, "cellClicked", Qt::DirectConnection,
            Q_ARG(int, precedingNoteBreak.row()), Q_ARG(int, precedingNoteBreak.column())));
        QVERIFY(session.phraseBreakAt(PhraseBreak { 1, 32 }).has_value());
        QVERIFY(session.phraseBreakAt(PhraseBreak { 1, 40 }).has_value());

        // Score double-clicks still address lyric-slot numbers, not the newly
        // expanded visible-column numbers.
        panel.focusSlot(QStringLiteral("Soprano"), 1);
        QCOMPARE(grid->currentColumn(), 2);
    }

    void delayedLyricsCommitStaysWithItsOriginalLanguage()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        write(root, QStringLiteral("song_es.toml"), "title = \"Cara a cara\"\n");
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        LyricsPanel panel(&session);

        QPlainTextEdit *verse = editorWithText(panel, QStringLiteral("one two"));
        QVERIFY(verse);
        verse->setPlainText(QStringLiteral("draft stays in English"));
        QVERIFY(panel.hasPendingEdits());

        // Exercise the dangerous path directly: switch the session without the
        // main window's normal pre-switch flush, then let the timer fire.
        session.setCurrentLanguage(QStringLiteral("es"));
        QTest::qWait(700);

        QCOMPARE(session.document(QStringLiteral("en"))
                     ->lyrics.value(QStringLiteral("1")).rawText,
            QStringLiteral("draft stays in English"));
        QVERIFY(session.document(QStringLiteral("es"))->lyrics.isEmpty());
        QCOMPARE(session.currentLanguage(), QStringLiteral("es"));
    }

    void unrelatedDocumentRefreshDoesNotEraseHeaderTyping()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        Session session;
        QVERIFY(session.openSong(root.filePath(QStringLiteral("song.toml"))));
        HeaderPanel panel(&session);

        QLineEdit *title = lineEditWithText(panel, QStringLiteral("Face to Face"));
        QVERIFY(title);
        title->setText(QStringLiteral("Title still being typed"));
        QVERIFY(panel.hasPendingEdits());

        session.mutate(QStringLiteral("Unrelated change"),
            [](SongDocument &doc) { doc.tempoBpm.set(101); });
        QCOMPARE(title->text(), QStringLiteral("Title still being typed"));

        panel.commitPendingEdits();
        QCOMPARE(session.document().title.valueOr(QString()),
            QStringLiteral("Title still being typed"));
    }

    void translationLanguageCodeMustBeSafeAndUnique()
    {
        QTemporaryDir dir;
        const QDir root(dir.path());
        write(root, QStringLiteral("song.toml"), baseSong());
        const auto loaded = io::load(root.filePath(QStringLiteral("song.toml")));
        QVERIFY(loaded);
        TranslationDialog dialog(*loaded,
            { QStringLiteral("en"), QStringLiteral("es") });
        QComboBox *language = dialog.findChild<QComboBox *>();
        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(language);
        QVERIFY(buttons);

        language->setEditText(QStringLiteral("../escape"));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        language->setEditText(QStringLiteral("es"));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        language->setEditText(QStringLiteral("pt-br"));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(dialog.languageCode(), QStringLiteral("pt-BR"));
    }

    void newSongUsesAnAutomaticLocalDraftSlot()
    {
        QTemporaryDir dir;
        Library library;
        library.setRoot(dir.path());
        library.rescan();
        NewSongDialog dialog(&library);

        bool mentionsLocalDraft = false;
        for (const QLabel *label : dialog.findChildren<QLabel *>()) {
            QVERIFY(label->text() != QStringLiteral("Song number"));
            mentionsLocalDraft |= label->text().contains(QStringLiteral("local draft folder"));
        }
        QVERIFY(mentionsLocalDraft);

        const SongDocument draft = dialog.buildDocument();
        QCOMPARE(draft.workId, 1);
        QCOMPARE(draft.path, QDir(dir.path()).filePath(QStringLiteral("1/song.toml")));
    }

    void corpusUpdateStateIsVisibleWithoutDependingOnColor()
    {
        QTemporaryDir dir;
        Library library;
        library.setRoot(dir.path());
        SongBrowser browser(&library);
        browser.show();
        browser.setManagedCorpusStatus(CorpusUpdateState::UpdateAvailable,
            QStringLiteral("Update available: abc → def • checked today"),
            QStringLiteral("Latest commit: def"), 2);

        QPushButton *update = nullptr;
        QPushButton *backups = nullptr;
        for (QPushButton *button : browser.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Update OP-songs…"))
                update = button;
            if (button->text() == QStringLiteral("Backups (2)…"))
                backups = button;
        }
        QVERIFY(update);
        QVERIFY(backups);
        QVERIFY(update->isVisibleTo(&browser));
        QVERIFY(update->styleSheet().contains(QStringLiteral("#f0c84b")));
        QCOMPARE(update->toolTip(), QStringLiteral("Latest commit: def"));

        browser.setManagedCorpusStatus(CorpusUpdateState::Current,
            QStringLiteral("Installed def • current as of today"),
            QStringLiteral("Current as of: today"), 2);
        QCOMPARE(update->text(), QStringLiteral("OP-songs is current"));
        QVERIFY(update->styleSheet().isEmpty());
    }

    void backupManagerDisplaysCommitAndFreshness()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("OP-songs"));
        const QString backup = target + QStringLiteral(".backup-20260811-120000");
        corpus::SnapshotInfo snapshot;
        snapshot.commitSha = QString(40, u'a');
        snapshot.commitDate
            = QDateTime::fromString(QStringLiteral("2026-08-10T12:00:00Z"), Qt::ISODate);
        snapshot.currentAsOf
            = QDateTime::fromString(QStringLiteral("2026-08-11T12:00:00Z"), Qt::ISODate);
        QVERIFY(corpus::writeSnapshot(backup, snapshot));

        CorpusBackupDialog dialog(target);
        QTreeWidget *tree = dialog.findChild<QTreeWidget *>();
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(1), QString(10, u'a'));
        QVERIFY(tree->topLevelItem(0)->text(2) != QStringLiteral("Unknown"));
        QVERIFY(tree->topLevelItem(0)->text(3) != QStringLiteral("Unknown"));
    }
};

QTEST_MAIN(UiWorkflowTests)
#include "UiWorkflowTests.moc"
