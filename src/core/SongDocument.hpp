// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QUndoStack>
#include "SongData.hpp"
#include "Common.hpp"

namespace OpenPsalm {

class SongDocument : public QObject {
    Q_OBJECT

public:
    explicit SongDocument(QObject* parent = nullptr);

    bool isDirty() const { return m_isDirty; }
    void setDirty(bool dirty);

    QString filePath() const { return m_filePath; }
    QString baseFilePath() const { return m_baseFilePath; }
    bool isOverlay() const { return m_isOverlay; }

    const SongData& songData() const { return m_songData; }
    SongData& songData() { return m_songData; }

    const SongData& effectiveData() const { return m_isOverlay ? m_effectiveData : m_songData; }

    QUndoStack* undoStack() const { return m_undoStack; }

    const std::vector<Diagnostic>& diagnostics() const { return m_diagnostics; }
    void setDiagnostics(const std::vector<Diagnostic>& diags);

    // Document lifecycle
    bool loadFromFile(const QString& path);
    bool saveToFile(const QString& path = QString());

    void updateEffectiveData();
    void revalidate();

signals:
    void documentModified();
    void documentLoaded();
    void documentSaved();
    void diagnosticsChanged();

private:
    QString m_filePath;
    QString m_baseFilePath;
    bool m_isOverlay{false};
    bool m_isDirty{false};

    SongData m_songData;       // Own file data (overlay or base)
    SongData m_baseData;       // Base song data if editing overlay
    SongData m_effectiveData;  // Merged effective data

    QUndoStack* m_undoStack{nullptr};
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace OpenPsalm
