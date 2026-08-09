// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// `ope-check`: the format layer with no GUI attached, for CI and for checking a
// whole corpus from a terminal.

#include "Cli.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ope-check"));

    QStringList arguments = QCoreApplication::arguments();
    arguments.removeFirst();

    ope::cli::Options options;
    int exitCode = 0;
    if (!ope::cli::parse(arguments, options, exitCode))
        return exitCode;
    if (options.root.isEmpty()) {
        QTextStream(stdout) << ope::cli::usage();
        return 2;
    }
    return ope::cli::run(options);
}
