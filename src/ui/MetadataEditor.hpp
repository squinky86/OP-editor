// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include "core/SongDocument.hpp"

namespace OpenPsalm {

class MetadataEditor : public QWidget {
    Q_OBJECT

public:
    explicit MetadataEditor(SongDocument* doc, QWidget* parent = nullptr);
    void refreshFromDocument();

private slots:
    void applyChanges();

private:
    SongDocument* m_doc{nullptr};
    bool m_updating{false};

    QLineEdit* m_titleEdit{nullptr};
    QLineEdit* m_subtitleEdit{nullptr};
    QCheckBox* m_activeCheck{nullptr};
    QLineEdit* m_languageEdit{nullptr};
    QSpinBox* m_verseCountSpin{nullptr};
    QLineEdit* m_keyEdit{nullptr};
    QSpinBox* m_timeNumSpin{nullptr};
    QSpinBox* m_timeDenSpin{nullptr};
    QSpinBox* m_tempoSpin{nullptr};
    QTextEdit* m_copyrightsEdit{nullptr};
    QTextEdit* m_commentaryEdit{nullptr};
    QLineEdit* m_phraseBreaksEdit{nullptr};
    QLineEdit* m_optionalPhraseBreaksEdit{nullptr};
};

} // namespace OpenPsalm
