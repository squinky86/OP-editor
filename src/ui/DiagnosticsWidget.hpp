// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QTableWidget>
#include "core/Common.hpp"

namespace OpenPsalm {

class DiagnosticsWidget : public QWidget {
    Q_OBJECT

public:
    explicit DiagnosticsWidget(QWidget* parent = nullptr);
    void setDiagnostics(const std::vector<Diagnostic>& diags);

signals:
    void diagnosticSelected(const Diagnostic& diag);

private:
    QTableWidget* m_table{nullptr};
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace OpenPsalm
