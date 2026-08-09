// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Playback.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>

namespace ope {
namespace {

/// A slowdown targets ~0.6x and a speedup ~1.4x of the active tempo, matching
/// the interpolation src/export/midi.rs performs across a spanner.
constexpr double SlowdownFactor = 0.6;
constexpr double SpeedupFactor = 1.4;
/// The exporter divides each span into eight equal segments; matching that keeps
/// the editor's timing identical to the rendered MP3.
constexpr int SpanSegments = 8;

struct Span {
    int beginTick = 0;
    int endTick = 0;
    bool slowdown = true;
    bool restoreAfter = false;
};

/// Collect tempo spanners. They are song-level (style guide §5.3 puts them on
/// the soprano), so the first part that carries any wins.
QList<Span> collectSpans(const SongDocument &doc)
{
    for (const Part *part : doc.partsInDisplayOrder()) {
        QList<Span> spans;
        std::optional<Span> open;
        int tick = 0;
        const QList<Measure> &measures = part->stream.measures();
        for (const Measure &measure : measures) {
            for (const Event &event : measure.events) {
                const int eventEnd = tick + event.playedTicks();
                if (!event.tempoSpanner.isEmpty()
                    && event.tempoSpanner != QLatin1String("atempo")) {
                    Span span;
                    span.beginTick = tick;
                    span.endTick = eventEnd;
                    span.slowdown = event.tempoSpanner == QLatin1String("rit")
                        || event.tempoSpanner == QLatin1String("ritard")
                        || event.tempoSpanner == QLatin1String("rall");
                    open = span;
                }
                if (event.spannerEnd && open) {
                    open->endTick = eventEnd;
                    open->restoreAfter = event.tempoSpanner == QLatin1String("atempo");
                    spans.append(*open);
                    open.reset();
                }
                tick = eventEnd;
            }
        }
        if (open) {
            open->endTick = tick;
            spans.append(*open);
        }
        if (!spans.isEmpty())
            return spans;
    }
    return {};
}

/// Piecewise-constant tempo over ticks, so seconds(tick) can be integrated once
/// and then queried per note.
class TempoMap {
public:
    TempoMap(const SongDocument &doc, double scale, int totalTicks)
    {
        const double baseBpm = std::max(1, doc.tempoBpm.valueOr(100)) * std::max(0.05, scale);
        const QList<Span> spans = collectSpans(doc);

        // Segment boundaries: every span start/end plus the interpolation steps.
        QMap<int, double> bpmAt;
        bpmAt.insert(0, baseBpm);
        for (const Span &span : spans) {
            const double target
                = baseBpm * (span.slowdown ? SlowdownFactor : SpeedupFactor);
            const int length = std::max(1, span.endTick - span.beginTick);
            for (int step = 0; step < SpanSegments; ++step) {
                const double fraction = static_cast<double>(step) / SpanSegments;
                const int tick = span.beginTick + static_cast<int>(length * fraction);
                bpmAt.insert(tick, baseBpm + (target - baseBpm) * fraction);
            }
            bpmAt.insert(span.endTick, span.restoreAfter ? baseBpm : target);
            if (span.restoreAfter)
                bpmAt.insert(span.endTick, baseBpm);
        }

        // Integrate: seconds at each boundary.
        int previousTick = 0;
        double previousBpm = baseBpm;
        double seconds = 0.0;
        for (auto it = bpmAt.constBegin(); it != bpmAt.constEnd(); ++it) {
            const int tick = it.key();
            if (tick > previousTick)
                seconds += ticksToSeconds(tick - previousTick, previousBpm);
            m_boundaries.append({ tick, seconds, it.value() });
            previousTick = tick;
            previousBpm = it.value();
        }
        m_tailBpm = previousBpm;
        m_tailTick = previousTick;
        m_tailSeconds = seconds;
        Q_UNUSED(totalTicks);
    }

    [[nodiscard]] double secondsAt(int tick) const
    {
        const Boundary *chosen = nullptr;
        for (const Boundary &boundary : m_boundaries) {
            if (boundary.tick <= tick)
                chosen = &boundary;
            else
                break;
        }
        if (!chosen)
            return ticksToSeconds(tick, m_tailBpm);
        return chosen->seconds + ticksToSeconds(tick - chosen->tick, chosen->bpm);
    }

    [[nodiscard]] int tickAt(double seconds) const
    {
        const Boundary *chosen = nullptr;
        for (const Boundary &boundary : m_boundaries) {
            if (boundary.seconds <= seconds)
                chosen = &boundary;
            else
                break;
        }
        if (!chosen)
            return 0;
        const double delta = seconds - chosen->seconds;
        return chosen->tick + static_cast<int>(delta * chosen->bpm * ticks::Quarter / 60.0);
    }

private:
    struct Boundary {
        int tick = 0;
        double seconds = 0.0;
        double bpm = 100.0;
    };
    static double ticksToSeconds(int tickCount, double bpm)
    {
        return (static_cast<double>(tickCount) / ticks::Quarter) * (60.0 / std::max(1.0, bpm));
    }
    QList<Boundary> m_boundaries;
    double m_tailBpm = 100.0;
    int m_tailTick = 0;
    double m_tailSeconds = 0.0;
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

    const TempoMap tempo(doc, options.tempoScale, totalTicks);
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
