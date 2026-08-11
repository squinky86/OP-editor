// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

#include "app/Session.h"
#include "ui/Dialogs.h"
#include "ui/LyricsPanel.h"
#include "ui/Panels.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

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
};

QTEST_MAIN(UiWorkflowTests)
#include "UiWorkflowTests.moc"
