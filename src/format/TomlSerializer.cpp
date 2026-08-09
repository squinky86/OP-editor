// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TomlSerializer.hpp"
#include <QFile>
#include <QTextStream>

namespace OpenPsalm {

namespace {

QString escapeTomlString(const QString& str) {
    QString out = str;
    out.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    out.replace(QLatin1String("\""), QLatin1String("\\\""));
    return QStringLiteral("\"%1\"").arg(out);
}

} // anonymous namespace

QString TomlSerializer::serialize(const SongData& song) {
    QString out;
    QTextStream ts(&out);

    // 1. Scalar Top-Level Metadata
    if (!song.isTranslationOverlay || song.overridesTitle) {
        ts << "title = " << escapeTomlString(song.title) << "\n";
    }
    if (song.subtitle.has_value() && (!song.isTranslationOverlay || song.overridesSubtitle)) {
        ts << "subtitle = " << escapeTomlString(song.subtitle.value()) << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesActive) {
        ts << "active = " << (song.active ? "true" : "false") << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesLanguage) {
        ts << "language = " << escapeTomlString(song.language) << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesVerseCount) {
        ts << "verse_count = " << song.verseCount << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesKeySignature) {
        ts << "key_signature = " << escapeTomlString(song.keySignature) << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesTimeSig) {
        ts << "time_sig_numerator = " << song.timeSigNumerator << "\n";
        ts << "time_sig_denominator = " << song.timeSigDenominator << "\n";
    }
    if (!song.isTranslationOverlay || song.overridesTempoBpm) {
        ts << "tempo_bpm = " << song.tempoBpm << "\n";
    }

    if (song.commentary.has_value() && (!song.isTranslationOverlay || song.overridesCommentary)) {
        ts << "commentary = " << escapeTomlString(song.commentary.value()) << "\n";
    }

    // Time signature changes
    if (!song.timeSigChanges.empty() && (!song.isTranslationOverlay || song.overridesTimeSigChanges)) {
        ts << "\n";
        for (const auto& tsc : song.timeSigChanges) {
            ts << "[[time_sig_changes]]\n";
            ts << "measure = " << tsc.measure << "\n";
            ts << "numerator = " << tsc.numerator << "\n";
            ts << "denominator = " << tsc.denominator << "\n";
            ts << "duration = " << tsc.duration << "\n\n";
        }
    }

    // Phrase breaks
    auto writePhraseBreakArray = [&](const QString& fieldName, const std::vector<PhraseBreak>& breaks) {
        if (breaks.empty()) return;
        ts << fieldName << " = [";
        for (size_t i = 0; i < breaks.size(); ++i) {
            if (i > 0) ts << ", ";
            ts << "\"" << breaks[i].toString() << "\"";
        }
        ts << "]\n";
    };

    if (!song.isTranslationOverlay || song.overridesPhraseBreaks) {
        writePhraseBreakArray(QStringLiteral("phrase_breaks"), song.phraseBreaks);
    }
    if (!song.isTranslationOverlay || song.overridesOptionalPhraseBreaks) {
        writePhraseBreakArray(QStringLiteral("optional_phrase_breaks"), song.optionalPhraseBreaks);
    }
    if (!song.isTranslationOverlay || song.overridesNonBreakingPhraseBreaks) {
        writePhraseBreakArray(QStringLiteral("non_breaking_phrase_breaks"), song.nonBreakingPhraseBreaks);
    }

    // Converge verses
    if (!song.convergeVerses.empty()) {
        ts << "converge_verses = [";
        for (size_t i = 0; i < song.convergeVerses.size(); ++i) {
            if (i > 0) ts << ", ";
            ts << song.convergeVerses[i];
        }
        ts << "]\n";
    }

    // Copyrights
    if (!song.copyrights.isEmpty() && (!song.isTranslationOverlay || song.overridesCopyrights)) {
        ts << "\ncopyrights = [\n";
        for (const QString& line : song.copyrights) {
            ts << "    " << escapeTomlString(line) << ",\n";
        }
        ts << "]\n";
    }

    // 2. Parts
    QStringList orderedPartNames = song.partNamesInOrder();
    for (const QString& partName : orderedPartNames) {
        const PartData& part = song.parts[partName];
        ts << "\n[parts." << partName << "]\n";

        if (!song.isTranslationOverlay || part.overridesChoralType) {
            ts << "choral_type = " << escapeTomlString(choralTypeToString(part.choralType, part.customChoralType)) << "\n";
        }
        if (!song.isTranslationOverlay || part.overridesClef) {
            ts << "clef = " << escapeTomlString(clefToString(part.clef)) << "\n";
        }
        if (!song.isTranslationOverlay || part.overridesStaffNumber) {
            ts << "staff_number = " << part.staffNumber << "\n";
        }

        if (!song.isTranslationOverlay || part.overridesSuppressVerses) {
            if (!part.suppressVerses.empty()) {
                ts << "suppress_verses = [";
                for (size_t i = 0; i < part.suppressVerses.size(); ++i) {
                    if (i > 0) ts << ", ";
                    ts << part.suppressVerses[i];
                }
                ts << "]\n";
            }
        }

        if (!song.isTranslationOverlay || part.overridesSuppressVersesWhen) {
            if (!part.suppressVersesWhen.isEmpty()) {
                ts << "suppress_verses_when = [";
                for (int i = 0; i < part.suppressVersesWhen.size(); ++i) {
                    if (i > 0) ts << ", ";
                    ts << escapeTomlString(part.suppressVersesWhen[i]);
                }
                ts << "]\n";
            }
        }

        if (!part.spliceLyricsInto.isEmpty() && (!song.isTranslationOverlay || part.overridesSpliceLyricsInto)) {
            ts << "splice_lyrics_into = " << escapeTomlString(part.spliceLyricsInto) << "\n";
        }

        if (!song.isTranslationOverlay || part.overridesNotes) {
            ts << "notes = '''\n" << part.notesText.trimmed() << "\n'''\n";
        }

        // Per-part lyrics
        if (!part.lyrics.isEmpty() && (!song.isTranslationOverlay || part.overridesLyrics)) {
            for (auto lit = part.lyrics.begin(); lit != part.lyrics.end(); ++lit) {
                ts << "\n[parts." << partName << ".lyrics." << lit.key() << "]\n";
                ts << "text = " << escapeTomlString(lit.value()) << "\n";
            }
        }
    }

    // 3. Global Lyrics
    if (!song.lyrics.allSections().isEmpty() && (!song.isTranslationOverlay || song.overridesLyrics)) {
        for (const QString& secKey : song.lyrics.keys()) {
            ts << "\n[lyrics." << secKey << "]\n";
            ts << "text = " << escapeTomlString(song.lyrics.section(secKey)) << "\n";
        }
    }

    return out;
}

bool TomlSerializer::saveToFile(const SongData& song, const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QString serialized = serialize(song);
    QByteArray bytes = serialized.toUtf8();
    return file.write(bytes) == bytes.size();
}

} // namespace OpenPsalm
