// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// OpenPsalm Editor. With no arguments the editor starts; with `--check` it runs
// the format checks headlessly over a songs directory.

#include "cli/Cli.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <utility>

int main(int argc, char **argv)
{
    QStringList arguments;
    arguments.reserve(argc - 1);
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    if (arguments.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "OpenPsalm Editor " << OPE_VERSION << "\n";
        return 0;
    }

    // The headless path takes no GUI: it must run over SSH and in CI.
    if (arguments.contains(QStringLiteral("--check"))
        || arguments.contains(QStringLiteral("--help"))
        || arguments.contains(QStringLiteral("-h"))) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("ope"));
        ope::cli::Options options;
        int exitCode = 0;
        if (!ope::cli::parse(arguments, options, exitCode))
            return exitCode;
        return ope::cli::run(options);
    }

    const int shotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 >= arguments.size()) {
        QTextStream(stderr) << "--screenshot needs an output PNG path\n";
        return 2;
    }
    const int tabIndex = arguments.indexOf(QStringLiteral("--tab"));
    int requestedTab = -1;
    if (tabIndex >= 0) {
        bool ok = false;
        if (tabIndex + 1 < arguments.size())
            requestedTab = arguments.at(tabIndex + 1).toInt(&ok);
        if (!ok || requestedTab < 0 || requestedTab > 2) {
            QTextStream(stderr) << "--tab needs 0 (score), 1 (lyrics), or 2 (source)\n";
            return 2;
        }
    }
    for (const QString &argument : std::as_const(arguments)) {
        if (argument.startsWith(QLatin1String("--"))
            && argument != QLatin1String("--screenshot")
            && argument != QLatin1String("--tab")) {
            QTextStream(stderr) << "unknown option: " << argument << "\n";
            return 2;
        }
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenPsalm"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("openpsalm.com"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenPsalm Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPE_VERSION));

    ope::ui::MainWindow window;
    window.show();

    // `--screenshot <file>` renders the window once and exits: how the score
    // view is checked in CI and how the screenshots in the README are made.
    if (shotIndex >= 0) {
        const QString target = arguments.at(shotIndex + 1);
        QTimer::singleShot(1200, &window, [&window, target] {
            const bool saved = window.grab().save(target, "PNG");
            if (!saved)
                QTextStream(stderr) << "could not write screenshot to " << target << "\n";
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }

    if (requestedTab >= 0)
        window.selectTab(requestedTab);

    // A path on the command line opens straight away.
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument == QLatin1String("--screenshot") || argument == QLatin1String("--tab")) {
            ++i;  // skip its value
            continue;
        }
        if (QFileInfo(argument).isFile()) {
            window.openPath(argument);
            break;
        }
    }
    return app.exec();
}
