// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// Song TOML used by the tests.
//
// These live in their own translation unit because moc cannot lex a raw string
// literal containing `"""` — which every `notes` block has — and silently emits
// no meta-object for the file that holds one.

#pragma once

#include <QByteArray>

namespace ope::fixtures {

QByteArray baseSong();
QByteArray partsAndMultilineStrings();
QByteArray emptyVerse();
QByteArray chorusAfterVerses();
QByteArray refrainFirst();
QByteArray sharedSections();
QByteArray brokenSharedReference();
QByteArray shortMeasure();
QByteArray timeSignatureChange();
QByteArray overlongVerse();
QByteArray timing();
QByteArray tied();
QByteArray dynamics();
QByteArray sopranoNotesOverride();

} // namespace ope::fixtures
