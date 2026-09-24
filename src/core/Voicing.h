// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#pragma once

#include "Clefs.h"
#include "Song.h"

namespace ope::voicing {

enum class Stem { Free, Up, Down };
struct Role {
    QString name;
    QString label;
    QString abbreviation;
    Stem stem;
};

[[nodiscard]] QString normalize(QStringView value);
/// Ordinary role order, also used to choose the one global tempo owner.
[[nodiscard]] const QList<Role> &roles();
[[nodiscard]] QStringList names();
[[nodiscard]] const Role *role(QStringView value);
[[nodiscard]] int rank(const Part &part);
[[nodiscard]] bool less(const Part &a, const Part &b);
[[nodiscard]] QList<int> orderedIndices(const SongDocument &doc);
[[nodiscard]] const Part *lead(const SongDocument &doc);
/// Friendly labels retain arbitrary/numbered identifiers where ambiguous.
[[nodiscard]] QString label(const Part &part);
[[nodiscard]] QString shortLabel(const SongDocument &doc, const Part &part);

struct Staff {
    std::optional<int> number;  ///< omission is a distinct staff for each part
    QList<int> partIndices;    ///< original document indices, in role order
    std::optional<clefs::Clef> clef;
    QString problem;           ///< unsupported, conflicting, or nonpositive metadata
};
[[nodiscard]] QList<Staff> staves(const SongDocument &doc);
/// A solo voice uses free stems. Repeated roles use the next available voice.
[[nodiscard]] QList<Stem> stems(const SongDocument &doc, const QList<int> &indices);

} // namespace ope::voicing
