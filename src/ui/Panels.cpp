// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Panels.h"

#include "Dialogs.h"

#include <QCheckBox>
#include <QEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ope::ui {
namespace {

QString formatSeconds(double seconds)
{
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar(u'0'));
}

QColor severityColor(Severity severity)
{
    switch (severity) {
    case Severity::Error: return QColor(0xd1, 0x24, 0x2f);
    case Severity::Warning: return QColor(0x9a, 0x67, 0x00);
    case Severity::Info: return QColor(0x57, 0x60, 0x6a);
    }
    return {};
}

QString severityLabel(Severity severity)
{
    switch (severity) {
    case Severity::Error: return QObject::tr("Error");
    case Severity::Warning: return QObject::tr("Warning");
    case Severity::Info: return QObject::tr("Info");
    }
    return {};
}

void addTimeSignatureDenominators(QComboBox *box)
{
    for (const int denominator : ticks::TimeSignatureDenominators)
        box->addItem(QString::number(denominator), denominator);
}

void selectTimeSignatureDenominator(QComboBox *box, int denominator)
{
    for (int i = box->count() - 1; i >= 0; --i) {
        if (!ticks::isSupportedTimeSignatureDenominator(box->itemData(i).toInt()))
            box->removeItem(i);
    }
    int index = box->findData(denominator);
    if (index < 0) {
        box->addItem(QObject::tr("%1 (unsupported)").arg(denominator), denominator);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

} // namespace

// ------------------------------------------------------------- HeaderPanel ---

HeaderPanel::HeaderPanel(Session *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *layout = new QFormLayout(this);
    layout->setLabelAlignment(Qt::AlignRight);
    layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_title = new QLineEdit(this);
    m_subtitle = new QLineEdit(this);
    m_key = new QComboBox(this);
    for (const QString &key : validKeySignatures()) {
        m_key->addItem(key);
        m_key->addItem(key + u'm');
    }
    m_key->setEditable(true);
    m_timeNumerator = new QSpinBox(this);
    m_timeNumerator->setRange(1, 32);
    m_timeDenominator = new QComboBox(this);
    addTimeSignatureDenominators(m_timeDenominator);
    m_tempo = new QSpinBox(this);
    m_tempo->setRange(20, 300);
    m_tempo->setSuffix(tr(" bpm"));
    m_verseCount = new QSpinBox(this);
    m_verseCount->setRange(0, 40);
    m_active = new QCheckBox(tr("Published (active)"), this);
    m_converge = new QComboBox(this);
    m_converge->addItem(tr("automatic"), QVariant());
    m_converge->addItem(tr("always"), true);
    m_converge->addItem(tr("never"), false);
    m_converge->setToolTip(tr("converge_verses: print shared lines once instead of repeating "
                              "them under every verse. Automatic turns it on when the song "
                              "uses [lyrics.sN] sections."));
    m_timeSigChanges = new QPushButton(this);
    m_timeSigChanges->setToolTip(tr("Measures whose metre differs from the song's"));
    m_copyrights = new QPlainTextEdit(this);
    m_copyrights->setPlaceholderText(tr("One copyright line per row; the OpenPsalm arrangement "
                                        "line goes last"));
    m_copyrights->setMaximumHeight(96);
    m_commentary = new QPlainTextEdit(this);
    m_commentary->setPlaceholderText(tr("HTML shown under the score on the song page"));
    m_commentary->setMaximumHeight(72);
    m_inherited = new QLabel(this);
    m_inherited->setWordWrap(true);
    m_inherited->setStyleSheet(QStringLiteral("color: #57606a; font-style: italic;"));

    auto *metre = new QWidget(this);
    metre->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *metreLayout = new QHBoxLayout(metre);
    metreLayout->setContentsMargins(0, 0, 0, 0);
    metreLayout->addWidget(m_timeNumerator);
    metreLayout->addWidget(new QLabel(QStringLiteral("/"), metre));
    metreLayout->addWidget(m_timeDenominator);
    metreLayout->addStretch();

    layout->addRow(tr("Title"), m_title);
    layout->addRow(tr("Subtitle"), m_subtitle);
    layout->addRow(tr("Key"), m_key);
    layout->addRow(tr("Metre"), metre);
    layout->addRow(tr("Tempo"), m_tempo);
    layout->addRow(tr("Verses"), m_verseCount);
    layout->addRow(tr("Metre changes"), m_timeSigChanges);
    layout->addRow(tr("Converge verses"), m_converge);
    layout->addRow(QString(), m_active);
    layout->addRow(tr("Copyrights"), m_copyrights);
    layout->addRow(tr("Commentary"), m_commentary);
    layout->addRow(QString(), m_inherited);

    const auto onEdit = [this] { commit(); };
    const auto pending = [this] {
        if (m_loading || m_pending)
            return;
        m_pending = true;
        Q_EMIT pendingEditsChanged(true);
    };
    connect(m_title, &QLineEdit::textChanged, this, pending);
    connect(m_subtitle, &QLineEdit::textChanged, this, pending);
    connect(m_timeNumerator, &QSpinBox::valueChanged, this, pending);
    connect(m_tempo, &QSpinBox::valueChanged, this, pending);
    connect(m_verseCount, &QSpinBox::valueChanged, this, pending);
    connect(m_copyrights, &QPlainTextEdit::textChanged, this, pending);
    connect(m_commentary, &QPlainTextEdit::textChanged, this, pending);
    connect(m_title, &QLineEdit::editingFinished, this, onEdit);
    connect(m_subtitle, &QLineEdit::editingFinished, this, onEdit);
    connect(m_key, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_timeNumerator, &QSpinBox::editingFinished, this, onEdit);
    connect(m_timeDenominator, &QComboBox::currentIndexChanged, this, onEdit);
    connect(m_tempo, &QSpinBox::editingFinished, this, onEdit);
    connect(m_verseCount, &QSpinBox::editingFinished, this, onEdit);
    connect(m_active, &QCheckBox::toggled, this, onEdit);
    connect(m_converge, &QComboBox::currentIndexChanged, this, onEdit);
    connect(m_timeSigChanges, &QPushButton::clicked, this, &HeaderPanel::editTimeSigChanges);
    // A multi-line box has no editingFinished, and polling textChanged would put
    // one undo step on the stack per keystroke. Commit when focus leaves it.
    m_copyrights->installEventFilter(this);
    m_commentary->installEventFilter(this);

    connect(session, &Session::documentChanged, this, &HeaderPanel::refresh);
    connect(session, &Session::languageChanged, this, &HeaderPanel::refresh);
    refresh();
}

bool HeaderPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusOut && (watched == m_copyrights || watched == m_commentary))
        commit();
    return QWidget::eventFilter(watched, event);
}

