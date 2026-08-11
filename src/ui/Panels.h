// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// The docked panels: song header, problems, inspector, source preview, and the
// transport.

#pragma once

#include "app/Session.h"

#include <QSet>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTreeWidget;
QT_END_NAMESPACE

namespace ope::ui {

/// Title, key, metre, tempo, verse count, copyrights, commentary.
class HeaderPanel : public QWidget {
    Q_OBJECT
public:
    explicit HeaderPanel(Session *session, QWidget *parent = nullptr);
    void refresh();
    /// Flush the multi-line boxes, which only commit on focus-out. Save must
    /// call this: Ctrl+S is a shortcut and does not move focus, so a copyright
    /// or commentary line typed just before it would never reach the document
    /// and would then be wiped by the post-save refresh.
    void commitPendingEdits() { commit(); }
    [[nodiscard]] bool hasPendingEdits() const noexcept { return m_pending; }

Q_SIGNALS:
    void pendingEditsChanged(bool pending);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void commit();
    void editTimeSigChanges();

    Session *m_session = nullptr;
    bool m_loading = false;
    bool m_pending = false;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_subtitle = nullptr;
    QComboBox *m_key = nullptr;
    QSpinBox *m_timeNumerator = nullptr;
    QComboBox *m_timeDenominator = nullptr;
    QSpinBox *m_tempo = nullptr;
    QSpinBox *m_verseCount = nullptr;
    QCheckBox *m_active = nullptr;
    QComboBox *m_converge = nullptr;
    QPushButton *m_timeSigChanges = nullptr;
    QPlainTextEdit *m_copyrights = nullptr;
    QPlainTextEdit *m_commentary = nullptr;
    QLabel *m_inherited = nullptr;
};

/// Findings, most severe first, with navigation.
class ProblemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(Session *session, QWidget *parent = nullptr);
    void refresh();
    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void navigate(const Finding &finding);

private:
    Session *m_session = nullptr;
    QTreeWidget *m_tree = nullptr;
    QCheckBox *m_showWarnings = nullptr;
    QCheckBox *m_showInfo = nullptr;
    QLabel *m_summary = nullptr;
};

/// Context-sensitive: the selected note, else the selected part, else the song.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(Session *session, QWidget *parent = nullptr);
    void refresh();

private:
    void commitPart();

    Session *m_session = nullptr;
    bool m_loading = false;
    QLabel *m_noteInfo = nullptr;
    QComboBox *m_choralType = nullptr;
    QComboBox *m_clef = nullptr;
    QSpinBox *m_staffNumber = nullptr;
    QComboBox *m_splice = nullptr;
    QLineEdit *m_suppressVerses = nullptr;
    QLineEdit *m_suppressWhen = nullptr;
    QLabel *m_spliceReport = nullptr;
    QString m_partName;
};

/// Exactly the bytes that will be written, with a diff against the file on disk.
class SourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcePanel(Session *session, QWidget *parent = nullptr);
    void refresh();

private:
    Session *m_session = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_openExternal = nullptr;
};

/// Play, pause, stop, verse, per-part mute, tempo override.
class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(Session *session, QWidget *parent = nullptr);
    void refresh();
    void setPlaying(bool playing);
    void setPosition(double seconds, double duration);
    void setDuration(double seconds);
    void setUnavailable(const QString &reason);

    [[nodiscard]] PlaybackOptions options() const;
    [[nodiscard]] int verse() const;

Q_SIGNALS:
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void optionsChanged();
    void verseChanged(int verse);

private:
    void updatePositionLabel();

    Session *m_session = nullptr;
    /// The transport's own idea of whether sound is coming out. Reading it back
    /// off the button's label is how the Play button came to emit Pause.
    bool m_playing = false;
    double m_position = 0.0;
    double m_duration = 0.0;
    QPushButton *m_play = nullptr;
    QPushButton *m_stop = nullptr;
    QComboBox *m_verse = nullptr;
    QWidget *m_partBox = nullptr;
    QList<QCheckBox *> m_partChecks;
    QSet<QString> m_mutedParts;
    QSlider *m_tempo = nullptr;
    QLabel *m_tempoLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
};

} // namespace ope::ui
