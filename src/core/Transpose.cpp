// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#include "Transpose.h"
#include "Validator.h"

namespace ope {
namespace {

std::optional<Pitch> tonic(QString key)
{
    if (key.endsWith(u'm'))
        key.chop(1);
    if (!validKeySignatures().contains(key))
        return std::nullopt;
    return Pitch { key.front(), 3,
        key.endsWith(u'#') ? 1 : key.endsWith(u'b') ? -1 : 0 };
}

Pitch transposePitch(const Pitch &pitch, Transposition interval)
{
    const int midi = pitch.midiNote() + interval.semitones;
    Pitch result = Pitch::fromDiatonic(pitch.diatonic() + interval.steps, 0);
    result.alter = midi - result.midiNote();
    // The notation supports up to double accidentals. A rare triple accidental
    // needs an adjacent enharmonic spelling, never a clamped or dropped alter.
    while (result.alter > 2 || result.alter < -2) {
        result = Pitch::fromDiatonic(result.diatonic() + (result.alter > 2 ? 1 : -1), 0);
        result.alter = midi - result.midiNote();
    }
    return result;
}

} // namespace

std::expected<Transposition, QString> keyTransposition(
    const QString &from, const QString &to, TransposeDirection direction)
{
    const auto source = tonic(from);
    const auto target = tonic(to);
    if (!source || !target)
        return std::unexpected(QStringLiteral("Choose a supported source and target key signature."));
    if (from.endsWith(u'm') != to.endsWith(u'm'))
        return std::unexpected(QStringLiteral("Transposition preserves major or minor mode. Choose a key in the same mode."));

    const int chromatic = target->midiNote() - source->midiNote();
    int semitones = (chromatic % 12 + 12) % 12;
    if ((direction == TransposeDirection::Down && semitones > 0)
        || (direction == TransposeDirection::Nearest && semitones > 6))
        semitones -= 12;
    return Transposition { semitones,
        target->diatonic() - source->diatonic() + 7 * ((semitones - chromatic) / 12) };
}

std::expected<SongDocument, QString> transposeSong(const SongDocument &authored,
    const SongDocument &effective, const QString &targetKey, TransposeDirection direction)
{
    if (authored.isMergedView)
        return std::unexpected(QStringLiteral("Select an authored song or translation to transpose."));
    const auto interval = keyTransposition(
        effective.keySignature.valueOr(QStringLiteral("C")), targetKey, direction);
    if (!interval)
        return std::unexpected(interval.error());
    SongDocument result = authored;
    if (interval->steps == 0 && interval->semitones == 0
        && effective.keySignature.valueOr(QStringLiteral("C")) == targetKey)
        return result;

    for (const Part &part : effective.parts) {
        for (const TokenIssue &issue : part.tokenIssues) {
            if (issue.code != TokenIssue::Code::UndocumentedDynamic)
                return std::unexpected(QStringLiteral("Fix the notation in %1, measure %2 before transposing.")
                    .arg(part.name).arg(issue.measureIndex + 1));
        }
        NoteStream stream = part.stream;
        bool changed = false;
        for (Measure &measure : stream.measures()) {
            for (Event &event : measure.events) {
                for (Pitch &pitch : event.pitches) {
                    if (!QStringLiteral("ABCDEFG").contains(pitch.step))
                        return std::unexpected(QStringLiteral("Fix the invalid pitch in %1 before transposing.").arg(part.name));
                    const Pitch transposed = transposePitch(pitch, *interval);
                    if (transposed.midiNote() < 0 || transposed.midiNote() > 127)
                        return std::unexpected(QStringLiteral("Transposing %1 would move a note outside the supported playback range. Choose the other direction.").arg(part.name));
                    if (transposed != pitch) {
                        pitch = transposed;
                        event.dirty = true;
                        changed = true;
                    }
                }
            }
        }
        if (changed) {
            Part &target = result.ensurePart(part.name);
            target.notes.set(stream.toSource());
            target.reparse();
        }
    }
    result.keySignature.set(targetKey);
    return result;
}

} // namespace ope