void HeaderPanel::editTimeSigChanges()
{
    if (!m_session->isOpen())
        return;
    const SongDocument &effective = m_session->effectiveDocument();
    TimeSigChangesDialog dialog(
        effective.timeSigChanges.valueOr({}), effective.measureCount(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QList<TimeSigChange> changes = dialog.changes();
    m_session->mutate(tr("Edit metre changes"), [&changes](SongDocument &doc) {
        if (changes.isEmpty())
            doc.timeSigChanges.clear();
        else
            doc.timeSigChanges.set(changes);
    });
}

void HeaderPanel::refresh()
{
    // A score or lyric edit also emits documentChanged. Do not let that
    // unrelated refresh replace title/commentary text that is still being
    // edited and has not reached the session yet.
    if (m_pending && m_session->isOpen())
        return;
    m_loading = true;
    const bool open = m_session->isOpen();
    setEnabled(open);
    if (!open) {
        m_loading = false;
        return;
    }
    const SongDocument &doc = m_session->document();
    const SongDocument &effective = m_session->effectiveDocument();

    m_title->setText(doc.title.valueOr(effective.title.valueOr(QString())));
    m_subtitle->setText(doc.subtitle.valueOr(effective.subtitle.valueOr(QString())));
    m_key->setCurrentText(effective.keySignature.valueOr(QStringLiteral("C")));
    m_timeNumerator->setValue(effective.timeSigNumerator.valueOr(4));
    selectTimeSignatureDenominator(m_timeDenominator, effective.timeSigDenominator.valueOr(4));
    m_tempo->setValue(effective.tempoBpm.valueOr(100));
    m_verseCount->setValue(effective.verseCount.valueOr(0));
    m_active->setChecked(effective.active.valueOr(true));
    m_converge->setCurrentIndex(!effective.convergeVerses.present() ? 0
            : *effective.convergeVerses                             ? 1
                                                                    : 2);
    m_copyrights->setPlainText(effective.copyrights.valueOr({}).join(u'\n'));
    m_commentary->setPlainText(effective.commentary.valueOr(QString()));

    const int changes = static_cast<int>(effective.timeSigChanges.valueOr({}).size());
    m_timeSigChanges->setText(changes == 0 ? tr("none — edit…")
                                           : tr("%n change(s) — edit…", nullptr, changes));

    if (doc.isOverlay) {
        QStringList inherited;
        const auto note = [&inherited](const QString &name, bool present) {
            if (!present)
                inherited.append(name);
        };
        note(tr("key"), doc.keySignature.present());
        note(tr("metre"), doc.timeSigNumerator.present());
        note(tr("tempo"), doc.tempoBpm.present());
        note(tr("verse count"), doc.verseCount.present());
        note(tr("phrase breaks"), doc.phraseBreaks.present());
        m_inherited->setText(inherited.isEmpty()
                ? tr("This translation overrides every header field.")
                : tr("Inherited from song.toml: %1. Editing one here creates an override.")
                      .arg(inherited.join(QStringLiteral(", "))));
        m_inherited->show();
    } else {
        m_inherited->hide();
    }
    m_loading = false;
}

void HeaderPanel::commit()
{
    if (m_loading || !m_session->isOpen())
        return;

    const bool hadPending = m_pending;
    m_pending = false;
    if (hadPending)
        Q_EMIT pendingEditsChanged(false);

    const QStringList copyrights = m_copyrights->toPlainText().split(u'\n', Qt::SkipEmptyParts);
    const QString commentary = m_commentary->toPlainText();
    const QString title = m_title->text();
    const QString subtitle = m_subtitle->text();
    const QString key = m_key->currentText();
    const int numerator = m_timeNumerator->value();
    const int denominator = m_timeDenominator->currentData().toInt();
    const int tempo = m_tempo->value();
    const int verses = m_verseCount->value();
    const bool active = m_active->isChecked();
    const QVariant converge = m_converge->currentData();

    // Compare against the *effective* value, but write only the fields that
    // actually moved. On a translation every untouched field must stay absent:
    // materialising the inherited tempo and metre into song_es.toml the moment
    // someone fixes a typo in the title is exactly the silent divergence the
    // overlay format exists to prevent.
    const SongDocument &effective = m_session->effectiveDocument();
    QStringList changed;
    const auto differs = [&changed](bool condition, const char *name) {
        if (condition)
            changed.append(QString::fromLatin1(name));
        return condition;
    };
    const bool titleChanged = differs(effective.title.valueOr(QString()) != title, "title");
    const bool subtitleChanged
        = differs(effective.subtitle.valueOr(QString()) != subtitle, "subtitle");
    const bool keyChanged = differs(effective.keySignature.valueOr(QString()) != key, "key");
    const bool metreChanged = differs(effective.timeSigNumerator.valueOr(4) != numerator
            || effective.timeSigDenominator.valueOr(4) != denominator,
        "metre");
    const bool tempoChanged = differs(effective.tempoBpm.valueOr(100) != tempo, "tempo");
    const bool versesChanged = differs(effective.verseCount.valueOr(0) != verses, "verses");
    const bool activeChanged = differs(effective.active.valueOr(true) != active, "active");
    const bool copyrightsChanged
        = differs(effective.copyrights.valueOr({}) != copyrights, "copyrights");
    const bool commentaryChanged
        = differs(effective.commentary.valueOr(QString()) != commentary, "commentary");
    const bool convergeChanged
        = differs(effective.convergeVerses.opt() != (converge.isValid()
                          ? std::optional<bool>(converge.toBool())
                          : std::nullopt),
            "converge_verses");
    if (changed.isEmpty())
        return;

    m_session->mutate(tr("Edit song details"), [&](SongDocument &doc) {
        if (titleChanged)
            doc.title.set(title);
        if (subtitleChanged) {
            if (subtitle.isEmpty())
                doc.subtitle.clear();
            else
                doc.subtitle.set(subtitle);
        }
        if (keyChanged)
            doc.keySignature.set(key);
        if (metreChanged) {
            doc.timeSigNumerator.set(numerator);
            doc.timeSigDenominator.set(denominator);
        }
        if (tempoChanged)
            doc.tempoBpm.set(tempo);
        if (versesChanged && verses > 0)
            doc.verseCount.set(verses);
        if (activeChanged) {
            // `active` is only written when it is false: absent means true, and
            // a gratuitous `active = true` would show up in every diff.
            if (active)
                doc.active.clear();
            else
                doc.active.set(false);
        }
        if (copyrightsChanged) {
            if (copyrights.isEmpty())
                doc.copyrights.clear();
            else
                doc.copyrights.set(copyrights);
        }
        if (commentaryChanged) {
            if (commentary.trimmed().isEmpty())
                doc.commentary.clear();
            else
                doc.commentary.set(commentary);
        }
        if (convergeChanged) {
            if (converge.isValid())
                doc.convergeVerses.set(converge.toBool());
            else
                doc.convergeVerses.clear();
        }
    });
}

// ----------------------------------------------------------- ProblemsPanel ---

ProblemsPanel::ProblemsPanel(Session *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *controls = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_showWarnings = new QCheckBox(tr("Warnings"), this);
    m_showWarnings->setChecked(true);
    m_showInfo = new QCheckBox(tr("Info"), this);
    m_showInfo->setChecked(false);
    controls->addWidget(m_summary, 1);
    controls->addWidget(m_showWarnings);
    controls->addWidget(m_showInfo);
    layout->addLayout(controls);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Severity"), tr("Rule"), tr("Where"), tr("Problem") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_tree);

    connect(m_showWarnings, &QCheckBox::toggled, this, &ProblemsPanel::refresh);
    connect(m_showInfo, &QCheckBox::toggled, this, &ProblemsPanel::refresh);
    connect(session, &Session::documentChanged, this, &ProblemsPanel::refresh);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        const int index = item->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < m_session->findings().size())
            Q_EMIT navigate(m_session->findings().at(index));
    });
    refresh();
}

