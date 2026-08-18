// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "LyricsPanel.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace ope::ui {
namespace {

/// U+203F. Two words sung on one note are joined with it; a lyric slot cannot
/// contain a space.
const QString Undertie = QString(QChar(0x203F));

/// The two things a text box can be. Everything else on a card follows from it:
/// the accent stripe, the badge wording, and whether Revert is offered.
QString colorDefault() { return QStringLiteral("#1f6feb"); }
QString colorOverride() { return QStringLiteral("#bb8009"); }
QString colorOk() { return QStringLiteral("#1a7f37"); }
QString colorBad() { return QStringLiteral("#d1242f"); }

QColor colorMelisma() { return QColor(0x80, 0x80, 0x80, 40); }
QColor colorMismatch() { return QColor(0xd1, 0x24, 0x2f, 60); }
QColor colorPlaceholder() { return QColor(0xbb, 0x80, 0x09, 60); }

/// The phrase-break lanes, in the order the score's ruler stacks them. Same
/// colours, so a break is the same colour wherever the user meets it.
struct LaneStyle {
    BreakKind kind;
    const char *field;
    QString marker;
    QColor color;
};

const QList<LaneStyle> &laneStyles()
{
    static const QList<LaneStyle> styles {
        { BreakKind::Required, "phrase_breaks", QStringLiteral("│"),
            QColor(0x1f, 0x6f, 0xeb) },
        { BreakKind::Optional, "optional_phrase_breaks", QStringLiteral("┆"),
            QColor(0x99, 0x77, 0x11) },
        { BreakKind::NonBreaking, "non_breaking_phrase_breaks", QStringLiteral("╎"),
            QColor(0x88, 0x88, 0x88) },
    };
    return styles;
}

const LaneStyle &laneStyle(BreakKind kind)
{
    for (const LaneStyle &style : laneStyles()) {
        if (style.kind == kind)
            return style;
    }
    return laneStyles().first();
}

/// Lexicographic order on (measure, tick) — a break's place in the song.
bool breakBefore(const PhraseBreak &a, const PhraseBreak &b)
{
    return a.measure != b.measure ? a.measure < b.measure : a.tick < b.tick;
}

QString sectionTitle(const QString &key)
{
    if (SongDocument::isChorusKey(key))
        return QObject::tr("Chorus");
    if (SongDocument::isCodaKey(key))
        return QObject::tr("Coda");
    if (SongDocument::isSharedKey(key))
        return QObject::tr("Shared section %1").arg(key);
    return QObject::tr("Verse %1").arg(key);
}

QString rowLabel(const AttachedSection &section)
{
    if (section.isChorus)
        return QObject::tr("chorus");
    if (section.isCoda)
        return QObject::tr("coda");
    return QObject::tr("verse %1").arg(section.verseNumber);
}

QString commitKey(const QString &language, const QString &partName, const QString &key)
{
    return language + QChar(0x1f) + partName + QChar(0x1f) + key;
}

/// The text a `@sN` reference stands for, as this voice would see it: its own
/// `[parts.X.lyrics.sN]` if it has one, otherwise the song's.
QString sharedText(const SongDocument &doc, const Part *part, const QString &key)
{
    if (part && part->lyrics.contains(key))
        return part->lyrics.value(key).rawText;
    return doc.lyrics.value(key).rawText;
}

/// Syllables after `@sN` expansion — what the seeder will actually count. A
/// verse ending in `@s1` is eight syllables longer than it looks.
QStringList expandedSyllables(const SongDocument &doc, const Part *part, const QString &text)
{
    QStringList out;
    for (const QString &token : LyricSection { text, {}, {}, false }.syllables()) {
        if (token.startsWith(u'@') && SongDocument::isSharedKey(QStringView(token).sliced(1))) {
            // Shared sections cannot nest, so one pass is the whole expansion.
            out.append(LyricSection { sharedText(doc, part, token.sliced(1)), {}, {}, false }
                           .syllables());
            continue;
        }
        out.append(token);
    }
    return out;
}

/// How many syllables `key` needs from a voice, or -1 when that voice does not
/// sing it (a shared template, or a section it has no slots for).
int requiredSyllables(const PartAlignment &alignment, const QString &key)
{
    for (const AttachedSection &section : alignment.sections) {
        if (section.key != key)
            continue;
        if (section.isChorus || section.isCoda)
            return std::max(0, static_cast<int>(alignment.lyricSlots.size()) - section.slotOffset);
        return alignment.maxVerseLength;
    }
    return -1;
}

} // namespace

