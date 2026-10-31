// SPDX-License-Identifier: MIT
#pragma once
// Independent Qt implementation of DOI evidence, source validation and
// chemistry citation formatting. No private Synthesis Recall source is bundled.
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVariantMap>
#include <QDateTime>
#include <QUrl>
#include <QStringList>
#include <QSet>
#include <algorithm>

namespace luna {
namespace Metadata {

inline QString normalizeDoi(QString raw) {
    raw = raw.normalized(QString::NormalizationForm_KC).trimmed();
    raw.remove(QRegularExpression(QStringLiteral(R"(^(?:https?://)?(?:dx\.)?doi\.org/)"),
                                  QRegularExpression::CaseInsensitiveOption));
    raw.remove(QRegularExpression(QStringLiteral(R"(^doi\s*:\s*)"),
                                  QRegularExpression::CaseInsensitiveOption));
    raw.remove(QRegularExpression(QStringLiteral(R"(\s+)")));
    raw.remove(QRegularExpression(QStringLiteral(R"([.,;:!?\]\}>"'“”’]+$)")));
    while (raw.endsWith(QLatin1Char(')')) && raw.count(QLatin1Char(')')) > raw.count(QLatin1Char('(')))
        raw.chop(1);
    static const QRegularExpression valid(QStringLiteral(R"(^10\.\d{4,9}/[-._;()/:A-Z0-9]+$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    return raw.size() <= 220 && valid.match(raw).hasMatch() ? raw.toLower() : QString();
}
struct Candidate {
    QString doi;
    QString location;
    int score = 0;
    bool explicitMarker = false;
};
inline QList<Candidate> collectCandidates(const QStringList& pages, QString metadata = {}, QString filename = {}) {
    QList<Candidate> found;
    auto append = [&found](const QString& content, const QString& location, int base) {
        const QString source = content.left(50000).normalized(QString::NormalizationForm_KC);
        static const QRegularExpression pattern(QStringLiteral(
            R"(10\s*\.\s*\d{4,9}\s*/\s*[^\s<>\[\]{}"“”'‘’]+)"),
            QRegularExpression::CaseInsensitiveOption);
        auto match = pattern.globalMatch(source);
        while (match.hasNext()) {
            const auto m = match.next();
            const QString doi = normalizeDoi(m.captured());
            if (doi.isEmpty()) continue;
            const QString before = source.mid(qMax(0, m.capturedStart() - 58),
                                              qMin(m.capturedStart(), 58));
            const bool explicitMarker = QRegularExpression(QStringLiteral(
                R"((?:doi\s*:?\s*|https?://\s*(?:dx\.)?doi\.org/\s*)$)"),
                QRegularExpression::CaseInsensitiveOption).match(before).hasMatch();
            const QString prior = source.left(m.capturedStart());
            const bool references = QRegularExpression(QStringLiteral(
                R"(\b(?:references|bibliography|literature cited)\b)"),
                QRegularExpression::CaseInsensitiveOption).match(prior).hasMatch();
            const int score = base + (explicitMarker ? 35 : 0) - (references ? 75 : 0);
            auto existing = std::find_if(found.begin(), found.end(),
                [&doi](const Candidate& c) { return c.doi == doi; });
            if (existing == found.end()) found.append({doi, location, score, explicitMarker});
            else if (score > existing->score) *existing = {doi, location, score, explicitMarker};
        }
    };
    append(metadata, QStringLiteral("metadata"), 120);
    for (int i = 0; i < pages.size() && i < 3; ++i)
        append(pages[i], QStringLiteral("page%1").arg(i+1), i == 0 ? 70 : i == 1 ? 40 : 20);
    if (found.isEmpty()) append(filename, QStringLiteral("filename"), 5);
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.doi < b.doi;
    });
    if (found.size() > 8) found = found.mid(0,8);
    return found;
}
inline bool acceptableIdentity(const QList<Candidate>& candidates) {
    if (candidates.isEmpty()) return false;
    // A DOI in a reference or filename can never be promoted to the paper's
    // identity. Conflicting high-confidence identities require review.
    if (candidates[0].score < 70 || candidates[0].location == QStringLiteral("filename")) return false;
    for (int i=1;i<candidates.size();++i)
        if (candidates[i].doi != candidates[0].doi && candidates[i].score >= candidates[0].score-15)
            return false;
    return true;
}
inline QString normalizedTitle(QString title) {
    title.remove(QRegularExpression(QStringLiteral(R"(<[^>]*>)")));
    title = title.normalized(QString::NormalizationForm_KC).toCaseFolded();
    title.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return title.left(350);
}
inline bool compatibleTitle(const QString& pdfTitle, const QString& apiTitle) {
    if (pdfTitle.simplified().isEmpty() || apiTitle.simplified().isEmpty()) return true;
    const QString a = normalizedTitle(pdfTitle), b = normalizedTitle(apiTitle);
    if (a.size() < 18 || b.size() < 18) return true; // insufficient PDF evidence
    if (a == b || a.contains(b) || b.contains(a)) return true;
    // Do not infer title similarity from trivial substrings: reject clearly
    // unrelated titles while allowing publisher punctuation differences.
    const qsizetype n = qMin(a.size(), b.size());
    qsizetype common = 0;
    for (qsizetype i=0;i<n && a.at(i)==b.at(i);++i) ++common;
    return common >= 14 && (common * 100 / qMax(a.size(),b.size()) >= 45);
}
inline QString officialAbbreviation(const QString& journal, const QString& verifiedShort = {}) {
    if (!verifiedShort.simplified().isEmpty()) return verifiedShort.simplified();
    // Fixed, reviewed journal aliases only. Unknown journals are NOT guessed.
    static const QHash<QString,QString> known = {
        {QStringLiteral("journal of the american chemical society"),QStringLiteral("J. Am. Chem. Soc.")},
        {QStringLiteral("j am chem soc"),QStringLiteral("J. Am. Chem. Soc.")},
        {QStringLiteral("nature communications"),QStringLiteral("Nat. Commun.")},
        {QStringLiteral("nature chemistry"),QStringLiteral("Nat. Chem.")},
        {QStringLiteral("nature catalysis"),QStringLiteral("Nat. Catal.")},
        {QStringLiteral("nature synthesis"),QStringLiteral("Nat. Synth.")},
        {QStringLiteral("nature"),QStringLiteral("Nature")},
        {QStringLiteral("science"),QStringLiteral("Science")},
        {QStringLiteral("organic letters"),QStringLiteral("Org. Lett.")},
        {QStringLiteral("journal of organic chemistry"),QStringLiteral("J. Org. Chem.")},
        {QStringLiteral("the journal of organic chemistry"),QStringLiteral("J. Org. Chem.")},
        {QStringLiteral("acs catalysis"),QStringLiteral("ACS Catal.")},
        {QStringLiteral("chemical science"),QStringLiteral("Chem. Sci.")},
        {QStringLiteral("chemical communications"),QStringLiteral("Chem. Commun.")},
        {QStringLiteral("organic & biomolecular chemistry"),QStringLiteral("Org. Biomol. Chem.")},
        {QStringLiteral("angewandte chemie international edition"),QStringLiteral("Angew. Chem. Int. Ed.")},
        {QStringLiteral("advanced synthesis & catalysis"),QStringLiteral("Adv. Synth. Catal.")},
        {QStringLiteral("journal of medicinal chemistry"),QStringLiteral("J. Med. Chem.")},
        {QStringLiteral("chemistry – a european journal"),QStringLiteral("Chem. Eur. J.")},
        {QStringLiteral("chemistry - a european journal"),QStringLiteral("Chem. Eur. J.")},
        {QStringLiteral("chem"),QStringLiteral("Chem")}
    };
    QString key = journal.simplified().toLower();
    key.remove(QLatin1Char('.'));
    return known.value(key, journal.simplified());
}
inline QString normalizePages(QString pages) {
    pages = pages.simplified();
    pages.replace(QRegularExpression(QStringLiteral(R"(\s*[-–—]\s*)")), QStringLiteral("–"));
    return pages;
}
inline bool articleNumberValid(const QString& value, const QString& doi) {
    const QString n=value.simplified().toLower(), d=normalizeDoi(doi);
    return !n.isEmpty() && n != d && n != d.section(QLatin1Char('/'),1);
}
inline QString labCitation(const QVariantMap& paper) {
    const QString manual = paper.value(QStringLiteral("citation")).toString().simplified();
    if (!manual.isEmpty()) return manual;
    const QString authorOverride=paper.value(QStringLiteral("senior_author_override")).toString().simplified();
    const QStringList names=paper.value(QStringLiteral("authors_structured")).toStringList();
    const int chosen=paper.value(QStringLiteral("senior_author_index"), -1).toInt();
    QString author=authorOverride;
    if (author.isEmpty() && chosen>=0 && chosen<names.size()) author=names[chosen];
    if (author.isEmpty() && !names.isEmpty()) author=names.last();
    if (author.isEmpty()) author=paper.value(QStringLiteral("authors")).toString().section(
        QLatin1Char(','),-1).trimmed();
    const QString journal=officialAbbreviation(paper.value(QStringLiteral("journal")).toString(),
        paper.value(QStringLiteral("journal_abbreviation")).toString());
    const QString year=paper.value(QStringLiteral("year")).toString();
    const QString volume=paper.value(QStringLiteral("volume")).toString();
    QString pages=paper.value(QStringLiteral("page")).toString();
    if (pages.isEmpty() && articleNumberValid(paper.value(QStringLiteral("article_number")).toString(),
                                               paper.value(QStringLiteral("doi")).toString()))
        pages=paper.value(QStringLiteral("article_number")).toString();
    QStringList publication;
    if (!journal.isEmpty()) publication<<journal;
    if (!year.isEmpty()) {
        if (publication.isEmpty()) publication<<year;
        else publication.last() += QLatin1Char(' ') + year;
    }
    if (!volume.isEmpty()) publication<<volume;
    if (!pages.isEmpty()) publication<<normalizePages(pages);
    QStringList parts;
    if (!author.isEmpty()) parts<<author;
    if (!publication.isEmpty()) parts<<publication.join(QStringLiteral(", "));
    QString result=parts.join(QStringLiteral(", "));
    while(result.endsWith(QLatin1Char('.')))result.chop(1);
    return result.isEmpty() ? result : result + QLatin1Char('.');
}
inline QString yearFromCrossref(const QJsonObject& obj) {
    for (const auto& key : {"published","issued","published-print","published-online"}) {
        const auto parts = obj.value(QLatin1String(key)).toObject()
            .value(QStringLiteral("date-parts")).toArray();
        if (!parts.isEmpty()) {
            const int y=parts.first().toArray().value(0).toInt();
            if (y>=1600 && y<=2200) return QString::number(y);
        }
    }
    return {};
}
inline QVariantMap fromCrossref(const QJsonObject& obj, const QString& expectedDoi) {
    if (normalizeDoi(obj.value(QStringLiteral("DOI")).toString())!=normalizeDoi(expectedDoi)) return {};
    QVariantMap m; m["doi"]=normalizeDoi(expectedDoi);m["metadata_source"]="Crossref";
    const auto first=[](const QJsonValue& v) {return v.toArray().isEmpty()?QString():v.toArray().first().toString().simplified();};
    m["title"]=first(obj.value(QStringLiteral("title")));
    m["journal"]=first(obj.value(QStringLiteral("container-title")));
    m["journal_abbreviation"]=first(obj.value(QStringLiteral("short-container-title")));
    m["year"]=yearFromCrossref(obj);
    m["volume"]=obj.value(QStringLiteral("volume")).toString();
    m["issue"]=obj.value(QStringLiteral("issue")).toString();
    m["page"]=normalizePages(obj.value(QStringLiteral("page")).toString());
    const QString number=obj.value(QStringLiteral("article-number")).toString();
    if(articleNumberValid(number, expectedDoi))m["article_number"]=number;
    QStringList names;
    for (const auto& a : obj.value(QStringLiteral("author")).toArray()) {
        const auto o=a.toObject();
        const QString name=(o.value(QStringLiteral("given")).toString()+QLatin1Char(' ')+
            o.value(QStringLiteral("family")).toString()).simplified();
        if(!name.isEmpty())names<<name;
    }
    m["authors_structured"]=names;
    if(!names.isEmpty())m["authors"]=names.join(QStringLiteral(", "));
    return m;
}
inline QVariantMap fromOpenAlex(const QJsonObject& obj, const QString& expectedDoi) {
    if (normalizeDoi(obj.value(QStringLiteral("doi")).toString())!=normalizeDoi(expectedDoi)) return {};
    QVariantMap m; m["doi"]=normalizeDoi(expectedDoi);m["metadata_source"]="OpenAlex";
    m["title"]=obj.value(QStringLiteral("title")).toString().simplified();
    m["journal"]=obj.value(QStringLiteral("primary_location")).toObject()
        .value(QStringLiteral("source")).toObject().value(QStringLiteral("display_name")).toString();
    const int year=obj.value(QStringLiteral("publication_year")).toInt();
    if(year>=1600&&year<=2200)m["year"]=QString::number(year);
    const auto b=obj.value(QStringLiteral("biblio")).toObject();
    m["volume"]=b.value(QStringLiteral("volume")).toString();
    m["issue"]=b.value(QStringLiteral("issue")).toString();
    const QString first=b.value(QStringLiteral("first_page")).toString();
    const QString last=b.value(QStringLiteral("last_page")).toString();
    m["page"]=normalizePages(first.isEmpty()?QString():first + (last.isEmpty()||last==first?QString():QStringLiteral("–")+last));
    QStringList names;
    for(const auto& a:obj.value(QStringLiteral("authorships")).toArray()){
        const QString name=a.toObject().value(QStringLiteral("author")).toObject()
            .value(QStringLiteral("display_name")).toString().simplified();
        if(!name.isEmpty())names<<name;
    }
    m["authors_structured"]=names;
    if(!names.isEmpty())m["authors"]=names.join(QStringLiteral(", "));
    return m;
}
inline QString publicationStatus(const QVariantMap& m) {
    if(m.value(QStringLiteral("metadata_status")).toString()==QStringLiteral("needs_review"))
        return QStringLiteral("needs_review");
    const bool essentials = !m.value(QStringLiteral("doi")).toString().isEmpty() &&
        !m.value(QStringLiteral("title")).toString().isEmpty() &&
        !m.value(QStringLiteral("authors_structured")).toStringList().isEmpty() &&
        !m.value(QStringLiteral("journal")).toString().isEmpty() &&
        !m.value(QStringLiteral("year")).toString().isEmpty();
    const bool citationComplete=essentials && !m.value(QStringLiteral("volume")).toString().isEmpty() &&
        (!m.value(QStringLiteral("page")).toString().isEmpty() ||
         !m.value(QStringLiteral("article_number")).toString().isEmpty());
    return citationComplete?QStringLiteral("complete"):QStringLiteral("partial");
}
}
}
