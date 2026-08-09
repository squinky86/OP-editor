// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// A small polyphonic synth, so hearing a hymn needs no soundfont and no
// external player. A few harmonics with a soft envelope is enough to check that
// the right notes are in the right places at the right volume, which is all the
// editor's playback is for.

#pragma once

#include "core/Playback.h"

#include <QList>

namespace ope::audio {

inline constexpr int SampleRate = 48000;

/// One sounding note.
struct Voice {
    bool active = false;
    double phase = 0.0;
    double frequency = 440.0;
    double amplitude = 0.3;
    qint64 startFrame = 0;
    qint64 releaseFrame = 0;  ///< frame at which the release stage begins
    bool releasing = false;
    double envelope = 0.0;
    int partIndex = 0;
};

/// Renders the notes of a PlaybackPlan into float samples on demand.
class Synth {
public:
    void setPlan(const PlaybackPlan &plan);
    void reset(double startSeconds);

    /// Fill `frames` mono samples. Returns false once the plan has finished and
    /// every voice has faded out.
    bool render(float *output, int frames);

    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] bool finished() const noexcept { return m_finished; }
    void setMasterGain(double gain) { m_gain = gain; }

private:
    void startVoice(const PlaybackNote &note);

    PlaybackPlan m_plan;
    QList<Voice> m_voices;
    qint64 m_frame = 0;
    int m_nextNote = 0;
    bool m_finished = false;
    double m_gain = 0.6;
};

} // namespace ope::audio