void ProblemsPanel::refresh()
{
    m_tree->clear();
    const QList<Finding> &findings = m_session->findings();

    QList<int> order;
    for (int i = 0; i < findings.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return static_cast<int>(findings.at(a).severity) < static_cast<int>(findings.at(b).severity);
    });

    for (const int index : order) {
        const Finding &finding = findings.at(index);
        if (finding.severity == Severity::Warning && !m_showWarnings->isChecked())
            continue;
        if (finding.severity == Severity::Info && !m_showInfo->isChecked())
            continue;
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, severityLabel(finding.severity));
        item->setForeground(0, severityColor(finding.severity));
        item->setText(1, finding.rule);
        item->setText(2, finding.location());
        item->setText(3, finding.fixHint.isEmpty()
                ? finding.message
                : QStringLiteral("%1  —  %2").arg(finding.message, finding.fixHint));
        item->setData(0, Qt::UserRole, index);
    }
    for (int column = 0; column < 3; ++column)
        m_tree->resizeColumnToContents(column);
    m_summary->setText(summary());
}

QString ProblemsPanel::summary() const
{
    const QList<Finding> &findings = m_session->findings();
    const int errors = countBySeverity(findings, Severity::Error);
    const int warnings = countBySeverity(findings, Severity::Warning);
    if (errors == 0 && warnings == 0)
        return tr("No problems found.");
    return tr("%n error(s), ", nullptr, errors) + tr("%n warning(s)", nullptr, warnings);
}

