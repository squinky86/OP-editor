// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MetadataEditor.hpp"
#include "core/Commands.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>

namespace OpenPsalm {

MetadataEditor::MetadataEditor(SongDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);

    auto* contentWidget = new QWidget(scrollArea);
    auto* formLayout = new QFormLayout(contentWidget);

    m_titleEdit = new QLineEdit(contentWidget);
    m_subtitleEdit = new QLineEdit(contentWidget);
    m_activeCheck = new QCheckBox(QStringLiteral("Active Song"), contentWidget);
    m_languageEdit = new QLineEdit(contentWidget);
    m_verseCountSpin = new QSpinBox(contentWidget);
    m_verseCountSpin->setRange(1, 100);

    m_keyEdit = new QLineEdit(contentWidget);

    auto* timeSigLayout = new QHBoxLayout();
    m_timeNumSpin = new QSpinBox(contentWidget);
    m_timeNumSpin->setRange(1, 32);
    m_timeDenSpin = new QSpinBox(contentWidget);
    m_timeDenSpin->setRange(1, 64);
    timeSigLayout->addWidget(m_timeNumSpin);
    timeSigLayout->addWidget(new QLabel(QStringLiteral("/"), contentWidget));
    timeSigLayout->addWidget(m_timeDenSpin);

    m_tempoSpin = new QSpinBox(contentWidget);
    m_tempoSpin->setRange(20, 300);

    m_phraseBreaksEdit = new QLineEdit(contentWidget);
    m_phraseBreaksEdit->setPlaceholderText(QStringLiteral("e.g. 1:0, 2:16, 4:0"));

    m_optionalPhraseBreaksEdit = new QLineEdit(contentWidget);
    m_optionalPhraseBreaksEdit->setPlaceholderText(QStringLiteral("e.g. 3:0"));

    m_copyrightsEdit = new QTextEdit(contentWidget);
    m_copyrightsEdit->setMaximumHeight(80);
    m_commentaryEdit = new QTextEdit(contentWidget);
    m_commentaryEdit->setMaximumHeight(80);

    formLayout->addRow(QStringLiteral("Title:"), m_titleEdit);
    formLayout->addRow(QStringLiteral("Subtitle:"), m_subtitleEdit);
    formLayout->addRow(QStringLiteral("Status:"), m_activeCheck);
    formLayout->addRow(QStringLiteral("Language:"), m_languageEdit);
    formLayout->addRow(QStringLiteral("Verse Count:"), m_verseCountSpin);
    formLayout->addRow(QStringLiteral("Key Signature:"), m_keyEdit);
    formLayout->addRow(QStringLiteral("Time Signature:"), timeSigLayout);
    formLayout->addRow(QStringLiteral("Tempo (BPM):"), m_tempoSpin);
    formLayout->addRow(QStringLiteral("Phrase Breaks:"), m_phraseBreaksEdit);
    formLayout->addRow(QStringLiteral("Optional Breaks:"), m_optionalPhraseBreaksEdit);
    formLayout->addRow(QStringLiteral("Copyrights:"), m_copyrightsEdit);
    formLayout->addRow(QStringLiteral("Commentary:"), m_commentaryEdit);

    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);

    auto hookChange = [this]() {
        if (!m_updating) applyChanges();
    };

    connect(m_titleEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_subtitleEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_activeCheck, &QCheckBox::toggled, this, hookChange);
    connect(m_languageEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_verseCountSpin, &QSpinBox::valueChanged, this, hookChange);
    connect(m_keyEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_timeNumSpin, &QSpinBox::valueChanged, this, hookChange);
    connect(m_timeDenSpin, &QSpinBox::valueChanged, this, hookChange);
    connect(m_tempoSpin, &QSpinBox::valueChanged, this, hookChange);
    connect(m_phraseBreaksEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_optionalPhraseBreaksEdit, &QLineEdit::editingFinished, this, hookChange);
    connect(m_copyrightsEdit, &QTextEdit::textChanged, this, hookChange);
    connect(m_commentaryEdit, &QTextEdit::textChanged, this, hookChange);

    connect(m_doc, &SongDocument::documentLoaded, this, &MetadataEditor::refreshFromDocument);
}

void MetadataEditor::refreshFromDocument() {
    m_updating = true;
    const auto& song = m_doc->songData();

    m_titleEdit->setText(song.title);
    m_subtitleEdit->setText(song.subtitle.value_or(QString()));
    m_activeCheck->setChecked(song.active);
    m_languageEdit->setText(song.language);
    m_verseCountSpin->setValue(song.verseCount);
    m_keyEdit->setText(song.keySignature);
    m_timeNumSpin->setValue(song.timeSigNumerator);
    m_timeDenSpin->setValue(song.timeSigDenominator);
    m_tempoSpin->setValue(song.tempoBpm);

    QStringList pbList;
    for (const auto& pb : song.phraseBreaks) pbList.append(pb.toString());
    m_phraseBreaksEdit->setText(pbList.join(QLatin1String(", ")));

    QStringList opbList;
    for (const auto& pb : song.optionalPhraseBreaks) opbList.append(pb.toString());
    m_optionalPhraseBreaksEdit->setText(opbList.join(QLatin1String(", ")));

    m_copyrightsEdit->setPlainText(song.copyrights.join(QLatin1Char('\n')));
    m_commentaryEdit->setPlainText(song.commentary.value_or(QString()));

    m_updating = false;
}

void MetadataEditor::applyChanges() {
    SongData newData = m_doc->songData();

    newData.title = m_titleEdit->text().trimmed();
    QString sub = m_subtitleEdit->text().trimmed();
    newData.subtitle = sub.isEmpty() ? std::nullopt : std::optional<QString>(sub);
    newData.active = m_activeCheck->isChecked();
    newData.language = m_languageEdit->text().trimmed();
    newData.verseCount = m_verseCountSpin->value();
    newData.keySignature = m_keyEdit->text().trimmed();
    newData.timeSigNumerator = m_timeNumSpin->value();
    newData.timeSigDenominator = m_timeDenSpin->value();
    newData.tempoBpm = m_tempoSpin->value();

    newData.phraseBreaks.clear();
    for (const QString& s : m_phraseBreaksEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        auto pb = PhraseBreak::fromString(s.trimmed(), PhraseBreakKind::Required);
        if (pb.has_value()) newData.phraseBreaks.push_back(pb.value());
    }

    newData.optionalPhraseBreaks.clear();
    for (const QString& s : m_optionalPhraseBreaksEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        auto pb = PhraseBreak::fromString(s.trimmed(), PhraseBreakKind::Optional);
        if (pb.has_value()) newData.optionalPhraseBreaks.push_back(pb.value());
    }

    newData.copyrights = m_copyrightsEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString comm = m_commentaryEdit->toPlainText().trimmed();
    newData.commentary = comm.isEmpty() ? std::nullopt : std::optional<QString>(comm);

    m_doc->undoStack()->push(new EditMetadataCommand(m_doc, newData));
}

} // namespace OpenPsalm
