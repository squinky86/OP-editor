// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com

#include "Session.h"

#include <QDir>
#include <QFileInfo>
#include <QUndoCommand>

namespace ope {
namespace {

/// Undo by snapshot.
///
/// A song document is a few hundred kilobytes at most, so keeping a copy either
/// side of an edit is cheap — and it is exact, which a hand-written inverse for
/// every one of the format's constructs would not reliably be. The editor gets
/// correct undo for every operation, present and future, for one class.
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Session *session, QString language, SongDocument before, SongDocument after,
        const QString &text)
        : QUndoCommand(text)
        , m_session(session)
        , m_language(std::move(language))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    void undo() override { m_session->restore(m_language, m_before); }
    void redo() override
    {
        if (m_first) {
            // The mutation has already been applied; redo() runs once at push.
            m_first = false;
            return;
        }
        m_session->restore(m_language, m_after);
    }

private:
    Session *m_session = nullptr;
    QString m_language;
    SongDocument m_before;
    SongDocument m_after;
    bool m_first = true;
};

} // namespace

Session::Session(QObject *parent) : QObject(parent)
{
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool) { Q_EMIT dirtyChanged(); });
}

std::expected<void, LoadError> Session::openSong(const QString &basePath)
{
    auto base = io::load(basePath);
    if (!base)
        return std::unexpected(base.error());

    // Every translation must load too: opening a song with a broken overlay and
    // silently hiding the overlay would be the worst of both worlds.
    QHash<QString, SongDocument> documents;
    QStringList languages;
    const QString baseLanguage = base->language;
    documents.insert(baseLanguage, *base);
    languages.append(baseLanguage);

    const QDir dir = QFileInfo(basePath).dir();
    for (const QString &file :
        dir.entryList(QStringList { QStringLiteral("song_*.toml") }, QDir::Files, QDir::Name)) {
        const QString code = i18n::codeFromFilename(file);
        if (code.isEmpty())
            continue;
        auto overlay = io::load(dir.filePath(file));
        if (!overlay)
            return std::unexpected(overlay.error());
        documents.insert(code, *overlay);
        languages.append(code);
    }

    close();
    m_documents = std::move(documents);
    m_languages = languages;
    m_baseLanguage = baseLanguage;
    m_currentLanguage = baseLanguage;
    m_isNewFile = false;
    m_selection = {};
    refresh();
    Q_EMIT languageChanged();
    Q_EMIT documentChanged();
    return {};
}

void Session::adoptNewDocument(SongDocument document)
{
    close();
    const QString language
        = document.language.isEmpty() ? i18n::defaultLanguage() : document.language;
    document.language = language;
    m_baseLanguage = document.isOverlay ? QString() : language;
    m_documents.insert(language, std::move(document));
    m_languages = { language };
    m_currentLanguage = language;
    m_isNewFile = true;
    m_selection = {};
    refresh();
    Q_EMIT languageChanged();
    Q_EMIT documentChanged();
    Q_EMIT dirtyChanged();
}

void Session::close()
{
    m_documents.clear();
    m_alignments.clear();
    m_languages.clear();
    m_currentLanguage.clear();
    m_baseLanguage.clear();
    m_findings.clear();
    m_selection = {};
    m_undo.clear();
    m_isNewFile = false;
}

void Session::setCurrentLanguage(const QString &code)
{
    if (!m_documents.contains(code) || code == m_currentLanguage)
        return;
    m_currentLanguage = code;
    m_selection = {};
    refresh();
    Q_EMIT languageChanged();
    Q_EMIT documentChanged();
}

SongDocument &Session::document() { return m_documents[m_currentLanguage]; }

const SongDocument &Session::document() const
{
    static const SongDocument empty;
    const auto it = m_documents.constFind(m_currentLanguage);
    return it == m_documents.constEnd() ? empty : *it;
}

const SongDocument *Session::baseDocument() const
{
    const auto it = m_documents.constFind(m_baseLanguage);
    return it == m_documents.constEnd() ? nullptr : &*it;
}

const SongDocument &Session::effectiveDocument() const { return m_effective; }

const PartAlignment &Session::alignment(const QString &partName) const
{
    const auto it = m_alignments.constFind(partName);
    return it == m_alignments.constEnd() ? m_emptyAlignment : *it;
}

void Session::setSelection(Selection selection)
{
    if (selection == m_selection)
        return;
    m_selection = selection;
    Q_EMIT selectionChanged();
}

const Event *Session::selectedEvent() const
{
    if (!m_selection.hasEvent())
        return nullptr;
    const SongDocument &doc = effectiveDocument();
    if (m_selection.partIndex >= doc.parts.size())
        return nullptr;
    const Part &part = doc.parts.at(m_selection.partIndex);
    if (m_selection.measureIndex >= part.stream.measureCount())
        return nullptr;
    const Measure &measure = part.stream.measures().at(m_selection.measureIndex);
    if (m_selection.eventIndex >= measure.events.size())
        return nullptr;
    return &measure.events.at(m_selection.eventIndex);
}

bool Session::isDirty() const
{
    if (m_isNewFile)
        return true;
    for (auto it = m_documents.constBegin(); it != m_documents.constEnd(); ++it) {
        if (it->isDirty())
            return true;
    }
    return false;
}

QString Session::currentPath() const { return document().path; }

