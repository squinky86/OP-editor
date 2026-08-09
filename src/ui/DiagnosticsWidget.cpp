// Copyright (C) 2026 Jon Hood, OpenPsalm.com
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DiagnosticsWidget.hpp"
#include <QVBoxLayout>
#include <QHeaderView>

namespace OpenPsalm {

DiagnosticsWidget::DiagnosticsWidget(QWidget* parent)
    : QWidget(parent),
      m_table(new QTableWidget(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Severity"),
        QStringLiteral("Code"),
        QStringLiteral("Message"),
        QStringLiteral("Part"),
        QStringLiteral("Measure")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        if (row >= 0 && row < static_cast<int>(m_diagnostics.size())) {
            emit diagnosticSelected(m_diagnostics[row]);
        }
    });

    layout->addWidget(m_table);
}

void DiagnosticsWidget::setDiagnostics(const std::vector<Diagnostic>& diags) {
    m_diagnostics = diags;
    m_table->setRowCount(static_cast<int>(diags.size()));

    for (int r = 0; r < static_cast<int>(diags.size()); ++r) {
        const auto& d = diags[r];

        auto* sevItem = new QTableWidgetItem(d.severityString());
        if (d.severity == DiagnosticSeverity::Error) {
            sevItem->setForeground(QBrush(QColor(220, 50, 50)));
        } else if (d.severity == DiagnosticSeverity::Warning) {
            sevItem->setForeground(QBrush(QColor(210, 140, 20)));
        } else {
            sevItem->setForeground(QBrush(QColor(80, 140, 200)));
        }

        m_table->setItem(r, 0, sevItem);
        m_table->setItem(r, 1, new QTableWidgetItem(d.code));
        m_table->setItem(r, 2, new QTableWidgetItem(d.message));
        m_table->setItem(r, 3, new QTableWidgetItem(d.partName.isEmpty() ? QStringLiteral("-") : d.partName));
        m_table->setItem(r, 4, new QTableWidgetItem(d.measure > 0 ? QString::number(d.measure) : QStringLiteral("-")));
    }
}

} // namespace OpenPsalm
