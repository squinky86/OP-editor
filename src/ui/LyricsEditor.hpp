// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include "core/SongDocument.hpp"

namespace OpenPsalm {

class LyricsEditor : public QWidget {
    Q_OBJECT

public:
    explicit LyricsEditor(SongDocument* doc, QWidget* parent = nullptr);
    void refreshFromDocument();

private slots:
    void onSectionSelected(int row);
    void onLyricsChanged();
    void addSection();
    void removeSection();
    void insertElision();

private:
    SongDocument* m_doc{nullptr};
    bool m_updating{false};
    QString m_currentKey;

    QListWidget* m_sectionList{nullptr};
    QTextEdit* m_lyricsEdit{nullptr};
    QLabel* m_syllableCountLabel{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_insertElisionBtn{nullptr};
};

} // namespace OpenPsalm