LyricsPanel::LyricsPanel(Session *session, QWidget *parent) : QWidget(parent), m_session(session)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *top = new QHBoxLayout;
    m_legend = new QLabel(this);
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setText(tr("<span style='color:%1'>▍</span> sung by every voice &nbsp; "
                         "<span style='color:%2'>▍</span> one voice only")
                          .arg(colorDefault(), colorOverride()));
    top->addWidget(m_legend);
    top->addStretch();

    m_addSection = new QToolButton(this);
    m_addSection->setText(tr("Add section"));
    m_addSection->setPopupMode(QToolButton::InstantPopup);
    m_addSection->setToolTip(tr("Add a chorus, coda, or shared section to this song"));
    top->addWidget(m_addSection);

    m_undertie = new QToolButton(this);
    m_undertie->setText(tr("Insert %1").arg(Undertie));
    m_undertie->setToolTip(tr("Insert an undertie (U+203F): two words sung on one note"));
    top->addWidget(m_undertie);
    layout->addLayout(top);

    m_tabs = new QTabWidget(this);

    auto *textPage = new QWidget(m_tabs);
    auto *textLayout = new QVBoxLayout(textPage);
    textLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(textPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_sectionHost = new QWidget(scroll);
    auto *hostLayout = new QVBoxLayout(m_sectionHost);
    hostLayout->setContentsMargins(2, 2, 2, 2);
    hostLayout->setSpacing(10);
    scroll->setWidget(m_sectionHost);
    textLayout->addWidget(scroll);
    m_tabs->addTab(textPage, tr("Text"));

    auto *gridPage = new QWidget(m_tabs);
    auto *gridLayout = new QVBoxLayout(gridPage);
    auto *gridTop = new QHBoxLayout;
    gridTop->addWidget(new QLabel(tr("Voice"), gridPage));
    m_gridPart = new QComboBox(gridPage);
    gridTop->addWidget(m_gridPart);
    gridTop->addStretch();
    gridLayout->addLayout(gridTop);
    m_gridHint = new QLabel(gridPage);
    m_gridHint->setWordWrap(true);
    m_gridHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_grid = new QTableWidget(gridPage);
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_grid->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    gridLayout->addWidget(m_gridHint);
    gridLayout->addWidget(m_grid);
    m_tabs->addTab(gridPage, tr("Alignment"));

    layout->addWidget(m_tabs);

    connect(m_gridPart, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_loading)
            rebuildGrid();
    });
    connect(m_undertie, &QToolButton::clicked, this, &LyricsPanel::insertUndertie);
    connect(m_grid, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (!m_loading)
            commitCell(row, column);
    });
    // The break row is the editing surface for phrase breaks: a click adds or
    // removes a required break after that syllable, the menu picks the lane.
    connect(m_grid, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (row == 0 && !m_loading)
            clickBreakCell(column);
    });
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_grid, &QWidget::customContextMenuRequested, this, [this](QPoint where) {
        const QModelIndex index = m_grid->indexAt(where);
        if (index.isValid() && index.row() == 0)
            showBreakMenu(index.column(), m_grid->viewport()->mapToGlobal(where));
    });

    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(600);
    connect(&m_commitTimer, &QTimer::timeout, this, &LyricsPanel::commitPendingEdits);

    connect(session, &Session::documentChanged, this, &LyricsPanel::refresh);
    connect(session, &Session::languageChanged, this, &LyricsPanel::refresh);
    refresh();
}

void LyricsPanel::refresh()
{
    // setPhraseBreak() emits documentChanged synchronously. Phrase breaks do
    // not affect this grid's shape or lyrics, so keep the existing table and
    // its viewport intact instead of rebuilding it and attempting to restore
    // a scrollbar whose range may still be awaiting a queued layout.
    if (m_phraseBreakEditInProgress) {
        refreshBreakRow();
        return;
    }

    m_loading = true;

    const SongDocument &doc = m_session->effectiveDocument();
    const QString previous = gridPartName();
    m_gridPart->clear();
    for (const Part *part : doc.partsInDisplayOrder())
        m_gridPart->addItem(part->name, part->name);
    const int index = m_gridPart->findData(previous);
    m_gridPart->setCurrentIndex(index >= 0 ? index : 0);

    // Sections the song does not declare yet. Rebuilt on every document change,
    // so the one it replaces has to go with it.
    delete m_addSection->menu();
    auto *menu = new QMenu(m_addSection);
    const auto offer = [this, menu, &doc](const QString &key, const QString &label) {
        if (doc.lyrics.contains(key))
            return;
        menu->addAction(label, this, [this, key] { addSection(key); });
    };
    offer(QStringLiteral("chorus"), tr("Chorus"));
    offer(QStringLiteral("coda"), tr("Coda"));
    int nextShared = 1;
    while (doc.lyrics.contains(QStringLiteral("s%1").arg(nextShared)))
        ++nextShared;
    const QString sharedKey = QStringLiteral("s%1").arg(nextShared);
    menu->addAction(tr("Shared section %1").arg(sharedKey), this,
        [this, sharedKey] { addSection(sharedKey); });
    m_addSection->setMenu(menu);
    m_addSection->setEnabled(m_session->isOpen());

    const QStringList signature = structureSignature();
    if (signature == m_signature && !m_editors.isEmpty()) {
        syncEditors();
    } else {
        rebuildSections();
        m_signature = signature;
    }
    rebuildGrid();
    m_loading = false;
}

