// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QSettings>
#include <QCryptographicHash>
#include <QHash>
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
    void ingest(const QString& identity, const QString& title, const QString& authors) {
        const QString target = resolve(identity);
        if (target.isEmpty() || seen_.contains(target)) return;
        seen_.insert(target);
        auto entry = record(target);
        if (entry.value("manual").toBool()) return;
        bool updated = false;
        if (entry.value("title").toString().isEmpty() && !title.simplified().isEmpty()) {
            entry["title"] = title.simplified().left(4096);
            updated = true;
        }
        if (entry.value("authors").toString().isEmpty() && !authors.simplified().isEmpty()) {
            entry["authors"] = authors.simplified().left(4096);
            updated = true;
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
