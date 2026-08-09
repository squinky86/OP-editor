// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NoteSerializer.hpp"

namespace OpenPsalm {

QString NoteSerializer::serialize(const std::vector<Measure>& measures, int measuresPerLine) {
    if (measures.empty()) return QString();

    QStringList lines;
    QString currentLine;
    int measureCountInLine = 0;

    for (size_t i = 0; i < measures.size(); ++i) {
        const Measure& m = measures[i];
        QString measureStr = m.toString();

        if (measureCountInLine > 0) {
            currentLine.append(QLatin1String(" | "));
        }
        currentLine.append(measureStr);
        measureCountInLine++;

        if (measureCountInLine >= measuresPerLine && i + 1 < measures.size()) {
            currentLine.append(QLatin1String(" |"));
            lines.append(currentLine);
            currentLine.clear();
            measureCountInLine = 0;
        }
    }

    if (!currentLine.isEmpty()) {
        lines.append(currentLine);
    }

    return lines.join(QLatin1String("\n"));
}

} // namespace OpenPsalm
