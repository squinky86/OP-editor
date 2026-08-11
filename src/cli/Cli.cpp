// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Cli.h"

#include "core/Library.h"
#include "core/Playback.h"
#include "core/Song.h"
#include "core/Validator.h"

#include <QDir>
#include <QFileInfo>
#include <QTextStream>

namespace ope::cli {
namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

/// Force every token to be re-emitted and confirm the result parses back to the
/// same notes. This is the real fidelity test: a byte-identical save of an
/// untouched file only proves nothing was written.
bool checkReemitPart(const Part &part, QStringList &problems)
{
    NoteStream copy = part.stream;
    for (Measure &measure : copy.measures()) {
        for (Event &event : measure.events)
            event.dirty = true;
    }
    const QString emitted = copy.toSource();
    QList<TokenIssue> issues;
    const NoteStream reparsed = NoteStream::parse(emitted, &issues);

    if (reparsed.measureCount() != part.stream.measureCount()) {
        problems.append(QStringLiteral("%1: re-emitted stream has %2 measures, expected %3")
                            .arg(part.name)
                            .arg(reparsed.measureCount())
                            .arg(part.stream.measureCount()));
        return false;
    }
    for (qsizetype m = 0; m < reparsed.measureCount(); ++m) {
        const QList<Event> &before = part.stream.measures().at(m).events;
        const QList<Event> &after = reparsed.measures().at(m).events;
        if (before.size() != after.size()) {
            problems.append(QStringLiteral("%1 m%2: %3 events after re-emit, expected %4")
                                .arg(part.name)
                                .arg(m + 1)
                                .arg(after.size())
                                .arg(before.size()));
            return false;
        }
        for (qsizetype e = 0; e < before.size(); ++e) {
            const Event &a = before.at(e);
            const Event &b = after.at(e);
            const auto mismatch = [&](const QString &what) {
                problems.append(QStringLiteral("%1 m%2 event %3: %4 changed (`%5` → `%6`)")
                                    .arg(part.name)
                                    .arg(m + 1)
                                    .arg(e)
                                    .arg(what, a.raw, b.text()));
            };
            if (a.kind != b.kind) {
                mismatch(QStringLiteral("kind"));
                return false;
            }
            if (a.pitches.size() != b.pitches.size()) {
                mismatch(QStringLiteral("pitch count"));
                return false;
            }
            for (qsizetype p = 0; p < a.pitches.size(); ++p) {
                if (!(a.pitches.at(p) == b.pitches.at(p))) {
                    mismatch(QStringLiteral("pitch"));
                    return false;
                }
            }
            if (!(a.duration == b.duration)) {
                mismatch(QStringLiteral("duration"));
                return false;
            }
            if (a.tie != b.tie || a.slurStart != b.slurStart || a.slurEnd != b.slurEnd
                || a.dashedSlurStart != b.dashedSlurStart || a.dashedSlurEnd != b.dashedSlurEnd
                || a.beamStart != b.beamStart || a.beamEnd != b.beamEnd
                || a.fermata != b.fermata || a.staccato != b.staccato
                || a.chorusStart != b.chorusStart || a.codaStart != b.codaStart) {
                mismatch(QStringLiteral("a flag"));
                return false;
            }
            if (a.dynamic != b.dynamic || a.hairpin != b.hairpin
                || a.tempoSpanner != b.tempoSpanner || a.spannerEnd != b.spannerEnd
                || a.dedupOffset != b.dedupOffset) {
                mismatch(QStringLiteral("a marking"));
                return false;
            }
            const bool tupletA = a.tuplet.has_value();
            const bool tupletB = b.tuplet.has_value();
            if (tupletA != tupletB
                || (tupletA
                    && (a.tuplet->actual != b.tuplet->actual
                        || a.tuplet->normal != b.tuplet->normal))) {
                mismatch(QStringLiteral("tuplet"));
                return false;
            }
        }
    }
    return true;
}

void checkFile(const QString &path, const Options &options, CheckSummary &totals,
    const QString &baseLanguage, const SongDocument *base = nullptr)
{
    ++totals.files;
    const auto loaded = io::load(path);
    if (!loaded) {
        ++totals.parseFailures;
        if (!options.quiet)
            out() << "PARSE  " << loaded.error().formatted() << "\n";
        return;
    }
    const SongDocument &doc = *loaded;

    if (options.checkRoundTrip) {
        const QByteArray written = io::serialize(doc);
        if (written != doc.originalBytes) {
            ++totals.roundTripFailures;
            if (!options.quiet) {
                out() << "ROUND  " << path << ": saving an unmodified file changed "
                      << (written.size() - doc.originalBytes.size()) << " byte(s)\n";
            }
        }
    }

    if (options.checkReemit) {
        QStringList problems;
        for (const Part &part : doc.parts) {
            if (!checkReemitPart(part, problems))
                break;
        }
        if (!problems.isEmpty()) {
            ++totals.reemitFailures;
            if (!options.quiet) {
                for (const QString &problem : problems)
                    out() << "REEMIT " << path << ": " << problem << "\n";
            }
        }
    }

    if (options.validate) {
        const bool languageKnown = i18n::isKnown(doc.language);
        // A translation is checked the way the seeder checks it: merged onto its
        // base, so an overlay that legitimately declares only lyrics is not
        // reported as a song with no parts.
        const SongDocument merged = base ? mergeOverlay(*base, doc) : doc;
        const QList<Finding> findings = validate(merged, languageKnown, baseLanguage);
        const int errors = countBySeverity(findings, Severity::Error);
        totals.errors += errors;
        totals.warnings += countBySeverity(findings, Severity::Warning);
        totals.infos += countBySeverity(findings, Severity::Info);
        if (errors > 0)
            ++totals.songsWithErrors;
        for (const Finding &finding : findings) {
            if (options.quiet)
                continue;
            if (finding.severity == Severity::Warning && !options.warnings)
                continue;
            if (finding.severity == Severity::Info && !options.info)
                continue;
            const char *tag = finding.severity == Severity::Error ? "ERROR "
                : finding.severity == Severity::Warning           ? "WARN  "
                                                                  : "INFO  ";
            out() << tag << QFileInfo(path).absoluteFilePath() << "  " << finding.formatted()
                  << "\n";
        }
    }
}

CheckSummary runChecks(const Options &options, bool announceMissingCorpus)
{
    CheckSummary totals;
    const QFileInfo info(options.root);

    const auto shouldCancel = [&] {
        if (options.cancelled && options.cancelled()) {
            totals.cancelled = true;
            return true;
        }
        return false;
    };
    const auto reportProgress = [&](int total, const QString &path) {
        if (options.progress)
            options.progress(totals.files, total, path);
    };

    if (info.isFile()) {
        if (shouldCancel())
            return totals;
        const QString overlayLanguage = i18n::codeFromFilename(info.fileName());
        if (overlayLanguage.isEmpty()) {
            checkFile(options.root, options, totals, QString());
        } else {
            const QString basePath = info.dir().filePath(QStringLiteral("song.toml"));
            const auto base = io::load(basePath);
            if (!base) {
                ++totals.parseFailures;
                if (!options.quiet) {
                    out() << "BASE   " << options.root
                          << ": cannot validate this overlay without " << basePath << ": "
                          << base.error().formatted() << "\n";
                }
            }
            checkFile(options.root, options, totals,
                base ? base->language : QString(), base ? &*base : nullptr);
        }
        reportProgress(1, options.root);
        return totals;
    }

    Library library;
    library.setRoot(options.root);
    library.rescan();
    if (library.entries().isEmpty()) {
        totals.foundCorpus = false;
        if (announceMissingCorpus) {
            out() << "No song directories found under " << options.root << "\n";
            out().flush();
        }
        return totals;
    }
    int handled = 0;
    int totalFiles = 0;
    int countedSongs = 0;
    for (const SongEntry &entry : library.entries()) {
        if (options.limit >= 0 && countedSongs >= options.limit)
            break;
        ++countedSongs;
        ++totalFiles;
        totalFiles += entry.translationPaths.size();
    }
    for (const SongEntry &entry : library.entries()) {
        if (options.limit >= 0 && handled >= options.limit)
            break;
        ++handled;
        if (shouldCancel())
            return totals;
        checkFile(entry.basePath, options, totals, QString());
        reportProgress(totalFiles, entry.basePath);
        const auto base = io::load(entry.basePath);
        for (const QString &path : entry.translationPaths) {
            if (shouldCancel())
                return totals;
            checkFile(path, options, totals, entry.baseLanguage,
                base ? &*base : nullptr);
            reportProgress(totalFiles, path);
        }
    }
    return totals;
}

} // namespace