std::expected<void, QString> Session::save()
{
    SongDocument &doc = document();
    if (doc.path.isEmpty())
        return std::unexpected(tr("this song has no file path yet"));

    const QByteArray bytes = m_isNewFile ? io::serializeFresh(doc) : io::serialize(doc);
    if (auto written = io::writeAtomically(doc.path, bytes); !written)
        return std::unexpected(written.error());

    // Re-read so spans point at the bytes now on disk; otherwise the next save
    // would splice against stale offsets.
    auto reloaded = io::load(doc.path);
    if (reloaded) {
        const QString language = doc.language;
        reloaded->language = language;
        m_documents.insert(language, *reloaded);
    } else {
        doc.markClean();
    }
    m_isNewFile = false;
    m_undo.setClean();
    refresh();
    Q_EMIT documentChanged();
    Q_EMIT dirtyChanged();
    return {};
}

void Session::mutate(
    const QString &description, const std::function<void(SongDocument &)> &mutation)
{
    if (!isOpen())
        return;
    SongDocument before = document();
    mutation(document());
    for (Part &part : document().parts) {
        // Any edit may have changed the notation; keeping the parsed stream and
        // the text in step here means no caller has to remember to.
        if (part.notes.dirty())
            part.reparse();
        part.stream.reindex();
    }
    SongDocument after = document();
    m_undo.push(new SnapshotCommand(this, m_currentLanguage, std::move(before), std::move(after),
        description));
    refresh();
    Q_EMIT documentChanged();
    Q_EMIT dirtyChanged();
}

void Session::restore(const QString &language, const SongDocument &document)
{
    m_documents.insert(language, document);
    if (m_currentLanguage != language)
        m_currentLanguage = language;
    refresh();
    Q_EMIT documentChanged();
    Q_EMIT dirtyChanged();
}

void Session::refresh()
{
    if (!isOpen()) {
        m_effective = {};
        m_findings.clear();
        m_alignments.clear();
        return;
    }

    const SongDocument &current = document();
    const SongDocument *base = baseDocument();
    m_effective = (current.isOverlay && base) ? mergeOverlay(*base, current) : current;

    m_alignments.clear();
    for (const Part &part : m_effective.parts)
        m_alignments.insert(part.name, alignPart(m_effective, part));

    m_findings = validate(m_effective, i18n::isKnown(m_currentLanguage), m_baseLanguage);
}

PlaybackPlan Session::buildPlaybackPlan(const PlaybackOptions &options) const
{
    return buildPlan(effectiveDocument(), options);
}

namespace {

/// The three lanes, in the order the UI presents them.
constexpr BreakKind AllBreakKinds[3] { BreakKind::Required, BreakKind::Optional,
    BreakKind::NonBreaking };

Field<QList<PhraseBreak>> &fieldFor(SongDocument &doc, BreakKind kind)
{
    switch (kind) {
    case BreakKind::Optional: return doc.optionalPhraseBreaks;
    case BreakKind::NonBreaking: return doc.nonBreakingPhraseBreaks;
    case BreakKind::Required: break;
    }
    return doc.phraseBreaks;
}

const Field<QList<PhraseBreak>> &fieldFor(const SongDocument &doc, BreakKind kind)
{
    switch (kind) {
    case BreakKind::Optional: return doc.optionalPhraseBreaks;
    case BreakKind::NonBreaking: return doc.nonBreakingPhraseBreaks;
    case BreakKind::Required: break;
    }
    return doc.phraseBreaks;
}

} // namespace

std::optional<BreakKind> Session::phraseBreakAt(PhraseBreak position) const
{
    const SongDocument &doc = effectiveDocument();
    for (const BreakKind kind : AllBreakKinds) {
        const Field<QList<PhraseBreak>> &field = fieldFor(doc, kind);
        if (field.present() && field->contains(position))
            return kind;
    }
    return std::nullopt;
}

void Session::setPhraseBreak(PhraseBreak position, std::optional<BreakKind> kind)
{
    if (phraseBreakAt(position) == kind)
        return;
    const QString description = !kind ? tr("Remove phrase break")
        : *kind == BreakKind::Required ? tr("Add phrase break")
        : *kind == BreakKind::Optional ? tr("Add optional phrase break")
                                       : tr("Add non-breaking phrase break");
    // A translation inherits the lanes it does not define, and defining one
    // replaces it whole — so an edit starts from the merged list, not from the
    // overlay's own empty one, and only the lane that actually changes is
    // written. Touching the optional lane must not freeze the required one.
    const SongDocument &effective = effectiveDocument();
    mutate(description, [position, kind, &effective](SongDocument &doc) {
        for (const BreakKind lane : AllBreakKinds) {
            QList<PhraseBreak> breaks = fieldFor(effective, lane).valueOr({});
            const bool wanted = kind && *kind == lane;
            const int removed = breaks.removeAll(position);
            if (removed == 0 && !wanted)
                continue;
            if (wanted)
                breaks.append(position);
            std::sort(breaks.begin(), breaks.end());
            Field<QList<PhraseBreak>> &field = fieldFor(doc, lane);
            if (breaks.isEmpty())
                field.clear();
            else
                field.set(breaks);
        }
    });
}

void Session::togglePhraseBreak(PhraseBreak position, BreakKind kind)
{
    setPhraseBreak(position, phraseBreakAt(position) == kind ? std::nullopt
                                                             : std::optional<BreakKind>(kind));
}

} // namespace ope
