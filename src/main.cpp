// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
//
// OpenPsalm Editor. With no arguments the editor starts; with `--check` it runs
// the format checks headlessly over a songs directory.

#include "cli/Cli.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>

int main(int argc, char **argv)
{
    QStringList arguments;
    arguments.reserve(argc - 1);
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

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

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenPsalm"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("openpsalm.com"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenPsalm Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPE_VERSION));

    ope::ui::MainWindow window;
    window.show();

    // `--screenshot <file>` renders the window once and exits: how the score
    // view is checked in CI and how the screenshots in the README are made.
    const int shotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < arguments.size()) {
        const QString target = arguments.at(shotIndex + 1);
        QTimer::singleShot(1200, &window, [&window, target] {
            window.grab().save(target);
            QCoreApplication::quit();
        });
    }

    const int tabIndex = arguments.indexOf(QStringLiteral("--tab"));
    if (tabIndex >= 0 && tabIndex + 1 < arguments.size())
        window.selectTab(arguments.at(tabIndex + 1).toInt());

    // A path on the command line opens straight away.
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument.startsWith(QLatin1String("--"))) {
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