QStringList LyricsPanel::structureSignature() const
{
    QStringList signature;
    if (!m_session->isOpen())
        return signature;
    const SongDocument &doc = m_session->effectiveDocument();
    signature.append(QStringLiteral("v%1").arg(doc.verseCount.valueOr(0)));
    for (const Part *part : doc.partsInDisplayOrder())
        signature.append(QStringLiteral("p%1").arg(part->name));
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it)
        signature.append(QStringLiteral("g%1").arg(it.key()));
    for (const Part *part : doc.partsInDisplayOrder()) {
        for (auto it = part->lyrics.constBegin(); it != part->lyrics.constEnd(); ++it)
            signature.append(QStringLiteral("o%1.%2").arg(part->name, it.key()));
    }
    return signature;
}

void LyricsPanel::syncEditors()
{
    const SongDocument &doc = m_session->effectiveDocument();
    for (const EditorRef &row : m_editors) {
        // Never overwrite a box whose keystrokes have not reached the document
        // yet; the debounce timer is about to carry them there.
        if (m_pendingCommits.contains(
                commitKey(m_session->currentLanguage(), row.partName, row.key)))
            continue;
        const Part *part = row.partName.isEmpty() ? nullptr : doc.part(row.partName);
        const QString text = part ? part->lyrics.value(row.key).rawText
                                  : doc.lyrics.value(row.key).rawText;
        if (row.editor->toPlainText() != text) {
            const int position = row.editor->textCursor().position();
            row.editor->setPlainText(text);
            QTextCursor cursor = row.editor->textCursor();
            cursor.setPosition(std::min<int>(position, text.size()));
            row.editor->setTextCursor(cursor);
        }
        updateCounter(row);
    }
}

QStringList LyricsPanel::partsOverriding(const QString &key) const
{
    QStringList names;
    for (const Part *part : m_session->effectiveDocument().partsInDisplayOrder()) {
        if (part->lyrics.contains(key))
            names.append(part->name);
    }
    return names;
}

QStringList LyricsPanel::partsUsingDefault(const QString &key) const
{
    QStringList names;
    for (const Part *part : m_session->effectiveDocument().partsInDisplayOrder()) {
        if (!part->lyrics.contains(key))
            names.append(part->name);
    }
    return names;
}

