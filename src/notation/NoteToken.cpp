// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NoteToken.hpp"

namespace OpenPsalm {

QString NoteToken::toString() const {
    QString out;

    // Pitch or Chord or Rest or Spacer
    if (kind == NoteKind::Rest) {
        out.append(QLatin1Char('r'));
    } else if (kind == NoteKind::Spacer) {
        out.append(QLatin1Char('s'));
    } else if (kind == NoteKind::Chord) {
        out.append(QLatin1Char('<'));
        for (size_t i = 0; i < pitches.size(); ++i) {
            if (i > 0) out.append(QLatin1Char(' '));
            out.append(pitches[i].toString());
        }
        out.append(QLatin1Char('>'));
    } else if (!pitches.empty()) {
        out.append(pitches[0].toString());
    }

    // Duration
    out.append(duration.toString());

    // Dedup offset
    if (dedupOffset.has_value()) {
        int v = dedupOffset.value();
        if (v >= 0) {
            out.append(QStringLiteral("/+%1").arg(v));
        } else {
            out.append(QStringLiteral("/%1").arg(v));
        }
    }

    // Articulations & modifiers in standard canonical order
    if (tie) out.append(QLatin1Char('~'));
    if (fermata) out.append(QLatin1Char('!'));
    if (staccato) out.append(QLatin1String("-."));

    if (sectionMarker == SectionMarker::Chorus) out.append(QLatin1String("@c"));
    if (sectionMarker == SectionMarker::Coda) out.append(QLatin1String("@e"));
    if (sharedSectionIndex > 0) out.append(QStringLiteral("@s%1").arg(sharedSectionIndex));

    switch (dynamic) {
        case Dynamic::None: break;
        case Dynamic::PPP: out.append(QLatin1String("%ppp")); break;
        case Dynamic::PP: out.append(QLatin1String("%pp")); break;
        case Dynamic::P: out.append(QLatin1String("%p")); break;
        case Dynamic::MP: out.append(QLatin1String("%mp")); break;
        case Dynamic::MF: out.append(QLatin1String("%mf")); break;
        case Dynamic::F: out.append(QLatin1String("%f")); break;
        case Dynamic::FF: out.append(QLatin1String("%ff")); break;
        case Dynamic::FFF: out.append(QLatin1String("%fff")); break;
        case Dynamic::FP: out.append(QLatin1String("%fp")); break;
        case Dynamic::SFZ: out.append(QLatin1String("%sfz")); break;
    }

    switch (hairpin) {
        case Hairpin::None: break;
        case Hairpin::CrescendoStart: out.append(QLatin1String("\\<")); break;
        case Hairpin::DecrescendoStart: out.append(QLatin1String("\\>")); break;
        case Hairpin::End: out.append(QLatin1String("\\!")); break;
    }

    switch (tempoSpanner) {
        case TempoSpanner::None: break;
        case TempoSpanner::Rit: out.append(QLatin1String("\\rit")); break;
        case TempoSpanner::Ritard: out.append(QLatin1String("\\ritard")); break;
        case TempoSpanner::Rall: out.append(QLatin1String("\\rall")); break;
        case TempoSpanner::Accel: out.append(QLatin1String("\\accel")); break;
        case TempoSpanner::String: out.append(QLatin1String("\\string")); break;
        case TempoSpanner::Atempo: out.append(QLatin1String("\\atempo")); break;
        case TempoSpanner::SpanEnd: out.append(QLatin1String("\\spanend")); break;
    }

    if (beamStart && slurStart) {
        out.append(QLatin1String("[("));
    } else {
        if (beamStart) out.append(QLatin1Char('['));
        if (slurStart) out.append(QLatin1Char('('));
    }

    if (dashedSlurStart) out.append(QLatin1String("-("));
    if (dashedSlurEnd) out.append(QLatin1String("-)"));

    if (beamEnd && slurEnd) {
        out.append(QLatin1String("])"));
    } else {
        if (slurEnd) out.append(QLatin1Char(')'));
        if (beamEnd) out.append(QLatin1Char(']'));
    }

    return out;
}

std::optional<NoteToken> NoteToken::fromString(const QString& tokenStr) {
    QString str = tokenStr.trimmed();
    if (str.isEmpty()) return std::nullopt;

    NoteToken tok;
    tok.rawToken = str;

    int idx = 0;

    // 0. Leading prefixes
    while (idx < str.length()) {
        QString rest = str.mid(idx);
        if (rest.startsWith(QLatin1String("[("))) { tok.beamStart = true; tok.slurStart = true; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("-("))) { tok.dashedSlurStart = true; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("@c"))) { tok.sectionMarker = SectionMarker::Chorus; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("@e"))) { tok.sectionMarker = SectionMarker::Coda; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("@s"))) {
            int sEnd = idx + 2;
            while (sEnd < str.length() && str[sEnd].isDigit()) { sEnd++; }
            tok.sharedSectionIndex = str.mid(idx + 2, sEnd - (idx + 2)).toInt();
            idx = sEnd;
            continue;
        }
        if (rest.startsWith(QLatin1String("\\ritard"))) { tok.tempoSpanner = TempoSpanner::Ritard; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\rit"))) { tok.tempoSpanner = TempoSpanner::Rit; idx += 4; continue; }
        if (rest.startsWith(QLatin1String("\\rall"))) { tok.tempoSpanner = TempoSpanner::Rall; idx += 5; continue; }
        if (rest.startsWith(QLatin1String("\\accel"))) { tok.tempoSpanner = TempoSpanner::Accel; idx += 6; continue; }
        if (rest.startsWith(QLatin1String("\\string"))) { tok.tempoSpanner = TempoSpanner::String; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\atempo"))) { tok.tempoSpanner = TempoSpanner::Atempo; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\spanend"))) { tok.tempoSpanner = TempoSpanner::SpanEnd; idx += 8; continue; }
        if (rest.startsWith(QLatin1String("\\<"))) { tok.hairpin = Hairpin::CrescendoStart; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("\\>"))) { tok.hairpin = Hairpin::DecrescendoStart; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("\\!"))) { tok.hairpin = Hairpin::End; idx += 2; continue; }
        if (rest.startsWith(QLatin1Char('('))) { tok.slurStart = true; idx += 1; continue; }
        if (rest.startsWith(QLatin1Char('['))) { tok.beamStart = true; idx += 1; continue; }
        break;
    }

    if (idx >= str.length()) {
        return std::nullopt; // Pure modifier with no pitch/duration
    }

    // 1. Kind and Pitch
    if (str[idx] == QLatin1Char('r')) {
        tok.kind = NoteKind::Rest;
        idx++;
    } else if (str[idx] == QLatin1Char('s')) {
        tok.kind = NoteKind::Spacer;
        idx++;
    } else if (str[idx] == QLatin1Char('<')) {
        tok.kind = NoteKind::Chord;
        int closeIdx = str.indexOf(QLatin1Char('>'), idx + 1);
        if (closeIdx < 0) return std::nullopt;

        QString inside = str.mid(idx + 1, closeIdx - (idx + 1));
        QStringList pitchTokens = inside.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (pitchTokens.isEmpty()) return std::nullopt;

        for (const QString& pt : pitchTokens) {
            auto p = Pitch::fromString(pt);
            if (!p.has_value()) return std::nullopt;
            tok.pitches.push_back(p.value());
        }
        idx = closeIdx + 1;
    } else {
        tok.kind = NoteKind::Note;
        // Pitch starts with a..g
        int pitchEnd = idx + 1;
        while (pitchEnd < str.length() && (str[pitchEnd] == QLatin1Char('i') ||
                                           str[pitchEnd] == QLatin1Char('s') ||
                                           str[pitchEnd] == QLatin1Char('e') ||
                                           str[pitchEnd] == QLatin1Char('\'') ||
                                           str[pitchEnd] == QLatin1Char(','))) {
            pitchEnd++;
        }
        QString pitchStr = str.mid(idx, pitchEnd - idx);
        auto p = Pitch::fromString(pitchStr);
        if (!p.has_value()) return std::nullopt;
        tok.pitches.push_back(p.value());
        idx = pitchEnd;
    }

    // 2. Duration (digits + dots)
    int durStart = idx;
    while (idx < str.length() && (str[idx].isDigit() || str[idx] == QLatin1Char('.'))) {
        idx++;
    }
    if (durStart == idx) {
        // Missing duration on note
        return std::nullopt;
    }
    auto d = Duration::fromString(str.mid(durStart, idx - durStart));
    if (!d.has_value()) return std::nullopt;
    tok.duration = d.value();

    // 3. Modifiers loop
    while (idx < str.length()) {
        QString rest = str.mid(idx);

        // Dedup offset (/+24, /-12, /48)
        if (rest.startsWith(QLatin1Char('/'))) {
            int slashEnd = idx + 1;
            while (slashEnd < str.length() && (str[slashEnd] == QLatin1Char('+') ||
                                               str[slashEnd] == QLatin1Char('-') ||
                                               str[slashEnd].isDigit())) {
                slashEnd++;
            }
            QString numStr = str.mid(idx + 1, slashEnd - (idx + 1));
            bool ok = false;
            int offsetVal = numStr.toInt(&ok);
            if (ok) {
                tok.dedupOffset = offsetVal;
            }
            idx = slashEnd;
            continue;
        }

        // Combined beam/slur
        if (rest.startsWith(QLatin1String("[("))) {
            tok.beamStart = true;
            tok.slurStart = true;
            idx += 2;
            continue;
        }
        if (rest.startsWith(QLatin1String("])"))) {
            tok.beamEnd = true;
            tok.slurEnd = true;
            idx += 2;
            continue;
        }

        // Dashed slur
        if (rest.startsWith(QLatin1String("-("))) {
            tok.dashedSlurStart = true;
            idx += 2;
            continue;
        }
        if (rest.startsWith(QLatin1String("-)"))) {
            tok.dashedSlurEnd = true;
            idx += 2;
            continue;
        }

        // Staccato
        if (rest.startsWith(QLatin1String("-."))) {
            tok.staccato = true;
            idx += 2;
            continue;
        }

        // Section markers
        if (rest.startsWith(QLatin1String("@c"))) {
            tok.sectionMarker = SectionMarker::Chorus;
            idx += 2;
            continue;
        }
        if (rest.startsWith(QLatin1String("@e"))) {
            tok.sectionMarker = SectionMarker::Coda;
            idx += 2;
            continue;
        }
        if (rest.startsWith(QLatin1String("@s"))) {
            int sEnd = idx + 2;
            while (sEnd < str.length() && str[sEnd].isDigit()) {
                sEnd++;
            }
            tok.sharedSectionIndex = str.mid(idx + 2, sEnd - (idx + 2)).toInt();
            idx = sEnd;
            continue;
        }

        // Dynamics (%...)
        if (rest.startsWith(QLatin1String("%ppp"))) { tok.dynamic = Dynamic::PPP; idx += 4; continue; }
        if (rest.startsWith(QLatin1String("%pp"))) { tok.dynamic = Dynamic::PP; idx += 3; continue; }
        if (rest.startsWith(QLatin1String("%mp"))) { tok.dynamic = Dynamic::MP; idx += 3; continue; }
        if (rest.startsWith(QLatin1String("%mf"))) { tok.dynamic = Dynamic::MF; idx += 3; continue; }
        if (rest.startsWith(QLatin1String("%fff"))) { tok.dynamic = Dynamic::FFF; idx += 4; continue; }
        if (rest.startsWith(QLatin1String("%ff"))) { tok.dynamic = Dynamic::FF; idx += 3; continue; }
        if (rest.startsWith(QLatin1String("%fp"))) { tok.dynamic = Dynamic::FP; idx += 3; continue; }
        if (rest.startsWith(QLatin1String("%sfz"))) { tok.dynamic = Dynamic::SFZ; idx += 4; continue; }
        if (rest.startsWith(QLatin1String("%p"))) { tok.dynamic = Dynamic::P; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("%f"))) { tok.dynamic = Dynamic::F; idx += 2; continue; }

        // Hairpins
        if (rest.startsWith(QLatin1String("\\<"))) { tok.hairpin = Hairpin::CrescendoStart; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("\\>"))) { tok.hairpin = Hairpin::DecrescendoStart; idx += 2; continue; }
        if (rest.startsWith(QLatin1String("\\!"))) { tok.hairpin = Hairpin::End; idx += 2; continue; }

        // Tempo spanners
        if (rest.startsWith(QLatin1String("\\ritard"))) { tok.tempoSpanner = TempoSpanner::Ritard; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\rit"))) { tok.tempoSpanner = TempoSpanner::Rit; idx += 4; continue; }
        if (rest.startsWith(QLatin1String("\\rall"))) { tok.tempoSpanner = TempoSpanner::Rall; idx += 5; continue; }
        if (rest.startsWith(QLatin1String("\\accel"))) { tok.tempoSpanner = TempoSpanner::Accel; idx += 6; continue; }
        if (rest.startsWith(QLatin1String("\\string"))) { tok.tempoSpanner = TempoSpanner::String; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\atempo"))) { tok.tempoSpanner = TempoSpanner::Atempo; idx += 7; continue; }
        if (rest.startsWith(QLatin1String("\\spanend"))) { tok.tempoSpanner = TempoSpanner::SpanEnd; idx += 8; continue; }

        // Single character modifiers
        QChar c = str[idx];
        if (c == QLatin1Char('~')) { tok.tie = true; idx++; continue; }
        if (c == QLatin1Char('!')) { tok.fermata = true; idx++; continue; }
        if (c == QLatin1Char('(')) { tok.slurStart = true; idx++; continue; }
        if (c == QLatin1Char(')')) { tok.slurEnd = true; idx++; continue; }
        if (c == QLatin1Char('[')) { tok.beamStart = true; idx++; continue; }
        if (c == QLatin1Char(']')) { tok.beamEnd = true; idx++; continue; }

        // Unrecognized character
        return std::nullopt;
    }

    return tok;
}

int Measure::totalInternalTicks() const {
    int sum = 0;
    for (const auto& ev : events) {
        sum += ev.duration.toInternalTicks();
    }
    return sum;
}

int Measure::totalPhraseTicks() const {
    return Ticks::internalToPhrase(totalInternalTicks());
}

QString Measure::toString() const {
    QString out;
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) out.append(QLatin1Char(' '));
        out.append(events[i].toString());
    }
    return out;
}

} // namespace OpenPsalm