// ---------------------------------------------------------- InspectorPanel ---

InspectorPanel::InspectorPanel(Session *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);

    auto *noteBox = new QGroupBox(tr("Selected note"), this);
    auto *noteLayout = new QVBoxLayout(noteBox);
    m_noteInfo = new QLabel(tr("Nothing selected"), noteBox);
    m_noteInfo->setWordWrap(true);
    m_noteInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_noteInfo->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    noteLayout->addWidget(m_noteInfo);
    layout->addWidget(noteBox);

    auto *partBox = new QGroupBox(tr("Part"), this);
    auto *partLayout = new QFormLayout(partBox);
    m_choralType = new QComboBox(partBox);
    m_choralType->addItems(validChoralTypes());
    m_choralType->setEditable(true);
    m_clef = new QComboBox(partBox);
    m_clef->addItems(validClefs());
    m_staffNumber = new QSpinBox(partBox);
    m_staffNumber->setRange(1, 12);
    m_splice = new QComboBox(partBox);
    m_splice->addItem(tr("(none)"), QString());
    for (const QString &type : validChoralTypes())
        m_splice->addItem(type, type);
    m_suppressVerses = new QLineEdit(partBox);
    m_suppressVerses->setPlaceholderText(QStringLiteral("2, 3, 4"));
    m_suppressWhen = new QLineEdit(partBox);
    m_suppressWhen->setPlaceholderText(QStringLiteral("soprano, alto"));
    m_spliceReport = new QLabel(partBox);
    m_spliceReport->setWordWrap(true);
    m_spliceReport->setStyleSheet(QStringLiteral("color: #57606a;"));

    partLayout->addRow(tr("Choral type"), m_choralType);
    partLayout->addRow(tr("Clef"), m_clef);
    partLayout->addRow(tr("Staff"), m_staffNumber);
    partLayout->addRow(tr("Splice lyrics into"), m_splice);
    partLayout->addRow(tr("Suppress verses"), m_suppressVerses);
    partLayout->addRow(tr("…when present"), m_suppressWhen);
    partLayout->addRow(QString(), m_spliceReport);
    layout->addWidget(partBox);
    layout->addStretch();

    const auto onEdit = [this] { commitPart(); };
    connect(m_choralType, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_clef, &QComboBox::currentTextChanged, this, onEdit);
    connect(m_staffNumber, &QSpinBox::editingFinished, this, onEdit);
    connect(m_splice, &QComboBox::currentIndexChanged, this, onEdit);
    connect(m_suppressVerses, &QLineEdit::editingFinished, this, onEdit);
    connect(m_suppressWhen, &QLineEdit::editingFinished, this, onEdit);

    connect(session, &Session::selectionChanged, this, &InspectorPanel::refresh);
    connect(session, &Session::documentChanged, this, &InspectorPanel::refresh);
    refresh();
}