void LyricsPanel::rebuildSections()
{
    auto *layout = qobject_cast<QVBoxLayout *>(m_sectionHost->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_editors.clear();
    if (!m_session->isOpen())
        return;

    const SongDocument &doc = m_session->effectiveDocument();

    // Verses come from verse_count as much as from the file: a song that
    // declares five verses and has written three should show two empty cards,
    // not hide the fact that they are missing.
    QStringList keys;
    const int declared = doc.verseCount.valueOr(0);
    int highest = declared;
    for (const QString &key : doc.verseKeys())
        highest = std::max(highest, key.toInt());
    for (int verse = 1; verse <= highest; ++verse)
        keys.append(QString::number(verse));
    QStringList others;
    for (auto it = doc.lyrics.constBegin(); it != doc.lyrics.constEnd(); ++it) {
        if (!SongDocument::isVerseKey(it.key()))
            others.append(it.key());
    }
    for (const Part &part : doc.parts) {
        for (auto it = part.lyrics.constBegin(); it != part.lyrics.constEnd(); ++it) {
            if (!SongDocument::isVerseKey(it.key()) && !others.contains(it.key()))
                others.append(it.key());
        }
    }
    keys.append(SongDocument::orderedLyricKeys(others));

    for (const QString &key : keys)
        buildSectionCard(layout, key);
    layout->addStretch();
}

void LyricsPanel::buildSectionCard(QVBoxLayout *host, const QString &key)
{
    const SongDocument &doc = m_session->effectiveDocument();

    auto *card = new QFrame(m_sectionHost);
    card->setObjectName(QStringLiteral("lyricCard"));
    card->setStyleSheet(QStringLiteral("QFrame#lyricCard { border: 1px solid palette(mid); "
                                       "border-radius: 4px; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(card);
    title->setTextFormat(Qt::RichText);
    title->setText(QStringLiteral("<b>%1</b> &nbsp;<span style='color:palette(mid)'>"
                                  "<code>lyrics.%2</code></span>")
                       .arg(sectionTitle(key), key));
    header->addWidget(title);
    header->addStretch();

    const QStringList available = partsUsingDefault(key);
    if (!available.isEmpty()) {
        auto *add = new QToolButton(card);
        add->setText(tr("Give one voice its own text"));
        add->setPopupMode(QToolButton::InstantPopup);
        add->setToolTip(tr("Write a [parts.NAME.lyrics.%1] override: that voice sings this "
                           "text instead of the song's").arg(key));
        auto *menu = new QMenu(add);
        for (const QString &partName : available) {
            menu->addAction(partName, this, [this, key, partName] { addOverride(key, partName); });
        }
        add->setMenu(menu);
        header->addWidget(add);
    }
    if (!SongDocument::isVerseKey(key) && doc.lyrics.contains(key)) {
        // Verses are governed by verse_count, so only the optional sections get
        // a delete button here.
        auto *remove = new QToolButton(card);
        remove->setText(tr("Delete section"));
        connect(remove, &QToolButton::clicked, this, [this, key] {
            m_session->mutate(tr("Delete lyrics.%1").arg(key),
                [&key](SongDocument &document) { document.removeGlobalLyric(key); });
            Q_EMIT statusMessage(tr("lyrics.%1 deleted").arg(key));
        });
        header->addWidget(remove);
    }
    cardLayout->addLayout(header);

    const QStringList overriding = partsOverriding(key);
    // The song-wide text always shows, even when every voice overrides it: it is
    // still what a newly added voice would inherit.
    cardLayout->addWidget(buildTextRow(key, QString(), doc.lyrics.value(key).rawText, available));
    for (const QString &partName : overriding) {
        const Part *part = doc.part(partName);
        cardLayout->addWidget(
            buildTextRow(key, partName, part->lyrics.value(key).rawText, { partName }));
    }

    host->addWidget(card);
}

QWidget *LyricsPanel::buildTextRow(const QString &key, const QString &partName,
    const QString &text, const QStringList &appliesTo)
{
    const bool isOverride = !partName.isEmpty();
    const QString accent = isOverride ? colorOverride() : colorDefault();

    auto *row = new QFrame(m_sectionHost);
    row->setObjectName(QStringLiteral("lyricRow"));
    row->setStyleSheet(
        QStringLiteral("QFrame#lyricRow { border-left: 3px solid %1; }").arg(accent));
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(8, 2, 0, 2);
    rowLayout->setSpacing(2);

    auto *badgeRow = new QHBoxLayout;
    auto *badge = new QLabel(row);
    badge->setTextFormat(Qt::RichText);
    if (isOverride) {
        badge->setText(QStringLiteral("<b style='color:%1'>%2 only</b> "
                                      "<span style='color:palette(mid)'>"
                                      "— parts.%2.lyrics.%3</span>")
                           .arg(accent, partName, key));
    } else if (appliesTo.isEmpty()) {
        badge->setText(QStringLiteral("<b style='color:%1'>%2</b> "
                                      "<span style='color:palette(mid)'>%3</span>")
                           .arg(accent, tr("Song default"),
                               tr("— every voice overrides this; kept as the fallback")));
    } else {
        badge->setText(QStringLiteral("<b style='color:%1'>%2</b> "
                                      "<span style='color:palette(mid)'>— %3</span>")
                           .arg(accent, tr("Song default"),
                               tr("sung by %1").arg(appliesTo.join(QStringLiteral(", ")))));
    }
    badgeRow->addWidget(badge);
    badgeRow->addStretch();
    if (isOverride) {
        auto *revert = new QToolButton(row);
        revert->setText(tr("Revert to song default"));
        revert->setToolTip(tr("Delete this override; %1 goes back to singing the song's "
                              "own text for this section").arg(partName));
        connect(revert, &QToolButton::clicked, this,
            [this, key, partName] { removeOverride(key, partName); });
        badgeRow->addWidget(revert);
    }
    rowLayout->addLayout(badgeRow);

    auto *editor = new QPlainTextEdit(row);
    editor->setPlainText(text);
    editor->setMaximumHeight(78);
    editor->setPlaceholderText(tr("Je -- sus loves me! This I know,"));
    editor->setTabChangesFocus(true);
    rowLayout->addWidget(editor);

    auto *counter = new QLabel(row);
    rowLayout->addWidget(counter);

    const EditorRef ref { key, partName, editor, counter };
    m_editors.append(ref);
    updateCounter(ref);

    connect(editor, &QPlainTextEdit::textChanged, this, [this, ref] {
        updateCounter(ref);
        if (m_loading)
            return;
        const bool wasEmpty = m_pendingCommits.isEmpty();
        const QString language = m_session->currentLanguage();
        m_pendingCommits.insert(commitKey(language, ref.partName, ref.key),
            PendingCommit { language, ref.key, ref.partName, ref.editor->toPlainText() });
        if (wasEmpty)
            Q_EMIT pendingEditsChanged(true);
        m_commitTimer.start();
    });
    return row;
}

void LyricsPanel::updateCounter(const EditorRef &row)
{
    const SongDocument &doc = m_session->effectiveDocument();
    const QString text = row.editor->toPlainText();

    if (SongDocument::isSharedKey(row.key)) {
        const int syllables
            = static_cast<int>(LyricSection { text, {}, {}, false }.syllables().size());
        row.counter->setText(tr("%n syllable(s) — spliced wherever @%1 appears", nullptr,
            syllables).arg(row.key));
        row.counter->setStyleSheet(QStringLiteral("color: palette(mid);"));
        return;
    }

    // A verse must fit every voice that sings it, and the voices need not agree:
    // an echo alto has fewer slots than the soprano, and each voice may resolve
    // an `@sN` reference to different text. Count it once per voice.
    QStringList verdicts;
    bool allFit = true;
    bool anyKnown = false;
    const QStringList names = row.partName.isEmpty() ? partsUsingDefault(row.key)
                                                     : QStringList { row.partName };
    for (const QString &partName : names) {
        const int required = requiredSyllables(m_session->alignment(partName), row.key);
        if (required < 0)
            continue;
        anyKnown = true;
        const int have
            = static_cast<int>(expandedSyllables(doc, doc.part(partName), text).size());
        const bool fits = have == required;
        allFit = allFit && fits;
        verdicts.append(fits ? tr("%1 %2/%3 ✓").arg(partName).arg(have).arg(required)
                             : tr("%1 %2/%3").arg(partName).arg(have).arg(required));
    }

    if (!anyKnown) {
        const int syllables
            = static_cast<int>(expandedSyllables(doc, nullptr, text).size());
        row.counter->setText(tr("%n syllable(s)", nullptr, syllables));
        row.counter->setStyleSheet(QStringLiteral("color: palette(mid);"));
        return;
    }
    row.counter->setText(tr("syllables / slots:") + u' '
        + verdicts.join(QStringLiteral(" · ")));
    row.counter->setStyleSheet(allFit
            ? QStringLiteral("color: %1;").arg(colorOk())
            : QStringLiteral("color: %1; font-weight: bold;").arg(colorBad()));
}

void LyricsPanel::commitPendingEdits()
{
    m_commitTimer.stop();
    const QHash<QString, PendingCommit> pending = std::exchange(m_pendingCommits, {});
    for (const PendingCommit &commit : pending)
        commitText(commit.language, commit.key, commit.partName, commit.text);
    if (!pending.isEmpty())
        Q_EMIT pendingEditsChanged(false);
}

void LyricsPanel::commitText(const QString &language, const QString &key,
    const QString &partName, const QString &text)
{
    const SongDocument *authored = m_session->document(language);
    if (!authored)
        return;
    const SongDocument doc = authored->isOverlay && m_session->baseDocument()
        ? mergeOverlay(*m_session->baseDocument(), *authored)
        : *authored;
    const Part *part = partName.isEmpty() ? nullptr : doc.part(partName);
    const QString existing = part ? part->lyrics.value(key).rawText : doc.lyrics.value(key).rawText;
    if (existing == text)
        return;

    m_session->mutate(language, tr("Edit lyrics"), [&](SongDocument &document) {
        materialiseOverlayLyrics(document, doc, partName);
        if (!partName.isEmpty()) {
            if (Part *target = document.part(partName))
                SongDocument::setLyric(target->lyrics, key, text);
            return;
        }
        SongDocument::setLyric(document.lyrics, key, text);
    });
    Q_EMIT statusMessage(partName.isEmpty()
            ? tr("lyrics.%1 updated").arg(key)
            : tr("parts.%1.lyrics.%2 updated").arg(partName, key));
}

void LyricsPanel::addOverride(const QString &key, const QString &partName)
{
    const SongDocument &doc = m_session->effectiveDocument();
    // Start from what the voice sings today, so an override is an edit of the
    // real text rather than a blank box that fails validation immediately.
    const QString seed = doc.lyrics.value(key).rawText;
    m_session->mutate(tr("Add %1 lyrics").arg(partName), [&](SongDocument &document) {
        materialiseOverlayLyrics(document, doc, partName);
        if (Part *target = document.part(partName))
            SongDocument::setLyric(target->lyrics, key, seed);
    });
    Q_EMIT statusMessage(tr("%1 now has its own text for lyrics.%2").arg(partName, key));
}

void LyricsPanel::removeOverride(const QString &key, const QString &partName)
{
    m_session->mutate(tr("Revert %1 lyrics").arg(partName), [&](SongDocument &document) {
        if (Part *target = document.part(partName))
            document.removePartLyric(*target, key);
    });
    Q_EMIT statusMessage(tr("%1 is back to the song's text for lyrics.%2").arg(partName, key));
}

void LyricsPanel::addSection(const QString &key)
{
    const SongDocument &doc = m_session->effectiveDocument();
    m_session->mutate(tr("Add lyrics.%1").arg(key), [&](SongDocument &document) {
        materialiseOverlayLyrics(document, doc, QString());
        SongDocument::setLyric(document.lyrics, key, QString());
    });
    Q_EMIT statusMessage(tr("lyrics.%1 added").arg(key));
}

// ------------------------------------------------------------- alignment ---

QString LyricsPanel::gridPartName() const { return m_gridPart->currentData().toString(); }

LyricsPanel::BreakCell LyricsPanel::breakCellFor(
    const PartAlignment &alignment, const Part &part, int column) const
{
    BreakCell cell;
    const Slot &slot = alignment.lyricSlots.at(column);
    const int measureIndex = slot.measureIndex;

    // The boundary after this syllable is where the *next* one starts — not
    // where this note ends, because the notes in between are its melisma and a
    // break may not split them. At the end of a measure it is the barline.
    int boundaryTick = 0;
    if (column + 1 < alignment.lyricSlots.size()
        && alignment.lyricSlots.at(column + 1).measureIndex == measureIndex) {
        boundaryTick = alignment.lyricSlots.at(column + 1).tickInMeasure;
    } else if (measureIndex < part.stream.measureCount()) {
        boundaryTick = part.stream.measures().at(measureIndex).playedTicks();
    }
    cell.boundary = PhraseBreak { measureIndex + 1, ticks::toPhraseTicks(boundaryTick) };
    // 64ths are the format's resolution; a boundary inside a tuplet may not
    // land on one, and rounding it would move the break somewhere else.
    cell.representable = ticks::fromPhraseTicks(cell.boundary.tick) == boundaryTick;

    const PhraseBreak slotStart { measureIndex + 1, ticks::toPhraseTicks(slot.tickInMeasure) };
    const SongDocument &doc = m_session->effectiveDocument();
    for (const PhraseBreak &brk : doc.allPhraseBreaks()) {
        if (brk == cell.boundary) {
            cell.existing = brk;
            cell.onBoundary = true;
            break;
        }
        // A break between this syllable's own notes belongs to this column too,
        // flagged: that is R9.4/R9.5 shown where the words are.
        if (!breakBefore(brk, slotStart) && breakBefore(brk, cell.boundary)
            && !(brk == slotStart)) {
            cell.existing = brk;
            cell.onBoundary = false;
        }
    }
    if (cell.existing)
        cell.kind = m_session->phraseBreakAt(*cell.existing);
    return cell;
}

void LyricsPanel::fillBreakRow(const PartAlignment &alignment, const Part &part)
{
    for (int column = 0; column < m_grid->columnCount(); ++column) {
        const BreakCell cell = breakCellFor(alignment, part, column);
        auto *item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled);
        // Hard right: a break comes *after* this syllable, and a mark centred
        // over the word reads as though it came before it.
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setData(Qt::UserRole, cell.boundary.measure);
        item->setData(Qt::UserRole + 1, cell.boundary.tick);
        item->setData(Qt::UserRole + 2, cell.representable);
        if (cell.existing) {
            item->setData(Qt::UserRole + 3, cell.existing->measure);
            item->setData(Qt::UserRole + 4, cell.existing->tick);
        }

        if (cell.existing && cell.kind) {
            const LaneStyle &style = laneStyle(*cell.kind);
            item->setText(style.marker);
            item->setForeground(style.color);
            QFont marker = m_grid->font();
            marker.setBold(true);
            marker.setPointSizeF(marker.pointSizeF() * 1.5);
            item->setFont(marker);
            if (cell.onBoundary) {
                item->setToolTip(tr("%1 \"%2\" — the line breaks after this syllable.\n"
                                    "Click to remove, right-click for the other lanes.")
                                     .arg(QString::fromLatin1(style.field),
                                         cell.existing->toString()));
            } else {
                // Drawn hatched: the break is real, but it does not land between
                // two of *this* voice's syllables.
                item->setBackground(QBrush(colorMismatch(), Qt::FDiagPattern));
                item->setToolTip(tr("%1 \"%2\" falls between %3's own notes, not between two "
                                    "of its syllables — the seeder will still take it, but the "
                                    "line will break inside a melisma here.")
                                     .arg(QString::fromLatin1(style.field),
                                         cell.existing->toString(), part.name));
            }
        } else if (!cell.representable) {
            item->setToolTip(tr("This boundary falls inside a tuplet and cannot be written as "
                                "a 64th — place the break on a neighbouring note."));
            item->setForeground(QColor(0xaa, 0xaa, 0xaa));
            item->setText(QStringLiteral("·"));
        } else {
            item->setToolTip(tr("Click to break the line after this syllable (%1).\n"
                                "Right-click for optional and non-breaking.")
                                 .arg(cell.boundary.toString()));
        }
        m_grid->setItem(0, column, item);
    }
}

void LyricsPanel::refreshBreakRow()
{
    const QString partName = gridPartName();
    const SongDocument &doc = m_session->effectiveDocument();
    const Part *part = doc.part(partName);
    const PartAlignment &alignment = m_session->alignment(partName);
    if (!part || m_grid->columnCount() != static_cast<int>(alignment.lyricSlots.size()))
        return;

    const bool wasLoading = m_loading;
    m_loading = true;
    fillBreakRow(alignment, *part);
    m_loading = wasLoading;
}

void LyricsPanel::clickBreakCell(int column)
{
    const QTableWidgetItem *item = m_grid->item(0, column);
    if (!item)
        return;
    if (item->data(Qt::UserRole + 3).isValid()) {
        // Something is already here — including a break that straddles this
        // syllable's notes. Clicking clears that one rather than adding a second.
        setPhraseBreakKeepingGridPosition(
            PhraseBreak { item->data(Qt::UserRole + 3).toInt(),
                item->data(Qt::UserRole + 4).toInt() },
            std::nullopt);
        return;
    }
    if (!item->data(Qt::UserRole + 2).toBool()) {
        Q_EMIT statusMessage(tr("That boundary is inside a tuplet and has no 64th to sit on."));
        return;
    }
    const PhraseBreak position {
        item->data(Qt::UserRole).toInt(), item->data(Qt::UserRole + 1).toInt()
    };
    const std::optional<BreakKind> current = m_session->phraseBreakAt(position);
    setPhraseBreakKeepingGridPosition(
        position, current == BreakKind::Required ? std::nullopt
                                                 : std::optional(BreakKind::Required));
}

void LyricsPanel::setPhraseBreakKeepingGridPosition(
    PhraseBreak position, std::optional<BreakKind> kind)
{
    m_phraseBreakEditInProgress = true;
    m_session->setPhraseBreak(position, kind);
    m_phraseBreakEditInProgress = false;
}

void LyricsPanel::showBreakMenu(int column, QPoint where)
{
    const QTableWidgetItem *item = m_grid->item(0, column);
    if (!item)
        return;
    const bool hasExisting = item->data(Qt::UserRole + 3).isValid();
    const PhraseBreak target = hasExisting
        ? PhraseBreak { item->data(Qt::UserRole + 3).toInt(),
              item->data(Qt::UserRole + 4).toInt() }
        : PhraseBreak { item->data(Qt::UserRole).toInt(), item->data(Qt::UserRole + 1).toInt() };
    if (!hasExisting && !item->data(Qt::UserRole + 2).toBool()) {
        Q_EMIT statusMessage(tr("That boundary is inside a tuplet and has no 64th to sit on."));
        return;
    }
    const std::optional<BreakKind> current = m_session->phraseBreakAt(target);

    QMenu menu(this);
    menu.addSection(tr("Phrase break at %1").arg(target.toString()));
    for (const LaneStyle &style : laneStyles()) {
        QAction *action = menu.addAction(QString::fromLatin1(style.field));
        action->setCheckable(true);
        action->setChecked(current && *current == style.kind);
        const BreakKind kind = style.kind;
        connect(action, &QAction::triggered, this,
            [this, target, kind] { setPhraseBreakKeepingGridPosition(target, kind); });
    }
    menu.addSeparator();
    QAction *none = menu.addAction(tr("no break here"));
    none->setCheckable(true);
    none->setChecked(!current);
    connect(none, &QAction::triggered, this,
        [this, target] { setPhraseBreakKeepingGridPosition(target, std::nullopt); });
    menu.exec(where);
}

void LyricsPanel::rebuildGrid()
{
    // A phrase-break edit changes the document and rebuilds this table. Keep
    // the user's place in a long hymn instead of snapping the alignment view
    // back to its first syllable after every click.
    const int horizontalScroll = m_grid->horizontalScrollBar()->value();
    const int verticalScroll = m_grid->verticalScrollBar()->value();
    const int currentRow = m_grid->currentRow();
    const int currentColumn = m_grid->currentColumn();

    const bool wasLoading = m_loading;
    m_loading = true;
    m_grid->clear();
    m_grid->setRowCount(0);
    m_grid->setColumnCount(0);

    if (m_session->isOpen()) {
        const QString partName = gridPartName();
        const PartAlignment &alignment = m_session->alignment(partName);
        const SongDocument &doc = m_session->effectiveDocument();
        const Part *part = doc.part(partName);
        if (part) {
            const int columns = static_cast<int>(alignment.lyricSlots.size());
            m_grid->setColumnCount(columns);
            // Row 0 phrase breaks, row 1 the note, then one row per section.
            m_grid->setRowCount(static_cast<int>(alignment.sections.size()) + 2);

            QStringList headers;
            for (int slot = 0; slot < columns; ++slot) {
                headers.append(tr("m%1\n#%2")
                                   .arg(alignment.lyricSlots.at(slot).measureIndex + 1)
                                   .arg(slot));
            }
            m_grid->setHorizontalHeaderLabels(headers);

            QStringList rowLabels { tr("break"), tr("note") };
            for (const AttachedSection &section : alignment.sections)
                rowLabels.append(rowLabel(section));
            m_grid->setVerticalHeaderLabels(rowLabels);

            fillBreakRow(alignment, *part);

            for (int slot = 0; slot < columns; ++slot) {
                const Slot &position = alignment.lyricSlots.at(slot);
                QString text;
                if (position.measureIndex < part->stream.measureCount()) {
                    const Measure &measure = part->stream.measures().at(position.measureIndex);
                    if (position.eventIndex < measure.events.size()) {
                        const Event &event = measure.events.at(position.eventIndex);
                        text = event.pitches.isEmpty()
                            ? QStringLiteral("—")
                            : QStringLiteral("%1%2/%3")
                                  .arg(event.pitches.first().step)
                                  .arg(event.pitches.first().octave)
                                  .arg(event.duration.base);
                    }
                }
                auto *item = new QTableWidgetItem(text);
                item->setFlags(Qt::ItemIsEnabled);
                if (position.dashedContinuation)
                    item->setBackground(colorPlaceholder());
                m_grid->setItem(1, slot, item);
            }

            for (int row = 0; row < alignment.sections.size(); ++row) {
                const AttachedSection &section = alignment.sections.at(row);
                for (int slot = 0; slot < columns; ++slot) {
                    const bool inSection = slot >= section.slotOffset
                        && slot < section.slotOffset + section.syllables.size();
                    auto *item = new QTableWidgetItem(
                        inSection ? alignment.syllableAt(section, slot) : QString());
                    if (!inSection) {
                        item->setFlags(Qt::ItemIsEnabled);
                        item->setBackground(colorMelisma());
                    } else {
                        item->setData(Qt::UserRole, section.key);
                        item->setData(Qt::UserRole + 1, slot - section.slotOffset);
                        if (item->text() == QLatin1String("_"))
                            item->setBackground(colorPlaceholder());
                    }
                    m_grid->setItem(row + 2, slot, item);
                }
                const int overflow
                    = section.slotOffset + static_cast<int>(section.syllables.size()) - columns;
                if (overflow > 0 && columns > 0) {
                    if (QTableWidgetItem *item = m_grid->item(row + 2, columns - 1))
                        item->setBackground(colorMismatch());
                }
            }

            QStringList hints;
            hints.append(tr("%1 lyric slots in %2").arg(columns).arg(partName));
            if (alignment.chorusFirst)
                hints.append(tr("refrain-first: the chorus starts at slot 0, verses follow it"));
            if (!alignment.errors.isEmpty())
                hints.append(alignment.errors.join(QStringLiteral("; ")));
            hints.append(tr("Grey cells take no syllable — a melisma continuation, or outside "
                            "this section's range."));
            hints.append(tr("Click the <b>break</b> row to break the line after a syllable; "
                            "right-click it for the optional and non-breaking lanes."));
            m_gridHint->setTextFormat(Qt::RichText);
            m_gridHint->setText(hints.join(QStringLiteral("  ·  ")));
        }
    }

    if (currentRow >= 0 && currentRow < m_grid->rowCount()
        && currentColumn >= 0 && currentColumn < m_grid->columnCount()) {
        m_grid->setCurrentCell(currentRow, currentColumn);
    }
    m_grid->resizeColumnsToContents();
    m_grid->horizontalScrollBar()->setValue(horizontalScroll);
    m_grid->verticalScrollBar()->setValue(verticalScroll);
    m_loading = wasLoading;
}

void LyricsPanel::commitCell(int row, int column)
{
    if (row < 2)
        return;  // the break and note rows are not text
    QTableWidgetItem *item = m_grid->item(row, column);
    if (!item)
        return;
    const QString key = item->data(Qt::UserRole).toString();
    const int indexInSection = item->data(Qt::UserRole + 1).toInt();
    if (key.isEmpty())
        return;

    const QString partName = gridPartName();
    const SongDocument &doc = m_session->effectiveDocument();
    const Part *part = doc.part(partName);
    const bool perPart = part && part->lyrics.contains(key);
    const QString original = perPart ? part->lyrics.value(key).rawText
                                     : doc.lyrics.value(key).rawText;

    // Rebuild the raw text from its syllables, keeping the ` -- ` structure the
    // author wrote everywhere the syllable itself did not change. The grid's
    // columns count *expanded* syllables, so an `@sN` token in the raw text
    // stands for several of them and the two indices are not the same number.
    QStringList tokens
        = original.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    int syllableIndex = 0;
    int target = -1;
    for (int i = 0; i < tokens.size() && target < 0; ++i) {
        const QString &token = tokens.at(i);
        if (token == QLatin1String("--"))
            continue;
        if (token.startsWith(u'@') && SongDocument::isSharedKey(QStringView(token).sliced(1))) {
            syllableIndex += static_cast<int>(
                LyricSection { sharedText(doc, part, token.sliced(1)), {}, {}, false }
                    .syllables()
                    .size());
            continue;
        }
        if (syllableIndex == indexInSection)
            target = i;
        ++syllableIndex;
    }
    if (target < 0) {
        // The syllable belongs to a shared section, and editing it here would
        // silently write it into this verse instead of into lyrics.sN.
        Q_EMIT statusMessage(
            tr("That syllable comes from a shared section — edit it on its own card."));
        rebuildGrid();
        return;
    }
    tokens[target] = item->text().trimmed();
    commitText(m_session->currentLanguage(), key, perPart ? partName : QString(),
        tokens.join(u' '));
}

void LyricsPanel::focusSlot(const QString &partName, int slot)
{
    const int index = m_gridPart->findData(partName);
    if (index >= 0)
        m_gridPart->setCurrentIndex(index);
    m_tabs->setCurrentIndex(1);
    if (slot >= 0 && slot < m_grid->columnCount() && m_grid->rowCount() > 2) {
        m_grid->setCurrentCell(2, slot);  // the first section row
        m_grid->scrollToItem(m_grid->item(2, slot));
    }
}

void LyricsPanel::focusEditor()
{
    if (!m_editors.isEmpty() && m_editors.first().editor) {
        m_editors.first().editor->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    m_tabs->setFocus(Qt::ShortcutFocusReason);
}

void LyricsPanel::insertUndertie()
{
    for (const EditorRef &row : m_editors) {
        if (row.editor->hasFocus()) {
            row.editor->insertPlainText(Undertie);
            return;
        }
    }
    if (QTableWidgetItem *item = m_grid->currentItem())
        item->setText(item->text() + Undertie);
}

} // namespace ope::ui
