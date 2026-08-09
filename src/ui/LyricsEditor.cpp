// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "LyricsEditor.hpp"
#include "core/Commands.hpp"
#include "notation/LyricAligner.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>

namespace OpenPsalm {

LyricsEditor::LyricsEditor(SongDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    auto* mainLayout = new QHBoxLayout(this);

    // Left column: sections list + actions
    auto* leftLayout = new QVBoxLayout();
    m_sectionList = new QListWidget(this);
    leftLayout->addWidget(new QLabel(QStringLiteral("Lyric Sections:"), this));
    leftLayout->addWidget(m_sectionList);

    auto* btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(QStringLiteral("Add Section"), this);
    m_removeBtn = new QPushButton(QStringLiteral("Remove"), this);
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    leftLayout->addLayout(btnLayout);

    mainLayout->addLayout(leftLayout, 1);

    // Right column: lyric text editor + syllable stats
    auto* rightLayout = new QVBoxLayout();
    auto* topActionLayout = new QHBoxLayout();
    m_syllableCountLabel = new QLabel(QStringLiteral("Syllables: 0"), this);
    m_insertElisionBtn = new QPushButton(QStringLiteral("Insert Elision (‿)"), this);
    topActionLayout->addWidget(m_syllableCountLabel);
    topActionLayout->addStretch();
    topActionLayout->addWidget(m_insertElisionBtn);

    m_lyricsEdit = new QTextEdit(this);
    m_lyricsEdit->setPlaceholderText(QStringLiteral("Enter lyric text with space between syllables and ' -- ' between parts of a word..."));

    rightLayout->addLayout(topActionLayout);
    rightLayout->addWidget(m_lyricsEdit);
    mainLayout->addLayout(rightLayout, 2);

    connect(m_sectionList, &QListWidget::currentRowChanged, this, &LyricsEditor::onSectionSelected);
    connect(m_lyricsEdit, &QTextEdit::textChanged, this, &LyricsEditor::onLyricsChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &LyricsEditor::addSection);
    connect(m_removeBtn, &QPushButton::clicked, this, &LyricsEditor::removeSection);
    connect(m_insertElisionBtn, &QPushButton::clicked, this, &LyricsEditor::insertElision);

    connect(m_doc, &SongDocument::documentLoaded, this, &LyricsEditor::refreshFromDocument);
}

void LyricsEditor::refreshFromDocument() {
    m_updating = true;
    m_sectionList->clear();

    const auto& keys = m_doc->songData().lyrics.keys();
    for (const QString& k : keys) {
        m_sectionList->addItem(k);
    }

    if (m_sectionList->count() > 0) {
        m_sectionList->setCurrentRow(0);
        m_currentKey = m_sectionList->item(0)->text();
        m_lyricsEdit->setPlainText(m_doc->songData().lyrics.section(m_currentKey));
    } else {
        m_currentKey.clear();
        m_lyricsEdit->clear();
    }

    m_updating = false;
    onLyricsChanged();
}

void LyricsEditor::onSectionSelected(int row) {
    if (row < 0 || row >= m_sectionList->count()) return;

    m_updating = true;
    m_currentKey = m_sectionList->item(row)->text();
    m_lyricsEdit->setPlainText(m_doc->songData().lyrics.section(m_currentKey));
    m_updating = false;

    QString text = m_lyricsEdit->toPlainText();
    QStringList syls = LyricAligner::parseSyllables(text);
    m_syllableCountLabel->setText(QStringLiteral("Syllables: %1").arg(syls.size()));
}

void LyricsEditor::onLyricsChanged() {
    if (m_updating || m_currentKey.isEmpty()) return;

    QString text = m_lyricsEdit->toPlainText();
    QStringList syls = LyricAligner::parseSyllables(text);
    m_syllableCountLabel->setText(QStringLiteral("Syllables: %1").arg(syls.size()));

    m_doc->undoStack()->push(new EditLyricsSectionCommand(m_doc, m_currentKey, text));
}

void LyricsEditor::addSection() {
    bool ok = false;
    QString key = QInputDialog::getText(this, QStringLiteral("Add Lyric Section"), QStringLiteral("Section key (e.g. 1, chorus, coda, s1):"), QLineEdit::Normal, QString(), &ok);
    if (ok && !key.trimmed().isEmpty()) {
        key = key.trimmed();
        m_doc->songData().lyrics.setSection(key, QString());
        refreshFromDocument();
        // Select newly added section
        for (int i = 0; i < m_sectionList->count(); ++i) {
            if (m_sectionList->item(i)->text() == key) {
                m_sectionList->setCurrentRow(i);
                break;
            }
        }
    }
}

void LyricsEditor::removeSection() {
    if (m_currentKey.isEmpty()) return;
    m_doc->songData().lyrics.removeSection(m_currentKey);
    refreshFromDocument();
}

void LyricsEditor::insertElision() {
    m_lyricsEdit->insertPlainText(QString::fromUtf8("‿"));
}

} // namespace OpenPsalm
