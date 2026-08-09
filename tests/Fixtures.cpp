// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Fixtures.h"

namespace ope::fixtures {

QByteArray baseSong()
{
    return R"TOML(title = "Face to Face"
subtitle = "Original"
tempo_bpm = 96
verse_count = 4
key_signature = "Bb"
phrase_breaks = ["2:64"]
copyrights = ["Lyrics: Breck (public domain)", "Music: Tullar (public domain)"]

[parts.Soprano]
choral_type = "soprano"
clef = "treble"
staff_number = 1
notes = """
f'1 | g'1
"""

[parts.Alto]
choral_type = "alto"
clef = "treble"
staff_number = 1
notes = """
d'1 | ees'1
"""

[lyrics.1]
text = "one two"

[lyrics.2]
text = "three four"
)TOML";
}

QByteArray partsAndMultilineStrings()
{
    return R"TOML(copyrights = [
  "one",
  "two",
]

[parts.Soprano]
clef = "treble"
notes = """
c'4 d'4 | e'2
"""
)TOML";
}

QByteArray emptyVerse()
{
    return R"TOML(title = "Empty Verse Test"
time_sig_numerator = 4
time_sig_denominator = 4

[parts.Soprano]
choral_type = "soprano"
clef = "treble"
staff_number = 1
notes = """
c4 d4 e4 f4 | g4@c a4 b4 c'4
"""

[parts.Soprano.lyrics.1]
text = ""

[parts.Soprano.lyrics.chorus]
text = "praise him"
)TOML";
}

QByteArray chorusAfterVerses()
{
    return R"TOML(title = "T"
[parts.Soprano]
notes = """
c4 d4 e4 f4 | g4@c a4 b4 c'4
"""
[lyrics.1]
text = "a b c d"
[lyrics.chorus]
text = "e f g h"
)TOML";
}

QByteArray refrainFirst()
{
    return R"TOML(title = "T"
[parts.Soprano]
notes = """
c4@c d4 e4 f4 | g4 a4 b4 c'4
"""
[lyrics.1]
text = "a b c d"
[lyrics.chorus]
text = "e f g h"
)TOML";
}

QByteArray sharedSections()
{
    return R"TOML(title = "Shared"
[parts.Soprano]
choral_type = "soprano"
notes = """
c4 d4 e4 f4 | g4 a4 b4 c'4
"""
[parts.Tenor]
choral_type = "tenor"
notes = """
c4 d4 e4 f4 | g4 a4 b4 c'4
"""
[parts.Tenor.lyrics.s1]
text = "echo line here"
[lyrics.1]
text = "verse one text goes @s1 tail"
[lyrics.s1]
text = "call line here"
)TOML";
}

QByteArray brokenSharedReference()
{
    return R"TOML(title = "Broken"
[parts.Soprano]
notes = """
c4 d4 e4 f4
"""
[lyrics.1]
text = "one two @s9 four"
)TOML";
}

QByteArray shortMeasure()
{
    return R"TOML(title = "Short"
time_sig_numerator = 4
time_sig_denominator = 4
[parts.Soprano]
notes = """
c'4 c'4 c'4
"""
)TOML";
}

QByteArray timeSignatureChange()
{
    return R"TOML(title = "Change"
time_sig_numerator = 4
time_sig_denominator = 4

[[time_sig_changes]]
measure = 2
numerator = 3
denominator = 4
duration = 1

[parts.Soprano]
notes = """
c'1 | c'4 c'4 c'4 | c'1
"""
)TOML";
}

QByteArray overlongVerse()
{
    return R"TOML(title = "Long verse"
time_sig_numerator = 4
time_sig_denominator = 4
[parts.Soprano]
notes = """
c'4 c'4 c'4 c'4
"""
[lyrics.1]
text = "one two three four five"
)TOML";
}

QByteArray timing()
{
    return R"TOML(title = "Timing"
time_sig_numerator = 4
time_sig_denominator = 4
tempo_bpm = 120
[parts.Soprano]
notes = """
c'4 d'4 e'4 f'4 | g'1
"""
)TOML";
}

QByteArray tied()
{
    return R"TOML(title = "Tie"
time_sig_numerator = 4
time_sig_denominator = 4
tempo_bpm = 60
[parts.Soprano]
notes = """
c'2~ c'2
"""
)TOML";
}

QByteArray dynamics()
{
    return R"TOML(title = "Dyn"
time_sig_numerator = 4
time_sig_denominator = 4
[parts.Soprano]
notes = """
c'4%p c'4 c'4%f c'4
"""
)TOML";
}

QByteArray sopranoNotesOverride()
{
    return R"TOML([parts.Soprano]
notes = """
f'2 f'2 | g'1
"""
)TOML";
}

} // namespace ope::fixtures
