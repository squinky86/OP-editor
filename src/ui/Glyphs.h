// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Notation glyphs as vector paths.
//
// No music font is bundled or required: a SMuFL font cannot be assumed present
// and shipping one would be the only asset the program needs at runtime. Every
// shape here is built from primitives in staff-space units, where 1.0 is the gap
// between two staff lines, and scaled at paint time.
//
// These are editing glyphs, not engraving glyphs. They must be unambiguous at a
// glance; they are not trying to match LilyPond's plates.

#pragma once

#include <QPainterPath>

namespace ope::ui::glyphs {

/// Filled or hollow notehead, centred on the origin, one staff space tall.
[[nodiscard]] QPainterPath notehead(bool filled);
/// Whole-note head: wider, hollow, with the characteristic inner slant.
[[nodiscard]] QPainterPath wholeNotehead();
/// Treble clef, drawn with the G line at the origin.
[[nodiscard]] QPainterPath trebleClef();
/// Bass clef, drawn with the F line at the origin.
[[nodiscard]] QPainterPath bassClef();
/// Accidentals, centred vertically on the origin.
[[nodiscard]] QPainterPath sharp();
[[nodiscard]] QPainterPath flat();
[[nodiscard]] QPainterPath natural();
[[nodiscard]] QPainterPath doubleSharp();
[[nodiscard]] QPainterPath doubleFlat();
/// Rest for a duration base (1, 2, 4, 8, 16, 32, 64), positioned so the origin
/// sits on the staff's middle line.
[[nodiscard]] QPainterPath rest(int durationBase);
/// A note flag for an unbeamed eighth or shorter; `count` is the number of hooks.
[[nodiscard]] QPainterPath flag(int count, bool stemUp);
/// Fermata arc, opening downward when `above`.
[[nodiscard]] QPainterPath fermata(bool above);

} // namespace ope::ui::glyphs