void InspectorPanel::refresh()
{
    m_loading = true;
    const Selection selection = m_session->selection();
    const SongDocument &doc = m_session->effectiveDocument();

    if (!selection.isValid() || selection.partIndex >= doc.parts.size()) {
        m_noteInfo->setText(tr("Nothing selected"));
        m_partName.clear();
        m_loading = false;
        return;
    }

    const Part &part = doc.parts.at(selection.partIndex);
    m_partName = part.name;

    if (const Event *event = m_session->selectedEvent()) {
        QStringList lines;
        lines.append(tr("token      %1").arg(event->text()));
        QStringList pitches;
        for (const Pitch &pitch : event->pitches)
            pitches.append(QStringLiteral("%1%2 (MIDI %3)")
                               .arg(pitch.step)
                               .arg(pitch.octave)
                               .arg(pitch.midiNote()));
        lines.append(tr("pitch      %1")
                         .arg(pitches.isEmpty() ? tr("(rest or spacer)")
                                                : pitches.join(QStringLiteral(", "))));
        lines.append(tr("duration   %1%2 = %3 ticks")
                         .arg(Duration::typeName(event->duration.base))
                         .arg(QString(event->duration.dots, u'.'))
                         .arg(event->playedTicks()));
        lines.append(tr("measure    %1, tick %2")
                         .arg(selection.measureIndex + 1)
                         .arg(ticks::toPhraseTicks(event->tickInMeasure)));
        lines.append(event->slotIndex >= 0 ? tr("lyric slot %1").arg(event->slotIndex)
                                           : tr("lyric slot (none — melisma or rest)"));
        m_noteInfo->setText(lines.join(u'\n'));
    } else {
        m_noteInfo->setText(tr("Part selected; no note under the cursor"));
    }

    m_choralType->setCurrentText(part.choralType.valueOr(QString()));
    m_clef->setCurrentText(part.clef.valueOr(QStringLiteral("treble")));
    m_staffNumber->setValue(part.staffNumber.valueOr(1));
    const int spliceIndex = m_splice->findData(part.spliceLyricsInto.valueOr(QString()));
    m_splice->setCurrentIndex(std::max(0, spliceIndex));
    QStringList verses;
    for (const int verse : part.suppressVerses.valueOr({}))
        verses.append(QString::number(verse));
    m_suppressVerses->setText(verses.join(QStringLiteral(", ")));
    m_suppressWhen->setText(part.suppressVersesWhen.valueOr({}).join(QStringLiteral(", ")));

    const SpliceReport report = analyseSplice(doc, part);
    if (!report.configured) {
        m_spliceReport->clear();
    } else {
        QStringList lines;
        for (const SpliceReport::VerseResult &verse : report.verses) {
            lines.append(verse.splices
                    ? tr("verse %1 → splices into %2").arg(verse.key, report.targetPartName)
                    : tr("verse %1 → own row (%2)").arg(verse.key, verse.reason));
        }
        m_spliceReport->setText(lines.join(u'\n'));
    }
    m_loading = false;
}

