// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QSettings>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QString>
#include <functional>

namespace luna {
// Local-only paper bibliography. No DOI extraction, network lookup or PDF uploads.
class Bibliography final : public QObject {
public:
    explicit Bibliography(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void(const QString&)> changed;

    // A pasted full citation takes priority over structured fields.
    static QString citation(const QVariantMap& entry) {
        const QString pasted = entry.value("citation").toString().simplified();
        if (!pasted.isEmpty()) return pasted;
        const QString journal = entry.value("journal").toString().simplified();
        const QString year = entry.value("year").toString().simplified();
        const QString volume = entry.value("volume").toString().simplified();
        const QString pages = entry.value("page").toString().simplified();
        if (journal.isEmpty() && year.isEmpty()) return QString();
        QString result = journal;
        if (!year.isEmpty()) result += (result.isEmpty() ? QString() : QStringLiteral(" ")) + year;
        if (!volume.isEmpty()) result += QStringLiteral(", ") + volume;
        if (!pages.isEmpty()) result += QStringLiteral(", ") + pages;
        return result;
    }

    // Conservative citation extraction from the printed first page only.
    // Only known journal patterns are accepted; unknown text is left untouched.
    // No DOI parsing or online queries are performed.
    static QVariantMap firstPageCitation(QString text) {
        text = text.left(32000);
        for (const QChar mark : {QChar(0x2013), QChar(0x2014), QChar(0x2212)})
            text.replace(mark, QLatin1Char('-'));
        text.replace(QChar(0x00a0), QLatin1Char(' '));
        const auto lines = text.split(QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
        static const QRegularExpression pattern(QStringLiteral(
            R"((?<journal>J\.?\s*Am\.?\s*Chem\.?\s*Soc\.?|J\.?\s*Org\.?\s*Chem\.?|J\.?\s*Med\.?\s*Chem\.?|J\.?\s*Phys\.?\s*Chem\.?\s*[ABC]\.?|Org\.?\s*Lett\.?|ACS\s+Catal\.?|Angew\.?\s*Chem\.?\s*(?:Int\.?\s*Ed\.?)?|Chem\.?\s*Sci\.?|Chem\.?\s*Commun\.?|Chem\.?\s*Eur\.?\s*J\.?|Adv\.?\s*Synth\.?\s*Catal\.?|Org\.?\s*Biomol\.?\s*Chem\.?|Nat(?:ure)?\.?\s*Chem\.?|Nature|Science)\s*[,;:]?\s*(?<year>(?:19|20)\d{2})\s*[,;:]\s*(?<volume>\d{1,4})(?:\s*\(\s*\d{1,3}\s*\))?\s*[,;:]\s*(?<page>(?:e\d{5,9}|\d{1,7})(?:\s*-\s*\d{1,7})?))"),
            QRegularExpression::CaseInsensitiveOption);
        int checked = 0;
        for (qsizetype i = 0; i < lines.size() && checked < 40; ++i, ++checked) {
            // Many journal headers break the year or volume across text lines.
            // Consider at most three adjacent lines, not the full document.
            QString candidate;
            for (qsizetype next = i; next < lines.size() && next < i + 3; ++next) {
                candidate += (candidate.isEmpty() ? QString() : QStringLiteral(" ")) + lines[next].simplified();
                if (candidate.size() > 320) break;
                const auto match = pattern.match(candidate);
                if (!match.hasMatch()) continue;
                return {{"journal", match.captured(QStringLiteral("journal")).simplified()},
                        {"year", match.captured(QStringLiteral("year"))},
                        {"volume", match.captured(QStringLiteral("volume"))},
                        {"page", match.captured(QStringLiteral("page")).remove(QLatin1Char(' '))}};
            }
        }
        // Some publisher proofs show the journal name, but deliberately do
        // not yet contain a final publication year, volume or article number.
        // Record only an explicit masthead; never infer missing publication data.
        for (qsizetype i = 0; i < lines.size() && i < 12; ++i) {
            const QString heading = lines[i].simplified();
            if (heading.compare(QStringLiteral("Nature Communications"), Qt::CaseInsensitive) == 0)
                return {{"journal", QStringLiteral("Nature Communications")}};
        }
        return {};
    }

    // Papers can list many authors; LUNA cards display only the final named
    // author, without claiming they are always the corresponding author.
    static QString lastListedAuthor(QString authors) {
        authors = authors.simplified();
        if (authors.isEmpty()) return {};
        // Publisher author symbols are not part of the actual name.
        authors.replace(QRegularExpression(QStringLiteral(R"([\u2020\u2021*]+)")), QString());
        // RSC commonly uses "and Name" as the final contributor.
        static const QRegularExpression lastAnd(QStringLiteral(
            R"((?:^|[,;]\s*|\s+)(?:and|&)\s+([\p{L}][\p{L}\p{M}\s.'’\-]{2,75})\s*$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto joined = lastAnd.match(authors);
        if (joined.hasMatch()) return joined.captured(1).simplified();
        const QStringList entries = authors.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (entries.size() < 2) return authors.left(90);
        QString last = entries.last().simplified();
        last.remove(QRegularExpression(QStringLiteral(R"(^(?:and|&)\s+)"),
                                       QRegularExpression::CaseInsensitiveOption));
        return last.left(90);
    }

    // Return tentative title/author candidates for manual review, especially
    // for Supporting Information PDFs without printed journal citations.
    static QVariantMap firstPageIdentity(const QString& text) {
        const QStringList raw = text.left(16000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
        QStringList lines;
        for (const QString& candidate : raw) {
            const QString line = candidate.simplified();
            if (!line.isEmpty()) lines.append(line);
            if (lines.size() >= 55) break;
        }
        // Supplementary information often has no original article metadata.
        // Do not mine captions, instructions or the body for fake title/authors.
        for (qsizetype i = 0; i < lines.size() && i < 16; ++i) {
            if (lines[i].contains(QStringLiteral("Supporting Information"), Qt::CaseInsensitive) ||
                lines[i].contains(QStringLiteral("Supplementary Information"), Qt::CaseInsensitive))
                return {};
        }
        if (firstPageCitation(text).value("journal").toString().isEmpty()) return {};
        static const QRegularExpression authorsPattern(QStringLiteral(
            R"(^[\p{L}\p{N}\s.,'’\-*†‡&]+$)"));
        static const QRegularExpression headingPattern(QStringLiteral(
            R"(^(?:(?:Nature|Nature Communications|Article in Press|Supporting Information|Research Article)|(?:Received|Accepted|Published|Cite this article as|DOI)\s*:?.*)$)"),
            QRegularExpression::CaseInsensitiveOption);
        for (qsizetype i = 1; i < lines.size() && i < 32; ++i) {
            QString authors = lines[i];
            if (authors.size() < 14 || authors.size() > 220 ||
                !authors.contains(QLatin1Char(',')) ||
                !authorsPattern.match(authors).hasMatch() ||
                authors.contains(QStringLiteral("department"), Qt::CaseInsensitive) ||
                authors.contains(QStringLiteral("university"), Qt::CaseInsensitive) ||
                authors.contains(QStringLiteral("institute"), Qt::CaseInsensitive)) continue;
            // RSC author lines often wrap after a comma. Include up to three
            // immediately following author-like lines, but stop before affiliations
            // or abstract prose. Keep the candidate review-only.
            for (qsizetype next = i + 1; next < lines.size() && next <= i + 3; ++next) {
                const QString continuation = lines[next];
                const bool appearsAuthorLike = continuation.contains(QLatin1Char(',')) ||
                    continuation.startsWith(QStringLiteral("and "), Qt::CaseInsensitive) ||
                    continuation.contains(QStringLiteral(" and "), Qt::CaseInsensitive);
                if (!appearsAuthorLike || continuation.size() > 220 ||
                    !authorsPattern.match(continuation).hasMatch() ||
                    continuation.contains(QStringLiteral("department"), Qt::CaseInsensitive) ||
                    continuation.contains(QStringLiteral("university"), Qt::CaseInsensitive) ||
                    continuation.contains(QStringLiteral("institute"), Qt::CaseInsensitive) ||
                    continuation.contains(QStringLiteral("abstract"), Qt::CaseInsensitive)) break;
                authors += QLatin1Char(' ') + continuation;
            }
            // Skip received/accepted dates and publisher banners between title
            // and authors. Never write the candidates without explicit review.
            QStringList titleLines;
            for (qsizetype j = i; j-- > 0 && j + 10 >= i && titleLines.size() < 3;) {
                const QString candidate = lines[j];
                if (headingPattern.match(candidate).hasMatch() ||
                    candidate.contains(QStringLiteral("https://"), Qt::CaseInsensitive) ||
                    candidate.contains(QStringLiteral("doi.org"), Qt::CaseInsensitive))
                    continue;
                if (candidate.size() > 250) break;
                if (candidate.size() >= 5)
                    titleLines.prepend(candidate);
            }
            const QString title = titleLines.join(QLatin1Char(' '));
            if (!credibleTitle(title) || !credibleAuthor(lastListedAuthor(authors)))
                continue;
            return {{"title", title.left(4096)}, {"authors", lastListedAuthor(authors)}};
        }
        return {};
    }

    // Recall's evidence rule: never present arbitrary strings as trusted metadata.
    // In particular PDF printer defaults and body sentences are not paper titles.
    static bool credibleTitle(const QString& value) {
        const QString text = value.simplified();
        if (text.size() < 12 || text.size() > 240) return false;
        static const QRegularExpression placeholder(QStringLiteral(
            R"((?:^|\b)(?:No Job Name|Microsoft Word|Untitled|New Document|Document1|Supporting Information|Revised Supporting Information)(?:\b|$))"),
            QRegularExpression::CaseInsensitiveOption);
        if (placeholder.match(text).hasMatch() ||
            text.contains(QStringLiteral("as well as"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("department of"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("university of"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("http"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("abstract"), Qt::CaseInsensitive))
            return false;
        return true;
    }
    static bool credibleAuthor(const QString& value) {
        const QString text = lastListedAuthor(value);
        if (text.size() < 5 || text.size() > 90 ||
            text.contains(QStringLiteral("as well as"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("department"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("university"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("the energetics"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("research"), Qt::CaseInsensitive))
            return false;
        static const QRegularExpression name(QStringLiteral(
            R"(^[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}$)"));
        return name.match(text).hasMatch();
    }
    // Reuse Recall's field-level conflict protection. Only fill missing/known-bad
    // values, never replace a plausible manual correction or invent absent data.
    bool enrichFromFirstPage(const QString& identity, const QVariantMap& proposal) {
        if (identity.isEmpty() || proposal.isEmpty()) return false;
        QVariantMap value = record(identity);
        bool changedField = false;
        const auto fill = [&value, &proposal, &changedField](const char* key, bool good) {
            const QString field = QString::fromLatin1(key);
            const QString next = proposal.value(field).toString().simplified();
            if (!good || next.isEmpty()) return;
            const QString old = value.value(field).toString().simplified();
            if (!old.isEmpty()) return;
            value[field] = next.left(4096);
            changedField = true;
        };
        fill("title", credibleTitle(proposal.value("title").toString()));
        fill("authors", credibleAuthor(proposal.value("authors").toString()));
        fill("journal", !proposal.value("journal").toString().isEmpty());
        const QString year = proposal.value("year").toString();
        static const QRegularExpression yyyy(QStringLiteral(R"(^[12]\d{3}$)"));
        fill("year", yyyy.match(year).hasMatch());
        static const QRegularExpression volumePattern(QStringLiteral(R"(^\d{1,4}$)"));
        fill("volume", volumePattern.match(proposal.value("volume").toString()).hasMatch());
        static const QRegularExpression pagePattern(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,7})(?:[-–]\d{1,7})?$)"),
                                                    QRegularExpression::CaseInsensitiveOption);
        fill("page", pagePattern.match(proposal.value("page").toString()).hasMatch());
        if (!changedField) return false;
        value["source"] = QStringLiteral("PDF first-page evidence");
        // Preserve prior manual ownership, but never mark machine extraction manual.
        return save(identity, value, value.value("manual").toBool());
    }

    QVariantMap record(const QString& identity) const {
        QVariantMap value = settings().value(key(resolve(identity))).toMap();
        if (!credibleTitle(value.value("title").toString())) value.remove("title");
        if (!credibleAuthor(value.value("authors").toString())) value.remove("authors");
        return value;
    }

    bool save(const QString& identity, QVariantMap value, bool manual = true) {
        if (identity.isEmpty()) return false;
        const QString target = resolve(identity);
        value.remove("doi"); // Keep old users' local citation records DOI-free, too.
        value["manual"] = manual;
        auto store = settings();
        store.setValue(key(target), value);
        store.sync();
        if (store.status() != QSettings::NoError) return false;
        if (changed) changed(target);
        return true;
    }

    // Use only PDF info metadata already read by the native PDF viewer.
    // Do not block page opening on a second parse and never overwrite user edits.
    void ingest(const QString& identity, const QString& title, const QString& authors,
                const QVariantMap& structured = {}) {
        const QString target = resolve(identity);
        if (target.isEmpty() || seen_.contains(target)) return;
        seen_.insert(target);
        auto entry = record(target);
        if (entry.value("manual").toBool()) return;
        bool updated = false;
        const auto fill = [&entry, &updated](const QString& key, const QString& text, int maxLength) {
            const QString value = text.simplified().left(maxLength);
            if (entry.value(key).toString().isEmpty() && !value.isEmpty()) {
                entry[key] = value;
                updated = true;
            }
        };
        if (credibleTitle(title)) fill(QStringLiteral("title"), title, 4096);
        if (credibleAuthor(authors)) fill(QStringLiteral("authors"), lastListedAuthor(authors), 4096);
        for (const char* field : {"journal", "year", "volume", "page"}) {
            const QString key = QString::fromLatin1(field);
            QString value = structured.value(key).toString();
            if (key == QStringLiteral("year")) {
                static const QRegularExpression validYear(QStringLiteral(R"(^[0-9]{4}$)"));
                if (!validYear.match(value.trimmed()).hasMatch()) continue;
            }
            fill(key, value, 256);
        }
        if (updated) {
            entry["source"] = QStringLiteral("PDF metadata");
            save(target, entry, false);
        }
    }

    bool relocate(const QString& oldIdentity, const QString& newIdentity) {
        if (oldIdentity.isEmpty() || newIdentity.isEmpty()) return false;
        const QString old = resolve(oldIdentity);
        if (old == newIdentity) return true;
        const QVariantMap data = record(old);
        auto store = settings();
        // Check the destination key directly: it may be an old in-memory alias.
        if (!store.value(key(newIdentity)).toMap().isEmpty()) return false;
        if (!data.isEmpty()) {
            store.setValue(key(newIdentity), data);
            store.sync();
            if (store.status() != QSettings::NoError) return false;
        }
        store.remove(key(old));
        store.sync();
        if (store.status() != QSettings::NoError) return false;
        aliases_.remove(newIdentity);
        aliases_.insert(old, newIdentity);
        seen_.remove(old);
        if (changed) changed(newIdentity);
        return true;
    }

private:
    static QSettings settings() {
        return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                         QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    }
    static QString key(const QString& identity) {
        return QStringLiteral("library/bibliography/") +
            QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    QString resolve(QString identity) const {
        QSet<QString> visited;
        while (aliases_.contains(identity) && !visited.contains(identity)) {
            visited.insert(identity);
            identity = aliases_.value(identity);
        }
        return identity;
    }
    QHash<QString,QString> aliases_;
    QSet<QString> seen_;
};
} // namespace luna
