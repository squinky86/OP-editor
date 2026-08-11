// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Synth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ope::audio {
namespace {

constexpr int MaxVoices = 24;
constexpr double AttackSeconds = 0.012;
constexpr double ReleaseSeconds = 0.10;

double midiToFrequency(int midiNote)
{
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

/// A voiced, slightly reedy tone: fundamental plus a few decaying harmonics.
/// Deliberately plain — this is a proofreading aid, not an instrument.
double toneAt(double phase)
{
    constexpr double harmonics[4] = { 1.0, 0.45, 0.22, 0.10 };
    double value = 0.0;
    for (int h = 0; h < 4; ++h)
        value += harmonics[h] * std::sin(phase * (h + 1));
    return value * 0.55;
}

} // namespace

void Synth::setPlan(const PlaybackPlan &plan)
{
    m_plan = plan;
    reset(0.0);
}

void Synth::reset(double startSeconds)
{
    m_voices.clear();
    m_frame = static_cast<qint64>(startSeconds * SampleRate);
    m_finished = false;
    m_nextNote = 0;
    // Recreate notes that began before a paused playhead but are still
    // sounding. Skipping them made Resume silent until the next note onset.
    while (m_nextNote < m_plan.notes.size()
        && m_plan.notes.at(m_nextNote).startSeconds < startSeconds) {
        if (m_plan.notes.at(m_nextNote).endSeconds > startSeconds)
            startVoice(m_plan.notes.at(m_nextNote));
        ++m_nextNote;
    }
}

void Synth::startVoice(const PlaybackNote &note)
{
    Voice voice;
    voice.active = true;
    voice.frequency = midiToFrequency(note.midiNote);
    voice.amplitude = std::clamp(note.velocity / 127.0, 0.05, 1.0) * 0.5;
    voice.startFrame = static_cast<qint64>(note.startSeconds * SampleRate);
    voice.releaseFrame = static_cast<qint64>(note.endSeconds * SampleRate);
    voice.partIndex = note.partIndex;

    if (m_voices.size() >= MaxVoices) {
        // Steal the oldest voice rather than dropping the new note: a missing
        // entry is more confusing than a slightly clipped tail.
        auto oldest = std::min_element(m_voices.begin(), m_voices.end(),
            [](const Voice &a, const Voice &b) { return a.startFrame < b.startFrame; });
        *oldest = voice;
        return;
    }
    m_voices.append(voice);
}

bool Synth::render(float *output, int frames)
{
    std::fill_n(output, frames, 0.0F);

    const double attackFrames = AttackSeconds * SampleRate;
    const double releaseFrames = ReleaseSeconds * SampleRate;

    for (int i = 0; i < frames; ++i) {
        const qint64 frame = m_frame + i;
        const double seconds = static_cast<double>(frame) / SampleRate;

        while (m_nextNote < m_plan.notes.size()
            && m_plan.notes.at(m_nextNote).startSeconds <= seconds) {
            startVoice(m_plan.notes.at(m_nextNote));
            ++m_nextNote;
        }

        double sample = 0.0;
        for (Voice &voice : m_voices) {
            if (!voice.active)
                continue;

            const double age = static_cast<double>(frame - voice.startFrame);
            if (age < 0)
                continue;

            double envelope = 1.0;
            if (age < attackFrames)
                envelope = age / attackFrames;
            if (frame >= voice.releaseFrame) {
                const double since = static_cast<double>(frame - voice.releaseFrame);
                envelope *= std::max(0.0, 1.0 - since / releaseFrames);
                if (envelope <= 0.0) {
                    voice.active = false;
                    continue;
                }
            }

            voice.phase += 2.0 * std::numbers::pi * voice.frequency / SampleRate;
            if (voice.phase > 2.0 * std::numbers::pi * 16)
                voice.phase -= 2.0 * std::numbers::pi * 16;
            sample += toneAt(voice.phase) * voice.amplitude * envelope;
        }

        m_voices.removeIf([](const Voice &voice) { return !voice.active; });

        // A soft limiter: four voices at full velocity would otherwise clip.
        sample *= m_gain;
        output[i] = static_cast<float>(std::tanh(sample));
    }

    m_frame += frames;

    const bool notesExhausted = m_nextNote >= m_plan.notes.size();
    const bool silent = m_voices.isEmpty();
    if (notesExhausted && silent
        && static_cast<double>(m_frame) / SampleRate > m_plan.totalSeconds)
        m_finished = true;
    return !m_finished;
}

double Synth::positionSeconds() const { return static_cast<double>(m_frame) / SampleRate; }

} // namespace ope::audio