bool CheckSummary::passed() const noexcept
{
    return !cancelled && foundCorpus && files > 0 && parseFailures == 0 && roundTripFailures == 0
        && reemitFailures == 0 && errors == 0;
}

QString CheckSummary::description() const
{
    if (cancelled)
        return QStringLiteral("Checking was cancelled after %1 file(s).").arg(files);
    if (!foundCorpus)
        return QStringLiteral("No numbered song directories were found.");
    return QStringLiteral(
        "%1 file(s) checked; %2 parse failure(s); %3 round-trip change(s); "
        "%4 note re-emission mismatch(es); %5 validation error(s); %6 warning(s).")
        .arg(files)
        .arg(parseFailures)
        .arg(roundTripFailures)
        .arg(reemitFailures)
        .arg(errors)
        .arg(warnings);
}

QString usage()
{
    return QStringLiteral(
        "OpenPsalm Editor — headless checks\n"
        "\n"
        "  ope [song.toml]           start the graphical editor\n"
        "  ope --check <path>        check a songs directory or a single song.toml\n"
        "  ope --version             print the application version\n"
        "\n"
        "Options:\n"
        "  --no-roundtrip            skip the save-unchanged byte-equality check\n"
        "  --no-reemit               skip the note-token re-emission check\n"
        "  --no-validate             skip the rule engine\n"
        "  --errors-only             hide warnings\n"
        "  --info                    also show info-level findings\n"
        "  --limit N                 stop after N songs\n"
        "  --quiet                   summary only\n"
        "\n"
        "GUI automation:\n"
        "  --tab 0|1|2               open score, lyrics, or source\n"
        "  --screenshot FILE         save a PNG after startup and exit\n"
        "\n"
        "With no arguments the graphical editor starts.\n");
}

