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
