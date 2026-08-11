// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "app/Session.h"
#include "core/CorpusSnapshot.h"
#include "ui/CorpusBackupDialog.h"
#include "ui/Dialogs.h"
#include "ui/LyricsPanel.h"
#include "ui/Panels.h"
#include "ui/SongBrowser.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTabWidget>
#include <QTreeWidget>

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
        grid->setCurrentCell(2, 18);
        grid->scrollToItem(grid->item(2, 18));
        QCoreApplication::processEvents();
        const int before = grid->horizontalScrollBar()->value();
        QVERIFY(before > 0);

        session.togglePhraseBreak(PhraseBreak { 1, 16 }, BreakKind::Required);
        QCOMPARE(grid->horizontalScrollBar()->value(), before);
        QCOMPARE(grid->currentColumn(), 18);
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
