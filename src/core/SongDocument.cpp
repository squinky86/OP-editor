// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SongDocument.hpp"
#include "format/TomlLoader.hpp"
#include "format/OverlayMerger.hpp"
#include "format/TomlSerializer.hpp"
#include "validation/Validator.hpp"
#include <QFileInfo>
#include <QDir>

namespace OpenPsalm {

SongDocument::SongDocument(QObject* parent)
    : QObject(parent),
      m_undoStack(new QUndoStack(this))
{
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) {
        setDirty(!clean);
    });
}

void SongDocument::setDirty(bool dirty) {
    if (m_isDirty != dirty) {
        m_isDirty = dirty;
        emit documentModified();
    }
}

void SongDocument::setDiagnostics(const std::vector<Diagnostic>& diags) {
    m_diagnostics = diags;
    emit diagnosticsChanged();
}

bool SongDocument::loadFromFile(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return false;

    m_filePath = fi.absoluteFilePath();
    QString fileName = fi.fileName();

    // Check if translation overlay: song_es.toml
    if (fileName.startsWith(QLatin1String("song_")) && fileName.endsWith(QLatin1String(".toml"))) {
        m_isOverlay = true;
        QDir dir = fi.dir();
        m_baseFilePath = dir.filePath(QLatin1String("song.toml"));
    } else {
        m_isOverlay = false;
        m_baseFilePath.clear();
    }

    auto loadRes = TomlLoader::loadFile(m_filePath);
    if (!loadRes.diagnostics.empty()) {
        m_diagnostics = loadRes.diagnostics;
    }
    m_songData = loadRes.songData;

    if (m_isOverlay && QFileInfo::exists(m_baseFilePath)) {
        auto baseLoadRes = TomlLoader::loadFile(m_baseFilePath);
        m_baseData = baseLoadRes.songData;
        updateEffectiveData();
    } else {
        m_effectiveData = m_songData;
    }

    m_undoStack->clear();
    setDirty(false);
    revalidate();

    emit documentLoaded();
    return true;
}

void SongDocument::updateEffectiveData() {
    if (m_isOverlay) {
        m_effectiveData = OverlayMerger::merge(m_baseData, m_songData);
    } else {
        m_effectiveData = m_songData;
    }
}

void SongDocument::revalidate() {
    updateEffectiveData();
    m_diagnostics = Validator::validateSong(effectiveData(), m_filePath);
    emit diagnosticsChanged();
}

bool SongDocument::saveToFile(const QString& path) {
    QString savePath = path.isEmpty() ? m_filePath : path;
    if (savePath.isEmpty()) return false;

    bool ok = TomlSerializer::saveToFile(m_songData, savePath);
    if (ok) {
        m_filePath = savePath;
        m_undoStack->setClean();
        setDirty(false);
        emit documentSaved();
    }
    return ok;
}

} // namespace OpenPsalm
