// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QIcon>
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OpenPsalmEditor"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("OpenPsalm"));
    app.setOrganizationDomain(QStringLiteral("openpsalm.com"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenPsalm Editor - Desktop SATB Score and TOML Editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Optional song.toml or translation overlay file to open."));

    parser.process(app);

    OpenPsalm::MainWindow window;
    window.show();

    const QStringList positionalArgs = parser.positionalArguments();
    if (!positionalArgs.isEmpty()) {
        window.openSong(positionalArgs.first());
    }

    return app.exec();
}