void InspectorPanel::commitPart()
{
    if (m_loading || m_partName.isEmpty() || !m_session->isOpen())
        return;

    const QString choralType = m_choralType->currentText();
    const QString clef = m_clef->currentText();
    const int staff = m_staffNumber->value();
    const QString splice = m_splice->currentData().toString();
    QList<int> suppress;
    for (const QString &piece : m_suppressVerses->text().split(u',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = piece.trimmed().toInt(&ok);
        if (ok)
            suppress.append(value);
    }
    QStringList when;
    for (const QString &piece : m_suppressWhen->text().split(u',', Qt::SkipEmptyParts))
        when.append(piece.trimmed().toLower());

    const QString name = m_partName;
    const Part *effective = m_session->effectiveDocument().part(name);
    if (!effective)
        return;

    // Only what moved, for the same reason as the song header: on a translation
    // every field written here is a field that stops tracking song.toml.
    const bool choralChanged = effective->choralType.valueOr(QString()) != choralType;
    const bool clefChanged = effective->clef.valueOr(QStringLiteral("treble")) != clef;
    const bool staffChanged = effective->staffNumber.valueOr(1) != staff;
    const bool spliceChanged = effective->spliceLyricsInto.valueOr(QString()) != splice;
    const bool suppressChanged = effective->suppressVerses.valueOr({}) != suppress;
    const bool whenChanged = effective->suppressVersesWhen.valueOr({}) != when;
    if (!choralChanged && !clefChanged && !staffChanged && !spliceChanged && !suppressChanged
        && !whenChanged)
        return;

    m_session->mutate(tr("Edit part"), [&](SongDocument &doc) {
        Part *part = doc.part(name);
        if (!part)
            return;
        if (choralChanged)
            part->choralType.set(choralType);
        if (clefChanged)
            part->clef.set(clef);
        if (staffChanged)
            part->staffNumber.set(staff);
        if (spliceChanged) {
            if (splice.isEmpty())
                part->spliceLyricsInto.clear();
            else
                part->spliceLyricsInto.set(splice);
        }
        if (suppressChanged) {
            if (suppress.isEmpty())
                part->suppressVerses.clear();
            else
                part->suppressVerses.set(suppress);
        }
        if (whenChanged) {
            if (when.isEmpty())
                part->suppressVersesWhen.clear();
            else
                part->suppressVersesWhen.set(when);
        }
    });
}

// ------------------------------------------------------------- SourcePanel ---

SourcePanel::SourcePanel(Session *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_openExternal = new QPushButton(tr("Open file in text editor…"), this);
    m_openExternal->setToolTip(
        tr("Use the system editor for advanced or newly introduced TOML fields. OPE will "
           "detect the external change before it overwrites anything."));
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *top = new QHBoxLayout;
    top->addWidget(m_status, 1);
    top->addWidget(m_openExternal);
    layout->addLayout(top);
    layout->addWidget(m_text);

    connect(m_openExternal, &QPushButton::clicked, this, [this] {
        if (m_session->isOpen())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_session->currentPath()));
    });

    connect(session, &Session::documentChanged, this, &SourcePanel::refresh);
    connect(session, &Session::languageChanged, this, &SourcePanel::refresh);
    refresh();
}

