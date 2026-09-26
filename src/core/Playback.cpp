// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Playback.h"
#include "Voicing.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>

namespace ope {
namespace {

// Use the exporter's 480 PPQ grid for tempo boundaries: rounding an eighth of
// a span on the editor's coarser 48 PPQ grid changes audible timings.
constexpr int MidiTicksPerQuarter = 480;
constexpr int MidiTicksPerTick = MidiTicksPerQuarter / ticks::Quarter;

int midiDuration(const Event &event)
{
    const int duration = event.duration.notatedTicks() * MidiTicksPerTick;
    return event.tuplet && event.tuplet->actual > 0
        ? duration * event.tuplet->normal / event.tuplet->actual : duration;
}

/// Port of OpenPsalm compute_tempo_changes: step marks jump and hold; gradual
/// marks ramp from the active tempo to the terminating note's onset.
/// Only the authored lead supplies markers, whether audible or muted.
QMap<int, int> tempoChanges(const SongDocument &doc)
{
    const int songBpm = std::max(20, doc.tempoBpm.valueOr(120));
    QMap<int, int> microsAt { { 0, 60000000 / songBpm } };
    const Part *lead = voicing::lead(doc);
    if (!lead)
        return microsAt;
    struct TimedEvent { int tick; const Event *event; };
    QList<TimedEvent> flat;
    int tick = 0;
    for (const Measure &measure : lead->stream.measures()) {
        for (const Event &event : measure.events) {
            flat.append({ tick, &event });
            tick += midiDuration(event);
        }
    }
    int activeBpm = songBpm;
    for (qsizetype i = 0; i < flat.size(); ++i) {
        const QString &kind = flat.at(i).event->tempoSpanner;
        const TempoMark *mark = tempoMark(kind);
        if (!mark)
            continue;
        if (mark->kind == TempoMark::Kind::Restore) {
            microsAt.insert(flat.at(i).tick, 60000000 / songBpm);
            activeBpm = songBpm;
            continue;
        }
        qsizetype end = flat.size() - 1;
        bool closedBySpanEnd = false;
        for (qsizetype j = i + 1; j < flat.size(); ++j) {
            if (flat.at(j).event->spannerEnd) {
                end = j;
                closedBySpanEnd = true;
                break;
            }
            if (!flat.at(j).event->tempoSpanner.isEmpty()) {
                end = j - 1;
                break;
            }
        }
        const int start = flat.at(i).tick;
        const Event &endEvent = *flat.at(end).event;
        // A start on the terminating note takes over at its onset. Preserve
        // the exporter's historical \spanend\atempo restore after that note.
        const bool handsOff = closedBySpanEnd && !endEvent.tempoSpanner.isEmpty()
            && endEvent.tempoSpanner != QLatin1String("atempo");
        if (mark->kind == TempoMark::Kind::Step) {
            activeBpm = std::max(20, static_cast<int>(std::round(songBpm * mark->ratio)));
            microsAt.insert(start, 60000000 / activeBpm);
            if (!closedBySpanEnd) {
                i = end;
                continue;
            }
        } else {
            const int target = std::max(20, static_cast<int>(std::round(activeBpm * mark->ratio)));
            const int length = flat.at(end).tick - start;
            for (int step = 1; step <= 8; ++step) {
                const double fraction = step / 8.0;
                const double bpm = activeBpm + (target - activeBpm) * fraction;
                microsAt.insert(start + static_cast<int>(std::round(length * fraction)),
                    static_cast<int>(std::round(60000000.0 / bpm)));
            }
        }
        activeBpm = songBpm;
        const int restoreTick = flat.at(end).tick + (handsOff ? 0 : midiDuration(endEvent));
        microsAt.insert(restoreTick, 60000000 / songBpm);
        i = handsOff ? end - 1 : end;
    }
    return microsAt;
}

/// Integrate the piecewise-constant MIDI tempo map once, before applying mutes.
class TempoMap {
public:
    TempoMap(const SongDocument &doc, double scale)
    {
        const auto changes = tempoChanges(doc);
        int previousTick = 0;
        double seconds = 0.0;
        double secondsPerTick = 0.0;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            seconds += (it.key() - previousTick) * secondsPerTick;
            secondsPerTick = it.value() / (1000000.0 * MidiTicksPerQuarter * std::max(0.05, scale));
            m_boundaries.append({ it.key(), seconds, secondsPerTick });
            previousTick = it.key();
        }
    }

