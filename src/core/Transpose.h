// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#pragma once

#include "Song.h"

namespace ope {

enum class TransposeDirection { Nearest, Up, Down };

struct Transposition {
    int semitones = 0;
    int steps = 0;
};

/// Preserve the mode and the diatonic spelling of the interval between tonics.
[[nodiscard]] std::expected<Transposition, QString> keyTransposition(
    const QString &from, const QString &to, TransposeDirection direction);

/// Produce an authored document with all effective sounding pitches transposed.
/// Translations materialize only the note streams that change. Failure leaves
/// both inputs untouched; rhythms, lyrics, and other metadata are preserved.
[[nodiscard]] std::expected<SongDocument, QString> transposeSong(
    const SongDocument &authored, const SongDocument &effective, const QString &targetKey,
    TransposeDirection direction = TransposeDirection::Nearest);

} // namespace ope