void SourcePanel::refresh()
{
    if (!m_session->isOpen()) {
        m_text->clear();
        m_status->clear();
        m_openExternal->setEnabled(false);
        return;
    }
    const SongDocument &doc = m_session->document();
    const QByteArray bytes
        = m_session->isNewFile() ? io::serializeFresh(doc) : io::serialize(doc);
    m_text->setPlainText(QString::fromUtf8(bytes));
    m_openExternal->setEnabled(!m_session->isNewFile() && !m_session->isDirty(
        m_session->currentLanguage()));
    if (!m_openExternal->isEnabled())
        m_openExternal->setToolTip(tr("Save this file first, then open it externally."));
    else
        m_openExternal->setToolTip(
            tr("Use the system editor for advanced or newly introduced TOML fields. OPE "
               "will detect external changes before it overwrites anything."));

    if (bytes == doc.originalBytes) {
        m_status->setText(tr("Identical to the file on disk."));
        m_status->setStyleSheet(QStringLiteral("color: #1a7f37;"));
        return;
    }

    // A line-level diff summary: enough to see the change is the size it should be.
    const QStringList before = QString::fromUtf8(doc.originalBytes).split(u'\n');
    const QStringList after = QString::fromUtf8(bytes).split(u'\n');
    int changed = 0;
    for (qsizetype i = 0; i < std::max(before.size(), after.size()); ++i) {
        const QString a = i < before.size() ? before.at(i) : QString();
        const QString b = i < after.size() ? after.at(i) : QString();
        if (a != b)
            ++changed;
    }
    m_status->setText(tr("%n line(s) differ from the file on disk.", nullptr, changed));
    m_status->setStyleSheet(QStringLiteral("color: #9a6700;"));
}

// ------------------------------------------------------------ TransportBar ---