    [[nodiscard]] double secondsAt(int tick) const
    {
        const int midiTick = tick * MidiTicksPerTick;
        const auto upper = std::upper_bound(m_boundaries.cbegin(), m_boundaries.cend(), midiTick,
            [](int t, const Boundary &b) { return t < b.tick; });
        const Boundary &boundary = upper == m_boundaries.cbegin() ? m_boundaries.first() : *std::prev(upper);
        return boundary.seconds + (midiTick - boundary.tick) * boundary.secondsPerTick;
    }

private:
    struct Boundary {
        int tick;
        double seconds;
        double secondsPerTick;
    };
    QList<Boundary> m_boundaries;
};

} // namespace

int PlaybackPlan::measureAt(double seconds) const
{
    int result = -1;
    for (int i = 0; i < measureStartSeconds.size(); ++i) {
        if (measureStartSeconds.at(i) <= seconds)
            result = i;
        else
            break;
    }
    return result;
}

int PlaybackPlan::tickAt(double seconds) const
{
    // Linear within the measure that contains `seconds`, which is accurate
    // enough to place a cursor and cheap enough to run every frame.
    const int measure = measureAt(seconds);
    if (measure < 0 || measure >= measureStartTicks.size())
        return 0;
    const double measureStart = measureStartSeconds.at(measure);
    const double nextStart = measure + 1 < measureStartSeconds.size()
        ? measureStartSeconds.at(measure + 1)
        : totalSeconds;
    const int tickStart = measureStartTicks.at(measure);
    const int nextTick = measure + 1 < measureStartTicks.size()
        ? measureStartTicks.at(measure + 1)
        : tickStart;
    if (nextStart <= measureStart)
        return tickStart;
    const double fraction = (seconds - measureStart) / (nextStart - measureStart);
    return tickStart + static_cast<int>((nextTick - tickStart) * std::clamp(fraction, 0.0, 1.0));
}

