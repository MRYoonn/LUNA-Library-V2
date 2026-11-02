// SPDX-License-Identifier: MIT
#pragma once
// Asynchronous DOI-only metadata lookup. Never transmit PDF bytes, file paths,
// extracted text or user notes. No dependencies on Recall private infrastructure.
#include "lunametadataengine.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <functional>

namespace luna {
class MetadataFetcher final : public QObject {
public:
    using Done = std::function<void(QVariantMap, QString)>;
    explicit MetadataFetcher(QObject* parent=nullptr) : QObject(parent) {}

    void lookup(const QString& inputDoi, Done done) {
        const QString doi=Metadata::normalizeDoi(inputDoi);
        if(doi.isEmpty()) {done({}, QStringLiteral("invalid_doi"));return;}
        if(qEnvironmentVariableIsSet("LUNA_METADATA_OFFLINE")) {
            done({},QStringLiteral("offline"));return;
        }
        if(busy_.contains(doi)) {done({},QStringLiteral("loading"));return;}
        busy_.insert(doi);
        const auto finish=[this,doi,done](QVariantMap record, QString error) {
            busy_.remove(doi);
            done(record,error);
        };
        const QUrl crossref(QStringLiteral("https://api.crossref.org/works/")+
            QString::fromLatin1(QUrl::toPercentEncoding(doi)));
        fetch(crossref,QStringLiteral("Crossref"),
            [this,doi,finish](QJsonObject document, QString failure) {
            QVariantMap cr=Metadata::fromCrossref(document.value(QStringLiteral("message")).toObject(), doi);
            if(cr.isEmpty() && failure.isEmpty()) failure=QStringLiteral("crossref_doi_mismatch");
            const bool missing=cr.isEmpty() ||
                cr.value(QStringLiteral("title")).toString().isEmpty() ||
                cr.value(QStringLiteral("journal")).toString().isEmpty() ||
                cr.value(QStringLiteral("year")).toString().isEmpty() ||
                cr.value(QStringLiteral("authors_structured")).toStringList().isEmpty() ||
                cr.value(QStringLiteral("volume")).toString().isEmpty() ||
                (cr.value(QStringLiteral("page")).toString().isEmpty() &&
                 cr.value(QStringLiteral("article_number")).toString().isEmpty());
            if(!missing) {finish(cr,{});return;}
            // OpenAlex is a fallback: never let an unverified result replace a
            // DOI-matched Crossref value. No broad title/author search.
            const QUrl alex(QStringLiteral("https://api.openalex.org/works/https://doi.org/")+doi);
            fetch(alex,QStringLiteral("OpenAlex"),[cr,doi,finish,failure](QJsonObject obj, QString error) mutable {
                QVariantMap secondary=Metadata::fromOpenAlex(obj,doi);
                if(secondary.isEmpty()) {
                    if(cr.isEmpty()) {
                        finish({},error.isEmpty()?QStringLiteral("provider_doi_mismatch"):
                            (failure.isEmpty()?error:failure+QStringLiteral(";")+error));
                    } else finish(cr, failure.isEmpty()?error:failure);
                    return;
                }
                if(cr.isEmpty()) {finish(secondary,{});return;}
                const QString a=cr.value(QStringLiteral("title")).toString();
                const QString b=secondary.value(QStringLiteral("title")).toString();
                if(!Metadata::compatibleTitle(a,b)) {
                    cr["metadata_status"]=QStringLiteral("needs_review");
                    cr["metadata_diagnostic"]=QStringLiteral("source_title_conflict");
                    finish(cr,QStringLiteral("source_title_conflict"));return;
                }
                // Independent providers sometimes disagree on year, issue,
                // volume or pages (online-first versus final print). Preserve
                // neither uncertain addition until explicit user review.
                for(const QString& key : {QStringLiteral("year"),QStringLiteral("volume"),
                     QStringLiteral("issue"),QStringLiteral("page")}) {
                    const QString prior=cr.value(key).toString().simplified();
                    const QString alternate=secondary.value(key).toString().simplified();
                    if(!prior.isEmpty() && !alternate.isEmpty() && prior!=alternate) {
                        cr["metadata_status"]=QStringLiteral("needs_review");
                        cr["metadata_diagnostic"]=QStringLiteral("source_field_conflict:")+key;
                        finish(cr,QStringLiteral("source_field_conflict"));return;
                    }
                }
                for(const QString& key : {QStringLiteral("title"),QStringLiteral("journal"),
                     QStringLiteral("year"),QStringLiteral("volume"),QStringLiteral("issue"),
                     QStringLiteral("page"),QStringLiteral("authors_structured")}) {
                    if(cr.value(key).toString().isEmpty() &&
                       (key!=QStringLiteral("authors_structured") ||
                        cr.value(key).toStringList().isEmpty()) &&
                       !secondary.value(key).isNull()) {
                        cr[key]=secondary.value(key);
                        QVariantMap sources=cr.value(QStringLiteral("field_sources")).toMap();
                        sources[key]=QStringLiteral("OpenAlex");cr["field_sources"]=sources;
                    }
                }
                cr["metadata_source"]=QStringLiteral("Crossref + OpenAlex");
                finish(cr, failure);
            });
        });
    }
private:
    void fetch(const QUrl& url, const QString& provider,
               std::function<void(QJsonObject,QString)> done) {
        if(url.scheme()!=QStringLiteral("https") ||
           !(url.host()==QStringLiteral("api.crossref.org") ||
             url.host()==QStringLiteral("api.openalex.org"))) {
            done({},QStringLiteral("blocked_host"));return;
        }
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        if(backoff_.value(provider,0)>now) {done({},QStringLiteral("rate_limited"));return;}
        QNetworkRequest request(url);
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("User-Agent","LUNA-Library/0.9 (DOI bibliographic lookup)");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(8500);
        auto* reply=network_.get(request);
        connect(reply,&QNetworkReply::finished,this,[this,reply,provider,done] {
            const auto cleanup=[reply] {reply->deleteLater();};
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(status==429 || status==503) {
                const int seconds=qBound(60,reply->rawHeader("Retry-After").toInt(),3600);
                backoff_[provider]=QDateTime::currentMSecsSinceEpoch()+qint64(seconds)*1000;
            }
            if(reply->error()!=QNetworkReply::NoError || status<200 || status>=300) {
                const QString code=status==429?QStringLiteral("rate_limited"):
                    status==404?QStringLiteral("not_found"):QStringLiteral("provider_unavailable");
                cleanup();done({},code);return;
            }
            const QByteArray bytes=reply->read(1048577);
            cleanup();
            if(bytes.size()>1048576) {done({},QStringLiteral("response_too_large"));return;}
            QJsonParseError parse;
            const QJsonDocument json=QJsonDocument::fromJson(bytes,&parse);
            if(parse.error!=QJsonParseError::NoError || !json.isObject()) {
                done({},QStringLiteral("invalid_json"));return;
            }
            done(json.object(),{});
        });
    }
    QNetworkAccessManager network_;
    QSet<QString> busy_;
    QHash<QString,qint64> backoff_;
};
}
