// SPDX-License-Identifier: MIT
#pragma once
// Conservative, OFFLINE first-page SI -> parent-article evidence. File names are
// hints only: neither a filename nor an SI-specific DOI is proof of identity.
#include "lunafrontmatter.h"
#include "lunametadataengine.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>

namespace luna::SIIdentity {

inline bool sameArticleTitle(const QString& first, const QString& second) {
    const QString a=Metadata::normalizedTitle(first);
    const QString b=Metadata::normalizedTitle(second);
    if (a.size()<24 || b.size()<24) return false;
    if (a==b) return true;
    // Hyphenation, typesetting and short subtitles can differ. Never use the
    // general article matcher: it considers missing/short titles compatible.
    return (a.contains(b) || b.contains(a)) &&
           qMin(a.size(),b.size())*100/qMax(a.size(),b.size()) >= 86;
}

inline bool isArticleTitle(const QString& value) {
    const QString title=value.simplified();
    if (title.size()<25 || title.size()>230 ||
        title.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()<4) return false;
    static const QRegularExpression noise(QStringLiteral(
        R"(^(?:contents|table of contents|supplementary information|supporting information|general methods|materials and methods|references|appendix|copyright|article recommendations|corresponding author|figure s\d+|table s\d+|scheme s\d+)\b|\b(?:university|institute|department)\b|(?:@|https?://|doi\.org))"),
        QRegularExpression::CaseInsensitiveOption);
    return !noise.match(title).hasMatch();
}

inline QString coverTitle(const QString& text) {
    if (!FrontMatter::isSupplementCover(text)) return {};
    const auto lines=FrontMatter::lines(text,10000);
    static const QRegularExpression heading(QStringLiteral(
        R"(^(?:(?:Revised|Electronic)\s+)?(?:Supporting|Supplementary)\s+Information(?:\s*\((?:SI|ESI)\))?(?:\s+for(?:\s+.+)?)?\.?$|^Electronic\s+Supplementary\s+Material(?:\s*\(ESI\))?(?:\s+for(?:\s+.+)?)?\.?$)"),
        QRegularExpression::CaseInsensitiveOption);
    int headingIndex=-1;
    for(int i=0;i<lines.size() && i<7;++i)
        if(heading.match(lines[i]).hasMatch()){headingIndex=i;break;}
    if(headingIndex<0) return {};
    QStringList titleLines;
    QString best;
    // Only the cover's first short block, never a body heading, reference,
    // experimental procedure or caption. Ambiguity stays unverified.
    for(int i=headingIndex+1;i<lines.size() && i<=headingIndex+5;++i) {
        const QString line=lines[i].simplified();
        if(line.isEmpty()) continue;
        if(line.contains(QLatin1Char('@')) ||
           line.startsWith(QStringLiteral("DOI"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Contents"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Authors"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Correspondence"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Department"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Institute"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("University"),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Table "),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("Figure "),Qt::CaseInsensitive) ||
           line.startsWith(QStringLiteral("General "),Qt::CaseInsensitive)) break;
        // A title can span two or three PDF4QT text rows. Keep the longest
        // plausible title, stopping *before* a recognizable author block.
        // Return the latest clean candidate, not the first short line.
        static const QRegularExpression authorList(QStringLiteral(
            R"(^[\p{Lu}][\p{L}.'’\-]+(?:\s+[\p{Lu}][\p{L}.'’\-]+){1,2}(?:,\s*| and )(?:[\p{Lu}][\p{L}.'’\-]+(?:\s+[\p{Lu}][\p{L}.'’\-]+){1,2}))+)"));
        if(!titleLines.isEmpty() && authorList.match(line).hasMatch()) break;
        if(line.size()>135 || line.contains(QLatin1Char('@'))) break;
        titleLines.append(line);
        const QString proposed=titleLines.join(QLatin1Char(' '));
        if(isArticleTitle(proposed)) best=proposed;
        if(titleLines.size()>=3) break;
    }
    return best;
}

inline QString filenameDoiHint(const QString& fileName) {
    const QString name=QFileInfo(fileName).fileName();
    static const QRegularExpression acs(QStringLiteral(
        R"(^(jo|jm|ol)(\d{1,2}[a-z]\d{5,6})_si_\d+\.pdf$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto a=acs.match(name);
    if(a.hasMatch()) {
        const QString key=a.captured(1).toLower();
        const QString journal=key==QStringLiteral("jo") ? QStringLiteral("acs.joc.") :
             key==QStringLiteral("jm") ? QStringLiteral("acs.jmedchem.") : QStringLiteral("acs.orglett.");
        return Metadata::normalizeDoi(QStringLiteral("10.1021/")+journal+a.captured(2));
    }
    static const QRegularExpression rsc(QStringLiteral(
        R"(^(d\d[a-z]{2}\d{5}[a-z])\d?_suppl\.pdf$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto r=rsc.match(name);
    return r.hasMatch() ? Metadata::normalizeDoi(QStringLiteral("10.1039/")+r.captured(1)) : QString();
}

inline QVariantMap evidence(const QString& firstPage, const QString& fileName={}) {
    if(!FrontMatter::isSupplementCover(firstPage)) return {};
    const QString printed=coverTitle(firstPage);
    // Only explicit DOI on SI front matter is a printed DOI lead; references
    // or a DOI hidden in PDF properties are not parent-article identity.
    const auto candidates=Metadata::collectCandidates({firstPage.left(11000)});
    QString doi, provenance;
    if(candidates.size()==1 && candidates.first().explicitMarker &&
       candidates.first().score>=105) {
        doi=candidates.first().doi;
        provenance=QStringLiteral("si_cover_explicit_doi");
    }
    if(doi.isEmpty() && !fileName.isEmpty()) {
        doi=filenameDoiHint(fileName);
        if(!doi.isEmpty())provenance=QStringLiteral("filename_hint_unverified");
    }
    QVariantMap result{
        {"documentKind",QStringLiteral("supporting_information")},
        {"siLinkStatus",QStringLiteral("needs_review")},
        {"doiStatus",QStringLiteral("needs_review")}};
    if(!printed.isEmpty())result["siCoverTitle"]=printed;
    if(!doi.isEmpty()) {
        result["siArticleDoiCandidate"]=doi;
        result["siEvidenceSource"]=provenance;
    }
    return result;
}

} // namespace luna::SIIdentity
