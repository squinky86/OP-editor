// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TomlLoader.hpp"
#include "notation/NoteParser.hpp"
#include "third_party/tomlplusplus/toml.hpp"
#include <QFile>
#include <QFileInfo>
#include <string_view>

namespace OpenPsalm {

LoadResult TomlLoader::loadFile(const QString& filePath) {
    LoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.code = QStringLiteral("FILE_READ_ERROR");
        diag.message = QStringLiteral("Could not open file: %1").arg(file.errorString());
        diag.filePath = filePath;
        result.diagnostics.push_back(diag);
        result.success = false;
        return result;
    }

    QString content = QString::fromUtf8(file.readAll());
    return loadFromString(content, filePath);
}

LoadResult TomlLoader::loadFromString(const QString& tomlContent, const QString& filePath) {
    LoadResult result;
    std::string utf8 = tomlContent.toStdString();

    toml::table tbl;
    try {
        tbl = toml::parse(utf8);
    } catch (const toml::parse_error& err) {
        Diagnostic diag;
        diag.severity = DiagnosticSeverity::Error;
        diag.code = QStringLiteral("TOML_PARSE_ERROR");
        diag.message = QString::fromStdString(std::string(err.description()));
        diag.filePath = filePath;
        SourceSpan span;
        span.startLine = static_cast<int>(err.source().begin.line);
        span.startColumn = static_cast<int>(err.source().begin.column);
        span.endLine = static_cast<int>(err.source().end.line);
        span.endColumn = static_cast<int>(err.source().end.column);
        diag.span = span;
        result.diagnostics.push_back(diag);
        result.success = false;
        return result;
    }

    SongData& song = result.songData;

    // Check if this is an overlay
    QFileInfo fi(filePath);
    if (fi.fileName().startsWith(QLatin1String("song_"))) {
        song.isTranslationOverlay = true;
    }

    // 1. Top level fields
    if (auto val = tbl["title"].value<std::string>()) {
        song.title = QString::fromStdString(*val);
        song.overridesTitle = true;
    }
    if (auto val = tbl["subtitle"].value<std::string>()) {
        song.subtitle = QString::fromStdString(*val);
        song.overridesSubtitle = true;
    }
    if (auto val = tbl["active"].value<bool>()) {
        song.active = *val;
        song.overridesActive = true;
    }
    if (auto val = tbl["language"].value<std::string>()) {
        song.language = QString::fromStdString(*val);
        song.overridesLanguage = true;
    }
    if (auto val = tbl["verse_count"].value<int64_t>()) {
        song.verseCount = static_cast<int>(*val);
        song.overridesVerseCount = true;
    }
    if (auto val = tbl["key_signature"].value<std::string>()) {
        song.keySignature = QString::fromStdString(*val);
        song.overridesKeySignature = true;
    }
    if (auto val = tbl["time_sig_numerator"].value<int64_t>()) {
        song.timeSigNumerator = static_cast<int>(*val);
        song.overridesTimeSig = true;
    }
    if (auto val = tbl["time_sig_denominator"].value<int64_t>()) {
        song.timeSigDenominator = static_cast<int>(*val);
        song.overridesTimeSig = true;
    }
    if (auto val = tbl["tempo_bpm"].value<int64_t>()) {
        song.tempoBpm = static_cast<int>(*val);
        song.overridesTempoBpm = true;
    }
    if (auto val = tbl["commentary"].value<std::string>()) {
        song.commentary = QString::fromStdString(*val);
        song.overridesCommentary = true;
    }

    // Copyrights array
    if (auto arr = tbl["copyrights"].as_array()) {
        song.overridesCopyrights = true;
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                song.copyrights.append(QString::fromStdString(*s));
            }
        }
    }

    // Converge verses array
    if (auto arr = tbl["converge_verses"].as_array()) {
        for (const auto& elem : *arr) {
            if (auto v = elem.value<int64_t>()) {
                song.convergeVerses.push_back(static_cast<int>(*v));
            }
        }
    }

    // Phrase breaks
    if (auto arr = tbl["phrase_breaks"].as_array()) {
        song.overridesPhraseBreaks = true;
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                auto pb = PhraseBreak::fromString(QString::fromStdString(*s), PhraseBreakKind::Required);
                if (pb.has_value()) {
                    song.phraseBreaks.push_back(pb.value());
                }
            }
        }
    }

    if (auto arr = tbl["optional_phrase_breaks"].as_array()) {
        song.overridesOptionalPhraseBreaks = true;
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                auto pb = PhraseBreak::fromString(QString::fromStdString(*s), PhraseBreakKind::Optional);
                if (pb.has_value()) {
                    song.optionalPhraseBreaks.push_back(pb.value());
                }
            }
        }
    }

    if (auto arr = tbl["non_breaking_phrase_breaks"].as_array()) {
        song.overridesNonBreakingPhraseBreaks = true;
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                auto pb = PhraseBreak::fromString(QString::fromStdString(*s), PhraseBreakKind::NonBreaking);
                if (pb.has_value()) {
                    song.nonBreakingPhraseBreaks.push_back(pb.value());
                }
            }
        }
    }

    // Time signature changes
    if (auto arr = tbl["time_sig_changes"].as_array()) {
        song.overridesTimeSigChanges = true;
        for (const auto& elem : *arr) {
            if (auto t = elem.as_table()) {
                TimeSignatureChange tsc;
                if (auto v = (*t)["measure"].value<int64_t>()) tsc.measure = static_cast<int>(*v);
                if (auto v = (*t)["numerator"].value<int64_t>()) tsc.numerator = static_cast<int>(*v);
                if (auto v = (*t)["denominator"].value<int64_t>()) tsc.denominator = static_cast<int>(*v);
                if (auto v = (*t)["duration"].value<int64_t>()) tsc.duration = static_cast<int>(*v);
                song.timeSigChanges.push_back(tsc);
            }
        }
    }

    // 2. Global Lyrics [lyrics.KEY]
    if (auto lyricsTbl = tbl["lyrics"].as_table()) {
        song.overridesLyrics = true;
        for (auto&& [k, v] : *lyricsTbl) {
            QString secKey = QString::fromUtf8(k.str().data(), static_cast<int>(k.str().size()));
            if (auto subTbl = v.as_table()) {
                if (auto s = (*subTbl)["text"].value<std::string>()) {
                    song.lyrics.setSection(secKey, QString::fromStdString(*s));
                }
            } else if (auto s = v.value<std::string>()) {
                song.lyrics.setSection(secKey, QString::fromStdString(*s));
            }
        }
    }

    // 3. Parts [parts.NAME]
    if (auto partsTbl = tbl["parts"].as_table()) {
        for (auto&& [pk, pv] : *partsTbl) {
            QString partName = QString::fromUtf8(pk.str().data(), static_cast<int>(pk.str().size()));
            if (auto pt = pv.as_table()) {
                PartData part;
                part.name = partName;

                if (auto val = (*pt)["choral_type"].value<std::string>()) {
                    QString ctStr = QString::fromStdString(*val);
                    part.choralType = choralTypeFromString(ctStr);
                    if (part.choralType == ChoralType::Custom) {
                        part.customChoralType = ctStr;
                    }
                    part.overridesChoralType = true;
                }

                if (auto val = (*pt)["clef"].value<std::string>()) {
                    auto c = clefFromString(QString::fromStdString(*val));
                    if (c.has_value()) {
                        part.clef = c.value();
                    }
                    part.overridesClef = true;
                }

                if (auto val = (*pt)["staff_number"].value<int64_t>()) {
                    part.staffNumber = static_cast<int>(*val);
                    part.overridesStaffNumber = true;
                }

                if (auto val = (*pt)["notes"].value<std::string>()) {
                    part.notesText = QString::fromStdString(*val);
                    part.overridesNotes = true;

                    // Parse notes stream
                    auto parseRes = NoteParser::parse(part.notesText, partName, filePath);
                    part.parsedMeasures = parseRes.measures;
                    for (const auto& d : parseRes.diagnostics) {
                        result.diagnostics.push_back(d);
                    }
                }

                if (auto arr = (*pt)["suppress_verses"].as_array()) {
                    part.overridesSuppressVerses = true;
                    for (const auto& elem : *arr) {
                        if (auto v = elem.value<int64_t>()) {
                            part.suppressVerses.push_back(static_cast<int>(*v));
                        }
                    }
                }

                if (auto arr = (*pt)["suppress_verses_when"].as_array()) {
                    part.overridesSuppressVersesWhen = true;
                    for (const auto& elem : *arr) {
                        if (auto s = elem.value<std::string>()) {
                            part.suppressVersesWhen.append(QString::fromStdString(*s));
                        }
                    }
                }

                if (auto val = (*pt)["splice_lyrics_into"].value<std::string>()) {
                    part.spliceLyricsInto = QString::fromStdString(*val);
                    part.overridesSpliceLyricsInto = true;
                }

                // Per-part lyrics [parts.NAME.lyrics.KEY]
                if (auto partLyricsTbl = (*pt)["lyrics"].as_table()) {
                    part.overridesLyrics = true;
                    for (auto&& [lk, lv] : *partLyricsTbl) {
                        QString lKey = QString::fromUtf8(lk.str().data(), static_cast<int>(lk.str().size()));
                        if (auto subTbl = lv.as_table()) {
                            if (auto ls = (*subTbl)["text"].value<std::string>()) {
                                part.lyrics.insert(lKey, QString::fromStdString(*ls));
                            }
                        } else if (auto ls = lv.value<std::string>()) {
                            part.lyrics.insert(lKey, QString::fromStdString(*ls));
                        }
                    }
                }

                song.parts.insert(partName, part);
            }
        }
    }

    return result;
}

} // namespace OpenPsalm
