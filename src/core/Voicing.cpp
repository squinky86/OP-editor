// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jon Hood, OpenPsalm.com
#include "Voicing.h"

#include <QSet>
#include <algorithm>
#include <tuple>

namespace ope::voicing {

QString normalize(QStringView value) { return value.trimmed().toString().toLower(); }

const QList<Role> &roles()
{
    static const QList<Role> values {
        { QStringLiteral("soprano"), QStringLiteral("Soprano"), QStringLiteral("S"), Stem::Up },
        { QStringLiteral("tenor1"), QStringLiteral("Tenor I"), QStringLiteral("T1"), Stem::Up },
        { QStringLiteral("alto"), QStringLiteral("Alto"), QStringLiteral("A"), Stem::Down },
        { QStringLiteral("tenor2"), QStringLiteral("Tenor II"), QStringLiteral("T2"), Stem::Down },
        { QStringLiteral("tenor"), QStringLiteral("Tenor"), QStringLiteral("T"), Stem::Up },
        { QStringLiteral("baritone"), QStringLiteral("Baritone"), QStringLiteral("Bar"), Stem::Up },
        { QStringLiteral("bass"), QStringLiteral("Bass"), QStringLiteral("Bass"), Stem::Down },
    };
    return values;
}

QStringList names()
{
    QStringList result;
    for (const Role &r : roles())
        result.append(r.name);
    return result;
}

const Role *role(QStringView value)
{
    const QString normalized = normalize(value);
    for (const Role &r : roles()) {
        if (r.name == normalized)
            return &r;
    }
    return nullptr;
}

int rank(const Part &part)
{
    const Role *r = role(part.choralType.valueOr({}));
    return r ? static_cast<int>(r - roles().constData()) : 99;
}

bool less(const Part &a, const Part &b)
{
    const auto key = [](const Part &p) {
        const QString lower = p.name.toLower();
        qsizetype suffix = lower.size();
        while (suffix > 0 && lower.at(suffix - 1) >= u'0' && lower.at(suffix - 1) <= u'9')
            --suffix;
        return std::tuple(rank(p), lower.sliced(suffix).toUInt(), lower, p.name);
    };
    return key(a) < key(b);
}

QList<int> orderedIndices(const SongDocument &doc)
{
    QList<int> result;
    for (int i = 0; i < doc.parts.size(); ++i)
        result.append(i);
    std::stable_sort(result.begin(), result.end(), [&](int a, int b) {
        return less(doc.parts.at(a), doc.parts.at(b));
    });
    return result;
}

const Part *lead(const SongDocument &doc)
{
    const auto found = std::min_element(doc.parts.cbegin(), doc.parts.cend(), less);
    return found == doc.parts.cend() ? nullptr : &*found;
}

QString label(const Part &part)
{
    const Role *r = role(part.choralType.valueOr({}));
    if (!r)
        return part.name;
    return normalize(part.name) == r->name || part.name == r->label
        ? r->label : QStringLiteral("%1 (%2)").arg(r->label, part.name);
}

QString shortLabel(const SongDocument &doc, const Part &part)
{
    const Role *r = role(part.choralType.valueOr({}));
    if (!r)
        return part.name;
    const int count = std::count_if(doc.parts.cbegin(), doc.parts.cend(), [&](const Part &p) {
        return role(p.choralType.valueOr({})) == r;
    });
    return count == 1 ? r->abbreviation : part.name;
}

QList<Staff> staves(const SongDocument &doc)
{
    QList<Staff> result;
    for (int index : orderedIndices(doc)) {
        const Part &part = doc.parts.at(index);
        auto found = result.end();
        if (part.staffNumber.present()) {
            found = std::find_if(result.begin(), result.end(), [&](const Staff &staff) {
                return staff.number == part.staffNumber.opt();
            });
        }
        if (found == result.end()) {
            result.append(Staff { part.staffNumber.opt(), {}, {}, {} });
            found = std::prev(result.end());
        }
        found->partIndices.append(index);
    }
    std::stable_sort(result.begin(), result.end(), [](const Staff &a, const Staff &b) {
        if (a.number.has_value() != b.number.has_value())
            return a.number.has_value();
        return a.number < b.number;
    });
    for (Staff &staff : result) {
        const Part &first = doc.parts.at(staff.partIndices.first());
        staff.clef = clefs::effective(first.clef.opt());
        QStringList problems;
        if (staff.number && *staff.number <= 0)
            problems.append(QStringLiteral("Staff %1 must be positive").arg(*staff.number));
        for (int index : staff.partIndices) {
            const Part &part = doc.parts.at(index);
            const auto clef = clefs::effective(part.clef.opt());
            if (!clef)
                problems.append(QStringLiteral("%1: unsupported clef “%2”")
                    .arg(part.name, part.clef.valueOr({})));
            else if (staff.clef && clef != staff.clef)
                problems.append(QStringLiteral("%1 (%2) and %3 (%4): conflicting clefs on staff %5")
                    .arg(first.name, staff.clef->name, part.name, clef->name)
                    .arg(staff.number.value_or(0)));
        }
        staff.problem = problems.join(QStringLiteral("; "));
        if (!problems.isEmpty())
            staff.clef.reset();
    }
    return result;
}

QList<Stem> stems(const SongDocument &doc, const QList<int> &indices)
{
    if (indices.size() == 1)
        return { Stem::Free };
    QList<Stem> result;
    QSet<int> occupied;
    QSet<QString> seenRoles;
    for (int index : indices) {
        const Role *r = role(doc.parts.at(index).choralType.valueOr({}));
        int voice = r && r->stem == Stem::Down ? 1 : 0;
        if (r && !seenRoles.contains(r->name)) {
            // Distinct roles keep their preferred direction even in a mixed
            // arrangement containing several upper or lower voices.
            while (occupied.contains(voice))
                voice += 2;
        } else {
            voice = 0;
            while (occupied.contains(voice))
                ++voice;
        }
        if (r)
            seenRoles.insert(r->name);
        occupied.insert(voice);
        result.append(voice % 2 == 0 ? Stem::Up : Stem::Down);
    }
    return result;
}

} // namespace ope::voicing
