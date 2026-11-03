// SPDX-License-Identifier: MIT
#pragma once
// Read-only, bounded publisher first-page cues adapted from the isolated
// Bibliography Accuracy Lab (PR #18) and SI Finder risk cases (PR #17).
// No network, local PDF data persistence, or user-document uploads.
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

namespace luna::FrontMatter {

inline QStringList lines(const QString& text, int limit = 16000) {
    const auto raw = text.left(limit).split(
        QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
    QStringList result;
    for (const QString& line : raw) {
        const QString clean = line.simplified();
        if (!clean.isEmpty()) result.append(clean);
        if (result.size() >= 175) break;
    }
    return result;
}

// A generic link "Supporting Information" in a main-paper navigation bar
// is NOT an SI cover. Require a standalone early cover heading, and reject
// a publisher article masthead/citation. No substring-only classifications.
inline bool isSupplementCover(const QString& firstPage) {
    const QStringList rows = lines(firstPage, 12000);
    static const QRegularExpression cover(QStringLiteral(
        R"(^(?:(?:Revised|Electronic)\s+)?(?:Supporting|Supplementary)\s+Information(?:\s*\((?:SI|ESI)\))?(?:\s+for(?:\s+.+)?)?\.?$|^Electronic\s+Supplementary\s+Material(?:\s*\(ESI\))?(?:\s+for(?:\s+.+)?)?\.?$)"),
        QRegularExpression::CaseInsensitiveOption);
    // The first visible heading is the best discriminator for actual SI.
    if (!rows.isEmpty() && cover.match(rows.first()).hasMatch()) return true;
    bool mainArticle = false;
    for (qsizetype i=0; i<rows.size() && i<15; ++i) {
        const QString& row = rows[i];
        if (row.startsWith(QStringLiteral("Cite This:"), Qt::CaseInsensitive) ||
            row.startsWith(QStringLiteral("Cite this article"), Qt::CaseInsensitive) ||
            row.compare(QStringLiteral("Article Recommendations"), Qt::CaseInsensitive)==0 ||
            row.startsWith(QStringLiteral("journal homepage:"), Qt::CaseInsensitive))
            mainArticle = true;
    }
    if (mainArticle) return false;
    // A real cover may have a short publisher banner before the SI heading.
    for (qsizetype i=1; i<rows.size() && i<7; ++i) {
        if (cover.match(rows[i]).hasMatch()) return true;
        if (rows[i].compare(QStringLiteral("Abstract"),Qt::CaseInsensitive)==0 ||
            rows[i].compare(QStringLiteral("Introduction"),Qt::CaseInsensitive)==0) break;
    }
    return false;
}

inline bool isPrinterJobTitle(const QString& title) {
    const QString text = title.simplified();
    static const QRegularExpression job(QStringLiteral(
        R"(^(?:[A-Za-z]{1,18}\.)?[A-Za-z]{1,20}\d[A-Za-z0-9.]*\s+\d{1,7}\s*\.{2,}\s*\d{1,7}\s*(?:\+\+)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression spool(QStringLiteral(
        R"(^[A-Za-z0-9_.-]{7,70}\s+\d{1,7}\s*\.{2,}\s*\d{1,7}\s*(?:\+\+)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return job.match(text).hasMatch() || spool.match(text).hasMatch();
}

inline QString normalizePrintedTitle(QString title) {
    title = title.simplified();
    title.replace(QRegularExpression(QStringLiteral(R"(-\s+(?=\p{L}))")),
                  QStringLiteral("-"));
    title.remove(QRegularExpression(QStringLiteral(R"([†‡☆\s]+$)")));
    return title;
}

// Strict publisher-only fallback for an ACS *article*, not SI or publisher
// menu content. A printed title MUST end at an author-like row, never at an
// arbitrary footer, reference, or DOI. Abstain if the cover is ambiguous.
inline QString acsTitle(const QString& text) {
    if (isSupplementCover(text)) return {};
    const QStringList rows = lines(text, 12000);
    qsizetype masthead=-1, citation=-1;
    for(qsizetype i=0; i<rows.size() && i<24; ++i) {
        const QString& row = rows[i];
        if (i<8 && row.startsWith(QStringLiteral("pubs.acs.org/"),Qt::CaseInsensitive))
            masthead=i;
        if (row.startsWith(QStringLiteral("Cite This:"),Qt::CaseInsensitive))
            citation=i;
    }
    if (masthead<0 || citation<masthead+3 || citation>masthead+15) return {};
    static const QRegularExpression authorList(QStringLiteral(
        R"(^[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}\s*,)"));
    QStringList words;
    bool foundAuthors=false;
    for(qsizetype i=masthead+1; i<citation; ++i) {
        const QString row=rows[i];
        if(row.compare(QStringLiteral("Article"),Qt::CaseInsensitive)==0 ||
           row.compare(QStringLiteral("Letter"),Qt::CaseInsensitive)==0 ||
           row.compare(QStringLiteral("Communication"),Qt::CaseInsensitive)==0 ||
           row.compare(QStringLiteral("Research Article"),Qt::CaseInsensitive)==0)
            continue;
        if(authorList.match(row).hasMatch()) {foundAuthors=true;break;}
        if(row.size()<4 || row.size()>180 || words.size()>=4 ||
           row.contains(QStringLiteral("Downloaded from"),Qt::CaseInsensitive))
            return {};
        words.append(row);
    }
    return foundAuthors ? normalizePrintedTitle(words.join(QLatin1Char(' '))) : QString();
}

// Restrict to known RSC article covers. Split two-column reading order is
// noisy; don't commit a title without the author boundary and printed DOI.
inline QString rscTitle(const QString& text) {
    if (isSupplementCover(text)) return {};
    const QStringList rows=lines(text, 12000);
    if (rows.size()<6 || !text.left(12000).contains(QStringLiteral("10.1039/"),Qt::CaseInsensitive))
        return {};
    const bool chemcomm=rows.first().compare(QStringLiteral("ChemComm"),Qt::CaseInsensitive)==0;
    const bool obc=rows.first().compare(QStringLiteral("Organic &"),Qt::CaseInsensitive)==0 &&
        rows.value(1).compare(QStringLiteral("Biomolecular Chemistry"),Qt::CaseInsensitive)==0;
    if (!chemcomm && !obc) return {};
    qsizetype kind=-1;
    for(qsizetype i=0; i<rows.size() && i<8; ++i)
        if(rows[i].compare(QStringLiteral("PAPER"),Qt::CaseInsensitive)==0 ||
           rows[i].compare(QStringLiteral("COMMUNICATION"),Qt::CaseInsensitive)==0) {
            kind=i;break;
        }
    if(kind<0) return {};
    static const QRegularExpression author(QStringLiteral(
        R"(^[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}\s*,)"));
    static const QRegularExpression volume(QStringLiteral(R"(^\d{1,4}\s*,\s*\d{1,7}$)"));
    QStringList titleRows;
    bool sawAuthor=false;
    for(qsizetype i=kind+1; i<rows.size() && i<24; ++i) {
        const QString row=rows[i];
        if(author.match(row).hasMatch()) {sawAuthor=true;break;}
        if(row.startsWith(QStringLiteral("Received"),Qt::CaseInsensitive) ||
           row.startsWith(QStringLiteral("Accepted"),Qt::CaseInsensitive) ||
           row.startsWith(QStringLiteral("Cite this:"),Qt::CaseInsensitive) ||
           row.startsWith(QStringLiteral("Licensed under"),Qt::CaseInsensitive) ||
           row.compare(QStringLiteral("Licence and permissions"),Qt::CaseInsensitive)==0 ||
           volume.match(row).hasMatch())
            continue;
        if(row.startsWith(QStringLiteral("DOI:"),Qt::CaseInsensitive)) return {};
        if(row.size()<4 || row.size()>180 || titleRows.size()>=4) return {};
        titleRows.append(row);
    }
    return (sawAuthor && !titleRows.isEmpty())
        ? normalizePrintedTitle(titleRows.join(QLatin1Char(' '))) : QString();
}

// Prefer exact publisher footers to incomplete first-page citations, but only
// when the same publisher journal masthead is present on this document's cover.
// This prevents reference-list "journal/year/page" patterns being imported.
inline QVariantMap publisherCitation(const QString& text) {
    if (isSupplementCover(text)) return {};
    const QStringList rows=lines(text, 32000);
    const QString front=rows.mid(0,15).join(QLatin1Char('\n'));
    const bool rsc=front.contains(QStringLiteral("10.1039/"),Qt::CaseInsensitive) ||
        front.contains(QStringLiteral("ChemComm"),Qt::CaseInsensitive) ||
        front.contains(QStringLiteral("Organic & Biomolecular Chemistry"),Qt::CaseInsensitive);
    const bool nature=front.contains(QStringLiteral("Nature Chemistry"),Qt::CaseInsensitive) ||
        front.contains(QStringLiteral("Nature Synthesis"),Qt::CaseInsensitive) ||
        front.contains(QStringLiteral("Nature Communications"),Qt::CaseInsensitive) ||
        // Nature Communications sometimes prints only "Article" above the
        // heading; the publisher DOI prefix is present instead.
        front.contains(QStringLiteral("10.1038/s41467-"),Qt::CaseInsensitive);
    static const QRegularExpression cc(QStringLiteral(
        R"(^\d{1,7}\s*\|\s*Chem\.\s*Commun\.,\s*(?<year>(?:19|20)\d{2}),\s*(?<volume>\d{1,4}),\s*(?<page>\d{1,7}\s*[-–]\s*\d{1,7})$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression obc(QStringLiteral(
        R"(^Org\.\s*Biomol\.\s*Chem\.,\s*(?<year>(?:19|20)\d{2}),\s*(?<volume>\d{1,4}),\s*(?<page>\d{1,7}\s*[-–]\s*\d{1,7})\s*\|\s*\d{1,7}$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nc(QStringLiteral(
        R"(^Nature Communications\s*\|\s*\((?<year>(?:19|20)\d{2})\)\s*(?<volume>\d{1,4}):(?<article>\d{1,7})$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ns(QStringLiteral(
        R"(^(?<journal>Nature Chemistry|Nature Synthesis)\s*\|\s*Volume\s+(?<volume>\d{1,4})\s*\|\s*[A-Za-z]+\s+(?<year>(?:19|20)\d{2})\s*\|\s*(?<page>\d{1,7}\s*[-–]\s*\d{1,7})$)"),
        QRegularExpression::CaseInsensitiveOption);
    for(qsizetype i=0;i<rows.size() && i<160;++i) {
        const QString& row=rows[i];
        if(rsc) {
            const auto a=cc.match(row);
            if(a.hasMatch()) {
                QString page=a.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-')).remove(QLatin1Char(' '));
                return {{"journal",QStringLiteral("Chemical Communications")},
                        {"year",a.captured(QStringLiteral("year"))},{"volume",a.captured(QStringLiteral("volume"))},{"page",page}};
            }
            const auto b=obc.match(row);
            if(b.hasMatch()) {
                QString page=b.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-')).remove(QLatin1Char(' '));
                return {{"journal",QStringLiteral("Organic & Biomolecular Chemistry")},
                        {"year",b.captured(QStringLiteral("year"))},{"volume",b.captured(QStringLiteral("volume"))},{"page",page}};
            }
        }
        if(nature) {
            const auto a=nc.match(row);
            if(a.hasMatch()) return {{"journal",QStringLiteral("Nature Communications")},
                {"year",a.captured(QStringLiteral("year"))},{"volume",a.captured(QStringLiteral("volume"))},
                {"article_number",a.captured(QStringLiteral("article"))}};
            const auto b=ns.match(row);
            if(b.hasMatch()) {
                QString page=b.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-')).remove(QLatin1Char(' '));
                return {{"journal",b.captured(QStringLiteral("journal"))},{"year",b.captured(QStringLiteral("year"))},
                        {"volume",b.captured(QStringLiteral("volume"))},{"page",page}};
            }
        }
    }
    return {};
}

} // namespace luna::FrontMatter