TransportBar::TransportBar(Session *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);

    m_play = new QPushButton(tr("▶ Play"), this);
    m_stop = new QPushButton(tr("■ Stop"), this);
    m_verse = new QComboBox(this);
    m_partBox = new QWidget(this);
    auto *partLayout = new QHBoxLayout(m_partBox);
    partLayout->setContentsMargins(0, 0, 0, 0);
    m_tempo = new QSlider(Qt::Horizontal, this);
    m_tempo->setRange(40, 200);
    m_tempo->setValue(100);
    m_tempo->setFixedWidth(110);
    m_tempoLabel = new QLabel(QStringLiteral("100%"), this);
    m_positionLabel = new QLabel(QStringLiteral("0:00 / 0:00"), this);

    m_play->setToolTip(tr("Play the song through the built-in synth (Space)"));
    m_stop->setToolTip(tr("Stop and rewind to the start"));
    m_verse->setToolTip(tr("The verse whose words the score highlights"));

    layout->addWidget(m_play);
    layout->addWidget(m_stop);
    layout->addSpacing(12);
    layout->addWidget(new QLabel(tr("Verse"), this));
    layout->addWidget(m_verse);
    layout->addSpacing(12);
    layout->addWidget(m_partBox);
    layout->addStretch();
    layout->addWidget(new QLabel(tr("Tempo"), this));
    layout->addWidget(m_tempo);
    layout->addWidget(m_tempoLabel);
    layout->addSpacing(12);
    layout->addWidget(m_positionLabel);

    connect(m_play, &QPushButton::clicked, this, [this] {
        if (m_playing)
            Q_EMIT pauseRequested();
        else
            Q_EMIT playRequested();
    });
    connect(m_stop, &QPushButton::clicked, this, &TransportBar::stopRequested);
    connect(m_verse, &QComboBox::currentIndexChanged, this, [this] {
        Q_EMIT verseChanged(verse());
        Q_EMIT optionsChanged();
    });
    connect(m_tempo, &QSlider::valueChanged, this, [this](int value) {
        m_tempoLabel->setText(QStringLiteral("%1%").arg(value));
        Q_EMIT optionsChanged();
    });
    connect(session, &Session::documentChanged, this, &TransportBar::refresh);
    refresh();
}

void TransportBar::refresh()
{
    const SongDocument &doc = m_session->effectiveDocument();

    const int current = m_verse->currentIndex();
    m_verse->blockSignals(true);
    m_verse->clear();
    const int verses = std::max(1, doc.verseCount.valueOr(
        static_cast<int>(doc.verseKeys().size())));
    for (int i = 1; i <= verses; ++i)
        m_verse->addItem(QString::number(i), i);
    m_verse->setCurrentIndex(std::clamp(current, 0, m_verse->count() - 1));
    m_verse->blockSignals(false);

    qDeleteAll(m_partChecks);
    m_partChecks.clear();
    auto *layout = qobject_cast<QHBoxLayout *>(m_partBox->layout());
    for (const Part *part : doc.partsInDisplayOrder()) {
        auto *check = new QCheckBox(part->name.left(3), m_partBox);
        check->setToolTip(tr("Mute %1").arg(part->name));
        check->setChecked(!m_mutedParts.contains(part->name));
        check->setProperty("partName", part->name);
        connect(check, &QCheckBox::toggled, this, [this, check](bool audible) {
            const QString partName = check->property("partName").toString();
            if (audible)
                m_mutedParts.remove(partName);
            else
                m_mutedParts.insert(partName);
            Q_EMIT optionsChanged();
        });
        layout->addWidget(check);
        m_partChecks.append(check);
    }
}

void TransportBar::setPlaying(bool playing)
{
    m_playing = playing;
    m_play->setText(playing ? tr("❚❚ Pause") : tr("▶ Play"));
}

void TransportBar::setPosition(double seconds, double duration)
{
    m_position = seconds;
    m_duration = duration;
    updatePositionLabel();
}

void TransportBar::setDuration(double seconds)
{
    m_duration = seconds;
    if (!m_playing)
        m_position = 0.0;
    updatePositionLabel();
}

void TransportBar::updatePositionLabel()
{
    m_positionLabel->setText(
        QStringLiteral("%1 / %2").arg(formatSeconds(m_position), formatSeconds(m_duration)));
}

void TransportBar::setUnavailable(const QString &reason)
{
    m_play->setEnabled(false);
    m_stop->setEnabled(false);
    m_positionLabel->setText(tr("audio unavailable — %1").arg(reason));
    m_positionLabel->setToolTip(reason);
}

int TransportBar::verse() const { return std::max(1, m_verse->currentData().toInt()); }

PlaybackOptions TransportBar::options() const
{
    PlaybackOptions options;
    options.verse = verse();
    options.tempoScale = m_tempo->value() / 100.0;
    for (const QCheckBox *check : m_partChecks) {
        if (!check->isChecked())
            options.mutedParts.append(check->property("partName").toString());
    }
    return options;
}

} // namespace ope::ui