bool parse(const QStringList &arguments, Options &options, int &exitCode)
{
    exitCode = 0;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument == QLatin1String("--check")) {
            if (i + 1 >= arguments.size()) {
                out() << "--check needs a path\n";
                exitCode = 2;
                return false;
            }
            options.root = arguments.at(++i);
        } else if (argument == QLatin1String("--no-roundtrip")) {
            options.checkRoundTrip = false;
        } else if (argument == QLatin1String("--no-reemit")) {
            options.checkReemit = false;
        } else if (argument == QLatin1String("--no-validate")) {
            options.validate = false;
        } else if (argument == QLatin1String("--errors-only")) {
            options.warnings = false;
        } else if (argument == QLatin1String("--info")) {
            options.info = true;
        } else if (argument == QLatin1String("--quiet")) {
            options.quiet = true;
        } else if (argument == QLatin1String("--limit")) {
            if (i + 1 >= arguments.size()) {
                out() << "--limit needs a positive number\n";
                out().flush();
                exitCode = 2;
                return false;
            }
            bool ok = false;
            const int limit = arguments.at(++i).toInt(&ok);
            if (!ok || limit <= 0) {
                out() << "--limit needs a positive number\n";
                out().flush();
                exitCode = 2;
                return false;
            }
            options.limit = limit;
        } else if (argument == QLatin1String("--help") || argument == QLatin1String("-h")) {
            out() << usage();
            out().flush();
            return false;
        } else {
            out() << "unknown option: " << argument << "\n" << usage();
            out().flush();
            exitCode = 2;
            return false;
        }
    }
    return true;
}

CheckSummary check(const Options &options)
{
    Options silent = options;
    silent.quiet = true;
    return runChecks(silent, false);
}

int run(const Options &options)
{
    const CheckSummary totals = runChecks(options, true);
    if (!totals.foundCorpus)
        return 2;

    out() << "\n";
    out() << "files checked      " << totals.files << "\n";
    out() << "parse failures     " << totals.parseFailures << "\n";
    out() << "round-trip changes " << totals.roundTripFailures << "\n";
    out() << "re-emit mismatches " << totals.reemitFailures << "\n";
    out() << "errors             " << totals.errors << " (in " << totals.songsWithErrors
          << " file(s))\n";
    out() << "warnings           " << totals.warnings << "\n";
    if (options.info)
        out() << "info               " << totals.infos << "\n";
    out().flush();

    return totals.passed() ? 0 : 1;
}

} // namespace ope::cli
