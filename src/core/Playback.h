// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Turns a song into a list of sounding notes on a timeline.
//
// Notes are absolute sounding pitches: clefs never transpose playback. Ties
// sustain, tuplets scale, and dynamics act per part. The authored arrangement
// lead supplies the global tempo map before muting, with the website's eight
// MIDI tempo steps and restoration after the terminating note. Other monitoring
// approximations (hairpins, staccato, fine tuplets and cursor interpolation) are
// described in docs/getting-started.md. This is a monitoring aid, not an export.

#pragma once

#include "Song.h"

#include <QList>
#include <QString>

namespace ope {

struct PlaybackNote {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midiNote = 60;
    int velocity = 80;
    int partIndex = 0;
    int measureIndex = 0;
    int eventIndex = 0;
    int startTick = 0;
    int endTick = 0;
};

/// Which sections of the song a playback pass should include.
struct PlaybackOptions {
    int verse = 1;                  ///< which verse's lyrics to follow
    QList<QString> mutedParts;      ///< part names to leave silent
    double tempoScale = 1.0;        ///< transport tempo override
    int fromMeasure = 0;            ///< 0-based; start here
    int toMeasure = -1;             ///< 0-based inclusive; -1 = end of song
};

struct PlaybackPlan {
    QList<PlaybackNote> notes;      ///< sorted by start time
    double totalSeconds = 0.0;
    QList<double> measureStartSeconds;
    QList<int> measureStartTicks;

    /// Index of the measure sounding at `seconds`, or -1.
    [[nodiscard]] int measureAt(double seconds) const;
    /// Tick position at `seconds`, for driving the score cursor.
    [[nodiscard]] int tickAt(double seconds) const;
};

/// Build the plan. Uses the document's tempo, time signatures, and markings.
[[nodiscard]] PlaybackPlan buildPlan(const SongDocument &doc, const PlaybackOptions &options = {});

} // namespace ope