PlaybackPlan buildPlan(const SongDocument &doc, const PlaybackOptions &options)
{
    PlaybackPlan plan;

    // Measure grid: every part must agree, and where they do not the longest wins
    // so playback still covers the whole song.
    const int measureCount = doc.measureCount();
    int tick = 0;
    for (int m = 0; m < measureCount; ++m) {
        plan.measureStartTicks.append(tick);
        int measureTicks = doc.expectedTicksForMeasure(m + 1);
        for (const Part &part : doc.parts) {
            if (m < part.stream.measureCount())
                measureTicks = std::max(measureTicks, part.stream.measures().at(m).playedTicks());
        }
        tick += measureTicks;
    }
    const int totalTicks = tick;

    const TempoMap tempo(doc, options.tempoScale);
    for (const int measureTick : plan.measureStartTicks)
        plan.measureStartSeconds.append(tempo.secondsAt(measureTick));

    const int firstMeasure = std::max(0, options.fromMeasure);
    const int lastMeasure = options.toMeasure < 0 ? measureCount - 1
                                                  : std::min(options.toMeasure, measureCount - 1);

    for (int partIndex = 0; partIndex < doc.parts.size(); ++partIndex) {
        const Part &part = doc.parts.at(partIndex);
        if (options.mutedParts.contains(part.name))
            continue;

        int velocity = 80;
        std::optional<QString> openHairpin;
        int hairpinStartVelocity = 80;

        // Tied notes are one sound: hold the start until the chain ends.
        struct Held {
            int startTick = 0;
            int measureIndex = 0;
            int eventIndex = 0;
            int velocity = 80;
        };
        QHash<int, Held> held;

        const QList<Measure> &measures = part.stream.measures();
        for (int m = 0; m < measures.size() && m <= lastMeasure; ++m) {
            int cursor = plan.measureStartTicks.value(m, 0);
            for (int e = 0; e < measures.at(m).events.size(); ++e) {
                const Event &event = measures.at(m).events.at(e);
                const int endTick = cursor + event.playedTicks();

                if (!event.dynamic.isEmpty()) {
                    velocity = velocityForDynamic(event.dynamic);
                    openHairpin.reset();
                }
                if (event.hairpin == QLatin1String("crescendo")
                    || event.hairpin == QLatin1String("diminuendo")) {
                    openHairpin = event.hairpin;
                    hairpinStartVelocity = velocity;
                } else if (event.hairpin == QLatin1String("end")) {
                    openHairpin.reset();
                } else if (openHairpin) {
                    // A gentle ramp, as the exporter applies per note.
                    const int step = *openHairpin == QLatin1String("crescendo") ? 6 : -6;
                    velocity = std::clamp(velocity + step, 1, 127);
                    Q_UNUSED(hairpinStartVelocity);
                }

                if (event.isSounding() && m >= firstMeasure) {
                    for (qsizetype pitchIndex = 0; pitchIndex < event.pitches.size();
                        ++pitchIndex) {
                        const int midiNote = event.pitches.at(pitchIndex).midiNote();
                        const auto existing = held.constFind(midiNote);
                        if (existing != held.constEnd()) {
                            // Continue the sustained note rather than restarting it.
                            Held updated = *existing;
                            held.insert(midiNote, updated);
                            if (!event.tie) {
                                PlaybackNote note;
                                note.startTick = updated.startTick;
                                note.endTick = endTick;
                                note.startSeconds = tempo.secondsAt(updated.startTick);
                                note.endSeconds = tempo.secondsAt(endTick);
                                note.midiNote = midiNote;
                                note.velocity = updated.velocity;
                                note.partIndex = partIndex;
                                note.measureIndex = updated.measureIndex;
                                note.eventIndex = updated.eventIndex;
                                plan.notes.append(note);
                                held.remove(midiNote);
                            }
                            continue;
                        }
                        if (event.tie) {
                            held.insert(midiNote, Held { cursor, m, e, velocity });
                            continue;
                        }
                        PlaybackNote note;
                        note.startTick = cursor;
                        note.endTick = endTick;
                        note.startSeconds = tempo.secondsAt(cursor);
                        note.endSeconds = tempo.secondsAt(endTick);
                        note.midiNote = midiNote;
                        note.velocity = velocity;
                        note.partIndex = partIndex;
                        note.measureIndex = m;
                        note.eventIndex = e;
                        plan.notes.append(note);
                    }
                } else if (!event.isSounding()) {
                    // A rest breaks a tie chain; never write a tie into a rest.
                    for (auto it = held.constBegin(); it != held.constEnd(); ++it) {
                        PlaybackNote note;
                        note.startTick = it->startTick;
                        note.endTick = cursor;
                        note.startSeconds = tempo.secondsAt(it->startTick);
                        note.endSeconds = tempo.secondsAt(cursor);
                        note.midiNote = it.key();
                        note.velocity = it->velocity;
                        note.partIndex = partIndex;
                        note.measureIndex = it->measureIndex;
                        note.eventIndex = it->eventIndex;
                        plan.notes.append(note);
                    }
                    held.clear();
                }
                cursor = endTick;
            }
        }
        for (auto it = held.constBegin(); it != held.constEnd(); ++it) {
            PlaybackNote note;
            note.startTick = it->startTick;
            note.endTick = totalTicks;
            note.startSeconds = tempo.secondsAt(it->startTick);
            note.endSeconds = tempo.secondsAt(totalTicks);
            note.midiNote = it.key();
            note.velocity = it->velocity;
            note.partIndex = partIndex;
            note.measureIndex = it->measureIndex;
            note.eventIndex = it->eventIndex;
            plan.notes.append(note);
        }
    }

    std::stable_sort(plan.notes.begin(), plan.notes.end(),
        [](const PlaybackNote &a, const PlaybackNote &b) {
            return a.startSeconds < b.startSeconds;
        });
    plan.totalSeconds = tempo.secondsAt(totalTicks);
    return plan;
}

} // namespace ope
