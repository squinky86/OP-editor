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
};

QTEST_GUILESS_MAIN(SessionTests)
#include "SessionTests.moc"
