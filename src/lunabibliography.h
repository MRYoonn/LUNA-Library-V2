// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QSet>
#include <QSettings>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace luna {
// Bibliography is optional local metadata. No PDF bytes, filenames or authors
// leave the machine: the only external query is a recognized DOI to Crossref.
class Bibliography final : public QObject {
public:
    explicit Bibliography(QObject* parent) : QObject(parent) {}
    ~Bibliography() override {
        for (auto* reply : network_.findChildren<QNetworkReply*>()) { disconnect(reply,nullptr,this,nullptr); reply->abort(); }
    }
    std::function<void(const QString&)> changed;
    static QString doi(QString text) {
        static const QRegularExpression pattern(QStringLiteral("10\\.\\d{4,9}/[^\\s<>\"{}]+"),QRegularExpression::CaseInsensitiveOption);
        QString result = pattern.match(text).captured();
        while (!result.isEmpty() && QStringLiteral(".,;)").contains(result.back())) result.chop(1);
        return result.size() <= 256 ? result.toLower() : QString();
    }
    static QVariantMap crossref(const QByteArray& bytes, const QString& expectedDoi) {
        if (bytes.size() > 262144 || expectedDoi.isEmpty()) return {};
        QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes,&error);
        if (error.error != QJsonParseError::NoError) return {};
        const auto envelope = document.object(); const auto message = envelope.value("message").toObject();
        if (envelope.value("status").toString() != "ok" || doi(message.value("DOI").toString()) != expectedDoi.toLower()) return {};
        auto first = [&](const char* name) { const auto array = message.value(name).toArray(); return array.isEmpty() ? QString() : array.first().toString().left(4096); };
        QVariantMap result{{"doi",expectedDoi.toLower()}, {"title",first("title")}, {"journal",first("short-container-title")}, {"source",QStringLiteral("Crossref")}};
        if (result.value("journal").toString().isEmpty()) result["journal"] = first("container-title");
        QStringList authors;
        for (const auto& value : message.value("author").toArray()) {
            const auto author = value.toObject(); QString name = (author.value("given").toString() + " " + author.value("family").toString()).simplified();
            if (name.isEmpty()) name = author.value("name").toString();
            if (!name.isEmpty()) authors.append(name.left(512)); if (authors.size() == 100) break;
        }
        result["authors"] = authors.join("; ");
        for (const auto* field : {"volume","issue","page"}) result[field] = message.value(field).toString().left(256);
        for (const auto* field : {"published-print","published-online","published"}) {
            const auto dates = message.value(field).toObject().value("date-parts").toArray();
            if (dates.isEmpty()) continue;
            const auto parts = dates.first().toArray(); const int year = parts.isEmpty() ? 0 : parts.first().toInt();
            if (year > 0 && year <= 9999) { result["year"] = QString::number(year); break; }
        }
        return result;
    }
    QVariantMap record(const QString& identity) const { return settings().value(key(resolve(identity))).toMap(); }
    bool save(const QString& identity, QVariantMap value, bool manual = true) {
        value["manual"] = manual; auto store = settings(); store.setValue(key(resolve(identity)),value); store.sync();
        if (store.status() != QSettings::NoError) return false;
        if (changed) changed(resolve(identity)); return true;
    }
    void ingest(const QString& identity, const QString& title, const QString& author, const QString& doiMetadata) {
        const QString target = resolve(identity); if (seen_.contains(target)) return; seen_.insert(target);
        auto data = record(target); if (data.value("manual").toBool()) return;
        if (data.isEmpty()) {
            data = {{"title",title.left(4096)}, {"authors",author.left(4096)}, {"doi",doi(doiMetadata)}, {"source",QStringLiteral("PDF metadata")}};
            if (!save(target,data,false)) return;
        }
        if (!data.value("doi").toString().isEmpty() && data.value("source").toString() != "Crossref") lookup(target,data.value("doi").toString());
    }
    void lookup(const QString& identity, const QString& requested) {
        const QString normalized = doi(requested), target = resolve(identity);
        if (normalized.isEmpty() || pending_.contains(target) || queue_.size() >= 32) return;
        pending_.insert(target); queue_.enqueue({target,normalized}); pump();
    }
    bool relocate(const QString& oldIdentity, const QString& newIdentity) {
        const QString old = resolve(oldIdentity); const auto data = record(old);
        if (old == newIdentity) return true;
        aliases_.remove(newIdentity);
        if (!data.isEmpty() && !save(newIdentity,data,data.value("manual").toBool())) return false;
        auto store = settings(); store.remove(key(old)); store.sync();
        aliases_.insert(old,newIdentity); seen_.insert(newIdentity); return store.status() == QSettings::NoError;
    }
private:
    static QSettings settings() { return QSettings(QSettings::defaultFormat(),QSettings::UserScope,QStringLiteral("LUNA"),QStringLiteral("LUNA Library")); }
    static QString key(const QString& identity) { return "library/bibliography/" + QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),QCryptographicHash::Sha256).toHex()); }
    QString resolve(QString identity) const { QSet<QString> visited; while (aliases_.contains(identity) && !visited.contains(identity)) { visited.insert(identity); identity = aliases_.value(identity); } return identity; }
    void pump() {
        while (active_ < 2 && !queue_.isEmpty()) {
            const auto task = queue_.dequeue(); ++active_;
            QNetworkRequest request(QUrl::fromEncoded("https://api.crossref.org/works/" + QUrl::toPercentEncoding(task.second)));
            request.setRawHeader("Accept","application/json"); request.setRawHeader("User-Agent","LUNA-Library/0.1");
            request.setTransferTimeout(10000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
            auto* reply = network_.get(request); auto bytes = std::make_shared<QByteArray>();
            connect(reply,&QNetworkReply::readyRead,this,[reply,bytes] { bytes->append(reply->readAll()); if (bytes->size() > 262144) reply->abort(); });
            connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,task] {
                bytes->append(reply->readAll()); const auto data = crossref(*bytes,task.second);
                const QString target = resolve(task.first);
                if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && !data.isEmpty() && !record(target).value("manual").toBool()) save(target,data,false);
                pending_.remove(task.first); --active_; reply->deleteLater(); pump();
            });
        }
    }
    QNetworkAccessManager network_; QQueue<QPair<QString,QString>> queue_; QSet<QString> pending_,seen_; QHash<QString,QString> aliases_; int active_ = 0;
};
}
