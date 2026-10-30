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
            R"((?<journal>J\.?\s*Am\.?\s*Chem\.?\s*Soc\.?|J\.?\s*Org\.?\s*Chem\.?|J\.?\s*Med\.?\s*Chem\.?|Org\.?\s*Lett\.?|ACS\s+Catal\.?|Angew\.?\s*Chem\.?\s*(?:Int\.?\s*Ed\.?)?|Chem\.?\s*Sci\.?|Chem\.?\s*Commun\.?|Chem\.?\s*Eur\.?\s*J\.?|Adv\.?\s*Synth\.?\s*Catal\.?|Nat(?:ure)?\.?\s*Chem\.?|Nature|Science)\s*[,;:]?\s*(?<year>(?:19|20)\d{2})\s*[,;:]\s*(?<volume>\d{1,4})(?:\s*\(\s*\d{1,3}\s*\))?\s*[,;:]\s*(?<page>(?:e\d{5,9}|\d{1,7})(?:\s*-\s*\d{1,7})?))"),
            QRegularExpression::CaseInsensitiveOption));
        int checked = 0;
        for (const QString& raw : lines) {
            if (++checked > 140) break;
            const QString line = raw.simplified();
            if (line.size() > 320) continue;
            const auto match = pattern.match(line);
            if (!match.hasMatch()) continue;
            return {{"journal", match.captured(QStringLiteral("journal")).simplified()},
                    {"year", match.captured(QStringLiteral("year"))},
                    {"volume", match.captured(QStringLiteral("volume"))},
                    {"page", match.captured(QStringLiteral("page")).remove(QLatin1Char(' '))}};
        }
        return {};
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
        static const QRegularExpression authorsPattern(QStringLiteral(
            R"(^[\p{L}\s.'’\-*†‡]+(?:,\s*[\p{L}\s.'’\-*†‡]+){1,}$)"));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QString authors = lines[i];
            if (authors.size() < 14 || authors.size() > 200 ||
                !authorsPattern.match(authors).hasMatch()) continue;
            QString title = lines[i-1].simplified();
            if (title.size() < 18 && i >= 2)
                title = lines[i-2].simplified() + QLatin1Char(' ') + title;
            else if (i >= 2) {
                const QString previous = lines[i-2].simplified();
                if (previous.size() >= 18 && previous.size() <= 180 &&
                    !previous.contains(QStringLiteral("supporting information"), Qt::CaseInsensitive) &&
                    !previous.contains(QStringLiteral("article"), Qt::CaseInsensitive) &&
                    !previous.contains(QStringLiteral("doi"), Qt::CaseInsensitive))
                    title = previous + QLatin1Char(' ') + title;
            }
            if (title.size() < 18 || title.size() > 350 ||
                title.contains(QStringLiteral("supporting information"), Qt::CaseInsensitive) ||
                title.contains(QStringLiteral("department"), Qt::CaseInsensitive) ||
                title.contains(QStringLiteral("university"), Qt::CaseInsensitive) ||
                title.contains(QStringLiteral("doi"), Qt::CaseInsensitive) ||
                title.contains(QRegularExpression(QStringLiteral(R"((?:19|20)\d{2}\s*[,;])"))))
                continue;
            return {{"title", title.left(4096)}, {"authors", authors.left(4096)}};
        }
        return {};
    }

    QVariantMap record(const QString& identity) const {
        return settings().value(key(resolve(identity))).toMap();
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
        fill(QStringLiteral("title"), title, 4096);
        fill(QStringLiteral("authors"), authors, 4096);
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
