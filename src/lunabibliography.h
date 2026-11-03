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
#include "lunametadataengine.h"
#include <functional>

namespace luna {
// Local QSettings bibliography. Remote provider requests (in a separate Qt worker)
// contain only validated DOI identifiers. PDFs and annotations never leave disk.
class Bibliography final : public QObject {
public:
    explicit Bibliography(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void(const QString&)> changed;

    // A pasted full citation takes priority over structured fields.
    static QString citation(const QVariantMap& entry) {
        const QString pasted = entry.value("citation").toString().simplified();
        if (!pasted.isEmpty()) return pasted;
        if (!entry.value("doi").toString().isEmpty())
            return Metadata::labCitation(entry);
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

    // A SI cover is not the same as an article's Supporting Information link.
    // ACS article navigation includes that standalone phrase in the first
    // 16 y-sorted text rows; using "contains" there corrupts main-paper cards.
    static bool isSupportingInformationCover(const QString& firstPageText) {
        const auto lines = firstPageText.left(12000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
        // These are article navigation / publisher citation cues, not SI
        // headers. Search the same front-matter window before interpreting
        // a generic "Supporting Information" hyperlink as a cover.
        for (qsizetype i = 0; i < lines.size() && i < 24; ++i) {
            const QString item = lines[i].simplified();
            if (item.startsWith(QStringLiteral("Cite This:"), Qt::CaseInsensitive) ||
                item.compare(QStringLiteral("Article Recommendations"), Qt::CaseInsensitive) == 0 ||
                item.startsWith(QStringLiteral("pubs.acs.org/"), Qt::CaseInsensitive))
                return false;
        }
        static const QRegularExpression cover(QStringLiteral(
            R"(^(?:(?:Revised|Electronic)\s+)?(?:Supporting|Supplementary)\s+Information(?:\s*\((?:SI|ESI)\))?(?:\s+for(?:\s+.+)?)?\.?$)"),
            QRegularExpression::CaseInsensitiveOption);
        for (qsizetype i = 0; i < lines.size() && i < 24; ++i)
            if (cover.match(lines[i].simplified()).hasMatch()) return true;
        return false;
    }

    // A narrow offline fallback for an ACS *main* first page. The document
    // must have the pubs.acs.org masthead, a Cite This publication line, and
    // an author-like boundary after the printed title. Other publisher pages,
    // SI covers, and ambiguous layouts deliberately abstain. This does NOT
    // replace DOI-matched provider metadata.
    static QString firstPageAcsTitle(const QString& firstPageText) {
        if (isSupportingInformationCover(firstPageText)) return {};
        const auto lines = firstPageText.left(12000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
        qsizetype masthead = -1, citation = -1;
        for (qsizetype i = 0; i < lines.size() && i < 24; ++i) {
            const QString line = lines[i].simplified();
            if (i < 8 && line.startsWith(QStringLiteral("pubs.acs.org/"),
                                          Qt::CaseInsensitive)) masthead = i;
            if (line.startsWith(QStringLiteral("Cite This:"), Qt::CaseInsensitive))
                citation = i;
        }
        if (masthead < 0 || citation < masthead + 3 || citation > masthead + 15)
            return {};
        static const QRegularExpression authors(QStringLiteral(
            R"([,;]\s*[*#†‡∇\d]*\s*[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,4})"));
        QStringList titleLines;
        bool sawAuthors = false;
        for (qsizetype i = masthead + 1; i < citation; ++i) {
            const QString line = lines[i].simplified();
            if (line.compare(QStringLiteral("Article"), Qt::CaseInsensitive) == 0 ||
                line.compare(QStringLiteral("Letter"), Qt::CaseInsensitive) == 0 ||
                line.compare(QStringLiteral("Communication"), Qt::CaseInsensitive) == 0 ||
                line.compare(QStringLiteral("Research Article"), Qt::CaseInsensitive) == 0)
                continue;
            if (authors.match(line).hasMatch()) {
                sawAuthors = true;
                break;
            }
            if (line.size() > 180 || titleLines.size() >= 4 ||
                line.contains(QStringLiteral("Downloaded from"), Qt::CaseInsensitive))
                return {};
            titleLines.append(line);
        }
        if (!sawAuthors || titleLines.isEmpty()) return {};
        QString title = titleLines.join(QLatin1Char(' ')).simplified();
        // ACS line breaks sometimes split a retained chemical hyphen.
        title.replace(QRegularExpression(QStringLiteral(R"(-\s+(?=\p{L}))")),
                      QStringLiteral("-"));
        return credibleTitle(title) ? title : QString();
    }

    // Royal Society of Chemistry first pages have a publisher masthead and
    // a printed DOI, but title/author lines may be interleaved with two-column
    // "Cite this" metadata. Return nothing for unknown or ambiguous layouts.
    static bool isRscArticleCover(const QString& firstPageText) {
        const QStringList lines=firstPageText.left(8000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")),Qt::SkipEmptyParts);
        if (lines.size()<5 || !firstPageText.left(8000).contains(
                QStringLiteral("10.1039/"),Qt::CaseInsensitive)) return false;
        const QString first=lines.first().simplified();
        return first.compare(QStringLiteral("ChemComm"),Qt::CaseInsensitive)==0 ||
               (first.compare(QStringLiteral("Organic &"),Qt::CaseInsensitive)==0 &&
                lines.value(1).simplified().compare(
                    QStringLiteral("Biomolecular Chemistry"),Qt::CaseInsensitive)==0);
    }

    static QString firstPageRscTitle(const QString& firstPageText) {
        if (!isRscArticleCover(firstPageText) ||
            isSupportingInformationCover(firstPageText)) return {};
        const auto lines=firstPageText.left(12000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")),Qt::SkipEmptyParts);
        qsizetype kind=-1;
        for (qsizetype i=0;i<lines.size() && i<8;++i) {
            const QString label=lines[i].simplified();
            if (label.compare(QStringLiteral("PAPER"),Qt::CaseInsensitive)==0 ||
                label.compare(QStringLiteral("COMMUNICATION"),Qt::CaseInsensitive)==0) {
                kind=i;break;
            }
        }
        if(kind<0) return {};
        static const QRegularExpression authorStart(QStringLiteral(
            R"(^[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}\s*,)"));
        static const QRegularExpression volumeOnly(QStringLiteral(
            R"(^\d{1,4}\s*,\s*\d{1,7}$)"));
        QStringList titleLines;
        bool sawAuthor=false;
        for(qsizetype i=kind+1;i<lines.size() && i<24;++i) {
            const QString line=lines[i].simplified();
            if (authorStart.match(line).hasMatch()) {sawAuthor=true;break;}
            // In ChemComm's two-column cover, Received/Accepted dates can
            // appear before the final author line. Skip, do not terminate.
            if (line.startsWith(QStringLiteral("Received"),Qt::CaseInsensitive) ||
                line.startsWith(QStringLiteral("Accepted"),Qt::CaseInsensitive))
                continue;
            if (line.startsWith(QStringLiteral("DOI:"),Qt::CaseInsensitive))
                break;
            if (line.compare(QStringLiteral("Licence and permissions"),Qt::CaseInsensitive)==0 ||
                line.startsWith(QStringLiteral("Licensed under"),Qt::CaseInsensitive) ||
                line.startsWith(QStringLiteral("Cite this:"),Qt::CaseInsensitive) ||
                volumeOnly.match(line).hasMatch()) continue;
            if (line.size()<4 || line.size()>180 || titleLines.size()>=4) return {};
            titleLines.append(line);
        }
        if(!sawAuthor || titleLines.isEmpty()) return {};
        QString title=titleLines.join(QLatin1Char(' ')).simplified();
        title.replace(QRegularExpression(QStringLiteral(R"(-\s+(?=\p{L}))")),
                      QStringLiteral("-"));
        title.remove(QRegularExpression(QStringLiteral(R"([†‡]\s*$)")));
        return credibleTitle(title) ? title : QString();
    }

    static QString firstPageRscLastAuthor(const QString& firstPageText) {
        if (!isRscArticleCover(firstPageText) ||
            isSupportingInformationCover(firstPageText)) return {};
        const auto lines=firstPageText.left(12000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")),Qt::SkipEmptyParts);
        static const QRegularExpression authorList(QStringLiteral(
            R"(^[\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}\s*,)"));
        static const QRegularExpression trailingAnd(QStringLiteral(
            R"((?:^|\s)\band\s*$)"),QRegularExpression::CaseInsensitiveOption);

        bool authorListBefore=false;
        for(qsizetype i=0;i<lines.size() && i<22;++i) {
            const QString row=lines[i].simplified();
            if(authorList.match(row).hasMatch()) authorListBefore=true;
            if(!authorListBefore || !trailingAnd.match(row).hasMatch()) continue;
            // A separate centered author name can follow the ampersand/and
            // after publisher footnotes, acceptance metadata or asterisks.
            for(qsizetype j=i+1;j<lines.size() && j<=i+4;++j) {
                QString next=lines[j].simplified();
                if(next.startsWith(QStringLiteral("Received"),Qt::CaseInsensitive) ||
                   next.startsWith(QStringLiteral("Accepted"),Qt::CaseInsensitive) ||
                   next.startsWith(QStringLiteral("Published"),Qt::CaseInsensitive) ||
                   next.startsWith(QStringLiteral("DOI:"),Qt::CaseInsensitive) ||
                   next.size()<=3) continue;
                next.remove(QRegularExpression(QStringLiteral(R"(^[*†‡\s\d]+)")));
                next.remove(QRegularExpression(QStringLiteral(R"([*†‡\s\d]+$)")));
                if(credibleAuthor(next)) return next;
                break;
            }
        }
        return {};
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
        // SI pages can cite the main paper; do not import these as SI's own fields.
        if (isSupportingInformationCover(text)) return {};
        // RSC footers carry the complete printed page range. The "Cite this"
        // masthead often exposes only the first page, and text columns may be
        // interleaved between its year/volume/page fragments. Prefer the
        // publisher's own exact footer over a partial front-matter match.
        static const QRegularExpression chemCommFooter(QStringLiteral(
            R"(^\d{1,7}\s*\|\s*Chem\.\s*Commun\.,\s*(?<year>(?:19|20)\d{2}),\s*(?<volume>\d{1,4}),\s*(?<page>\d{1,7}-\d{1,7})$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression obcFooter(QStringLiteral(
            R"(^Org\.\s*Biomol\.\s*Chem\.,\s*(?<year>(?:19|20)\d{2}),\s*(?<volume>\d{1,4}),\s*(?<page>\d{1,7}-\d{1,7})\s*\|\s*\d{1,7}$)"),
            QRegularExpression::CaseInsensitiveOption);
        for (qsizetype i=0; i<lines.size() && i<160; ++i) {
            const QString row=lines[i].simplified();
            const auto cc=chemCommFooter.match(row);
            if (cc.hasMatch())
                return {{"journal",QStringLiteral("Chemical Communications")},
                        {"year",cc.captured(QStringLiteral("year"))},
                        {"volume",cc.captured(QStringLiteral("volume"))},
                        {"page",cc.captured(QStringLiteral("page"))}};
            const auto ob=obcFooter.match(row);
            if (ob.hasMatch())
                return {{"journal",QStringLiteral("Organic & Biomolecular Chemistry")},
                        {"year",ob.captured(QStringLiteral("year"))},
                        {"volume",ob.captured(QStringLiteral("volume"))},
                        {"page",ob.captured(QStringLiteral("page"))}};
        }
        static const QRegularExpression pattern(QStringLiteral(
            R"((?<journal>J\.?\s*Am\.?\s*Chem\.?\s*Soc\.?|J\.?\s*Org\.?\s*Chem\.?|J\.?\s*Med\.?\s*Chem\.?|ACS\s+Med\.?\s*Chem\.?\s*Lett\.?|J\.?\s*Phys\.?\s*Chem\.?\s*[ABC]\.?|Org\.?\s*Lett\.?|ACS\s+Catal\.?|Angew\.?\s*Chem\.?\s*(?:Int\.?\s*Ed\.?)?|Chem\.?\s*Sci\.?|Chem\.?\s*Commun\.?|Chem\.?\s*Eur\.?\s*J\.?|Adv\.?\s*Synth\.?\s*Catal\.?|Org\.?\s*Biomol\.?\s*Chem\.?|Nat(?:ure)?\.?\s*Chem\.?|Nature|Science)\s*[,;:]?\s*(?<year>(?:19|20)\d{2})\s*[,;:]\s*(?<volume>\d{1,4})(?:\s*\(\s*\d{1,3}\s*\))?\s*[,;:]\s*(?<page>(?:e\d{5,9}|\d{1,7})(?:\s*-\s*\d{1,7})?))"),
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
                // Journal-year-volume appearing in a title, author reference,
                // or early body citation is not the paper's publication data.
                // Keep printed header lines and explicit publisher "Cite" cues.
                const QString prefix = candidate.left(
                    match.capturedStart(QStringLiteral("journal"))).simplified();
                static const QRegularExpression printedCitationCue(QStringLiteral(
                    R"(^(?:Cite\s+(?:This|this article)|How to cite)\s*:)$"),
                    QRegularExpression::CaseInsensitiveOption);
                if (!prefix.isEmpty() && !printedCitationCue.match(prefix).hasMatch())
                    continue;
                const QString pages = match.captured(QStringLiteral("page")).remove(QLatin1Char(' '));
                QVariantMap citation = {
                    {"journal", match.captured(QStringLiteral("journal")).simplified()},
                    {"year", match.captured(QStringLiteral("year"))},
                    {"volume", match.captured(QStringLiteral("volume"))}};
                // Preserve the legacy firstPageCitation() "page=e..." view
                // for existing native callers, but also identify the actual
                // article-number field for accurate persistence and scoring.
                citation["page"] = pages;
                if (pages.startsWith(QLatin1Char('e'), Qt::CaseInsensitive))
                    citation["article_number"] = pages;
                return citation;
            }
        }
        // Publisher-specific front-page citation formats that do not follow
        // "Journal YEAR, VOLUME, PAGES". Match only clearly printed metadata,
        // not reference lists or inferred DOI content.
        static const QRegularExpression elsevier(QStringLiteral(
            R"(^(?<journal>Tetrahedron Letters|Bioorg\. Med\. Chem\. Lett\.|Bioorg\. Med\. Chem\.|Bioorganic Chemistry|European Journal of Medicinal Chemistry)\s+(?<volume>\d{1,4})\s*\((?<year>(?:19|20)\d{2})\)\s+(?<article_number>\d{5,8})$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression springer(QStringLiteral(
            R"(^(?<journal>Medicinal Chemistry Research|Silicon)\s*\((?<year>(?:19|20)\d{2})\)\s*(?<volume>\d{1,4}):(?<page>\d{1,7}[-–]\d{1,7})$)"),
            QRegularExpression::CaseInsensitiveOption);
        // Elsevier and Springer header records are at the very top (not the
        // body reference list). Do not use a later matching citation.
        for (qsizetype i=0; i<lines.size() && i<5; ++i) {
            const QString line = lines[i].simplified();
            const auto e = elsevier.match(line);
            if (e.hasMatch()) {
                QString journal = e.captured(QStringLiteral("journal"));
                if (journal.compare(QStringLiteral("Bioorg. Med. Chem. Lett."),Qt::CaseInsensitive)==0)
                    journal = QStringLiteral("Bioorganic & Medicinal Chemistry Letters");
                else if (journal.compare(QStringLiteral("Bioorg. Med. Chem."),Qt::CaseInsensitive)==0)
                    journal = QStringLiteral("Bioorganic & Medicinal Chemistry");
                return {{"journal",journal},
                        {"year",e.captured(QStringLiteral("year"))},
                        {"volume",e.captured(QStringLiteral("volume"))},
                        {"article_number",e.captured(QStringLiteral("article_number"))}};
            }
            const auto s = springer.match(line);
            if (s.hasMatch()) {
                QString page=s.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-'));
                return {{"journal",s.captured(QStringLiteral("journal"))},
                        {"year",s.captured(QStringLiteral("year"))},
                        {"volume",s.captured(QStringLiteral("volume"))},
                        {"page",page}};
            }
        }
        // Nature publication metadata lives in the printed footer, frequently
        // >40 layout lines after the title/abstract. Require the exact journal
        // heading and footer format; never treat a random body citation as own.
        static const QRegularExpression naturePage(QStringLiteral(
            R"(^(?<journal>Nature Chemistry|Nature Synthesis)\s*\|\s*Volume\s+(?<volume>\d{1,4})\s*\|\s*[A-Za-z]+\s+(?<year>(?:19|20)\d{2})\s*\|\s*(?<page>\d{1,7}[-–]\d{1,7})$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression natureArticle(QStringLiteral(
            R"(^Nature Communications\s*\|\s*\((?<year>(?:19|20)\d{2})\)\s*(?<volume>\d{1,4}):(?<article_number>\d{1,7})$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression rscFooter(QStringLiteral(
            R"(^\d+\s*\|\s*Chem\.\s*Commun\.,\s*(?<year>(?:19|20)\d{2}),\s*(?<volume>\d{1,4}),\s*(?<page>\d{1,7}[-–]\d{1,7})$)"),
            QRegularExpression::CaseInsensitiveOption);
        for (qsizetype i=0; i<lines.size() && i<160; ++i) {
            const QString line=lines[i].simplified();
            const auto n=naturePage.match(line);
            if (n.hasMatch()) {
                QString page=n.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-'));
                return {{"journal",n.captured(QStringLiteral("journal"))},
                        {"year",n.captured(QStringLiteral("year"))},
                        {"volume",n.captured(QStringLiteral("volume"))},
                        {"page",page}};
            }
            const auto a=natureArticle.match(line);
            if (a.hasMatch())
                return {{"journal",QStringLiteral("Nature Communications")},
                        {"year",a.captured(QStringLiteral("year"))},
                        {"volume",a.captured(QStringLiteral("volume"))},
                        {"article_number",a.captured(QStringLiteral("article_number"))}};
            // RSC "Cite this:" lines can have intervening title columns.
            // A printed footer provides the unambiguous complete page range.
            const auto r=rscFooter.match(line);
            if (r.hasMatch()) {
                QString page=r.captured(QStringLiteral("page"));
                page.replace(QChar(0x2013),QLatin1Char('-'));
                return {{"journal",QStringLiteral("Chemical Communications")},
                        {"year",r.captured(QStringLiteral("year"))},
                        {"volume",r.captured(QStringLiteral("volume"))},
                        {"page",page}};
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
        authors.replace(QRegularExpression(QStringLiteral(R"([\x{2020}\x{2021}*]+)")), QString());
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

    // A fragment such as "Hannah M. Kortman, Hao Fang" is NOT the final
    // author. Require an explicit ending "and John J. Molloy", with a nearby
    // comma-delimited author list on the first-page cover. On ambiguity,
    // return no author rather than the first plausible personal name.
    static QString firstPageLastAuthor(const QString& firstPageText) {
        const QStringList raw = firstPageText.left(24000).split(
            QRegularExpression(QStringLiteral(R"([\r\n]+)")), Qt::SkipEmptyParts);
        QStringList lines;
        for (const QString& candidate : raw) {
            const QString line = candidate.simplified();
            if (!line.isEmpty()) lines.append(line);
            if (lines.size() >= 75) break;
        }
        static const QRegularExpression terminal(QStringLiteral(
            R"((?:^|[\s,])(?:and|&)\s+([\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}))"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression superscript(QStringLiteral(
            R"(^[\s*†‡\p{Ll}\p{N}]*$)"));
        static const QRegularExpression pairedNames(QStringLiteral(
            R"([\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}\s*,\s*(?:[a-dA-D0-9*†‡]{1,4}\s+)?[\p{Lu}])"));
        QString found;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            const auto match = terminal.match(line);
            if (!match.hasMatch()) continue;
            if (!superscript.match(line.mid(match.capturedEnd())).hasMatch()) continue;
            const QString candidate = match.captured(1).simplified();
            if (!credibleAuthor(candidate)) continue;
            bool hasAuthorList = pairedNames.match(line).hasMatch();
            for (qsizetype delta = 1; !hasAuthorList && delta <= 7; ++delta) {
                if (i >= delta && pairedNames.match(lines[i - delta]).hasMatch())
                    hasAuthorList = true;
                if (i + delta < lines.size() && pairedNames.match(lines[i + delta]).hasMatch())
                    hasAuthorList = true;
            }
            if (!hasAuthorList) continue;
            if (!found.isEmpty() && found != candidate) return {};
            found = candidate;
        }
        return found;
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
        // Do not mine an SI cover for the parent article's title/authors.
        if (isSupportingInformationCover(text)) return {};
        if (firstPageCitation(text).value("journal").toString().isEmpty()) return {};
        const QString explicitLastAuthor = firstPageLastAuthor(text);
        if (explicitLastAuthor.isEmpty()) return {};
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
                const bool publicationBoundary =
                    candidate.startsWith(QStringLiteral("Cite this"), Qt::CaseInsensitive) ||
                    !firstPageCitation(candidate).isEmpty() ||
                    candidate.contains(QStringLiteral("https://"), Qt::CaseInsensitive) ||
                    candidate.contains(QStringLiteral("doi.org"), Qt::CaseInsensitive);
                if (publicationBoundary) break;
                // Acceptance dates can be between the author block and title,
                // but mastheads and article-type headings stop title collection.
                if (headingPattern.match(candidate).hasMatch()) {
                    if (candidate.startsWith(QStringLiteral("Received"), Qt::CaseInsensitive) ||
                        candidate.startsWith(QStringLiteral("Accepted"), Qt::CaseInsensitive))
                        continue;
                    break;
                }
                if (candidate.size() > 250) break;
                if (candidate.size() >= 5)
                    titleLines.prepend(candidate);
            }
            const QString title = titleLines.join(QLatin1Char(' '));
            if (!credibleTitle(title) || !credibleAuthor(lastListedAuthor(authors)))
                continue;
            return {{"title", title.left(4096)}, {"authors", explicitLastAuthor}};
        }
        return {};
    }

    // Recall's evidence rule: never present arbitrary strings as trusted metadata.
    // In particular PDF printer defaults and body sentences are not paper titles.
    static bool credibleTitle(const QString& value) {
        const QString text = value.simplified();
        if (text.size() < 12 || text.size() > 240) return false;
        // ACS PDF Info titles like "jo5c01153 1..9" and "ml5c00120 1..7"
        // are printer job names, not scientific titles. They MUST NOT
        // displace a visible first-page title or DOI-matched provider title.
        static const QRegularExpression printerJob(QStringLiteral(
            R"(^(?:[A-Za-z]{1,18}\.)?[A-Za-z]{1,20}\d[A-Za-z0-9.]*\s+\d+\s*\.{2,}\s*\d+$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression printerSpool(QStringLiteral(
            R"(^[A-Za-z0-9_.-]{7,70}\s+\d{1,7}\s*\.{2,}\s*\d{1,7}\s*(?:\+\+)?$)"),
            QRegularExpression::CaseInsensitiveOption);
        if (printerJob.match(text).hasMatch() || printerSpool.match(text).hasMatch())
            return false;
        static const QRegularExpression placeholder(QStringLiteral(
            R"((?:^|\b)(?:No Job Name|Microsoft Word|Untitled|New Document|Document1|Supporting Information|Revised Supporting Information)(?:\b|$))"),
            QRegularExpression::CaseInsensitiveOption);
        if (placeholder.match(text).hasMatch() ||
            text.contains(QStringLiteral("All publication charges"), Qt::CaseInsensitive) ||
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
        if (proposal.value("documentKind").toString() == QStringLiteral("supporting_information")) {
            // Preserve all previous data for recovery; suppress only machine-owned
            // paper fields after the page explicitly identifies itself as SI.
            QVariantMap archived = value.value("unverifiedSiMetadata").toMap();
            for (const char* key : {"title", "authors", "journal", "year", "volume", "page", "citation", "doi", "authors_structured", "journal_abbreviation", "issue", "article_number"}) {
                const QString field = QString::fromLatin1(key);
                if (value.value(field + QStringLiteral("Source")).toString() != QStringLiteral("manual") &&
                    value.contains(field)) {
                    archived[field] = value.take(field);
                    changedField = true;
                }
            }
            changedField = changedField || value.value("documentKind") != proposal.value("documentKind");
            value["documentKind"] = proposal.value("documentKind");
            value["unverifiedSiMetadata"] = archived;
            return changedField && save(identity, value, false);
        }
        const auto fill = [&value, &proposal, &changedField](const char* key, bool good) {
            const QString field = QString::fromLatin1(key);
            const QString next = proposal.value(field).toString().simplified();
            if (!good || next.isEmpty() ||
                value.value(field + QStringLiteral("Source")).toString() == QStringLiteral("manual")) return;
            const QString old = value.value(field).toString().simplified();
            if (!old.isEmpty()) return;
            value[field] = next.left(4096);
            value[field + QStringLiteral("Source")] =
                field == QStringLiteral("title") && proposal.value("titleSource").toString() == QStringLiteral("pdf_metadata") ?
                    QStringLiteral("pdf_metadata") : QStringLiteral("first_page_verified");
            changedField = true;
        };
        fill("title", credibleTitle(proposal.value("title").toString()));
        // Re-extraction is an explicit user action. A confirmed final author
        // supersedes an earlier incomplete/incorrect AUTO value such as Hao Fang,
        // but never changes a user-entered manual author.
        const QString proposedAuthor = proposal.value("authors").toString().simplified();
        if (proposal.value("authorEvidence").toString() == QStringLiteral("first-page-terminal-and") &&
            credibleAuthor(proposedAuthor) &&
            value.value("authorsSource").toString() != QStringLiteral("manual") &&
            value.value("authorsSource").toString() != QStringLiteral("Crossref") &&
            value.value("authorsSource").toString() != QStringLiteral("OpenAlex") &&
            value.value("authorsSource").toString() != QStringLiteral("Crossref + OpenAlex") &&
            value.value("authors").toString().simplified() != proposedAuthor) {
            value["authors"] = proposedAuthor;
            value["authorsSource"] = QStringLiteral("first_page_verified");
            value["authorEvidence"] = QStringLiteral("first-page-terminal-and");
            changedField = true;
        }
        fill("journal", !proposal.value("journal").toString().isEmpty());
        const QString year = proposal.value("year").toString();
        static const QRegularExpression yyyy(QStringLiteral(R"(^[12]\d{3}$)"));
        fill("year", yyyy.match(year).hasMatch());
        static const QRegularExpression volumePattern(QStringLiteral(R"(^\d{1,4}$)"));
        fill("volume", volumePattern.match(proposal.value("volume").toString()).hasMatch());
        static const QRegularExpression pagePattern(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,7})(?:[-–]\d{1,7})?$)"),
                                                    QRegularExpression::CaseInsensitiveOption);
        const QString proposedPage = proposal.value("page").toString();
        const bool articleInLegacyPage = !proposal.value("article_number").toString().isEmpty() &&
            proposedPage.compare(proposal.value("article_number").toString(),Qt::CaseInsensitive)==0;
        fill("page", !articleInLegacyPage && pagePattern.match(proposedPage).hasMatch());
        static const QRegularExpression articlePattern(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,8})$)"),
                                                       QRegularExpression::CaseInsensitiveOption);
        fill("article_number", articlePattern.match(proposal.value("article_number").toString()).hasMatch());
        if (!changedField) return false;
        value["source"] = QStringLiteral("PDF first-page evidence");
        // Preserve prior manual ownership, but never mark machine extraction manual.
        return save(identity, value, false);
    }

    QVariantMap record(const QString& identity) const {
        QVariantMap value = settings().value(key(resolve(identity))).toMap();
        // The full citation field had only a manual-paste writer in the legacy
        // editor; automatic extraction wrote structured fields, never this field.
        if (!value.contains("citationSource") && !value.value("citation").toString().simplified().isEmpty())
            value["citationSource"] = QStringLiteral("manual");
        // A legacy record-wide flag alone cannot prove that the author was edited.
        // Keep the original value for recovery, but never display an unverified name.
        if (!value.contains("authorsSource")) {
            if (!value.value("manual").toBool() &&
                     value.value("authorEvidence").toString() == QStringLiteral("first-page-terminal-and"))
                value["authorsSource"] = QStringLiteral("first_page_verified");
            else {
                if (!value.value("authors").toString().isEmpty())
                    value["legacyAuthors"] = value.value("authors");
                value["authorsSource"] = QStringLiteral("legacy_unknown");
            }
        }
        const bool manual = value.value("manual").toBool();
        if (manual && !value.contains("titleSource") && !value.value("title").toString().isEmpty())
            value["titleSource"] = QStringLiteral("legacy_preserved");
        const QString title = value.value("title").toString().simplified();
        const QString author = value.value("authors").toString().simplified();
        // Never hide a legitimate manually edited short title or one-word
        // family name. Suppress only notorious generated placeholders, even
        // when an older auto-extraction dialog accidentally marked them manual.
        static const QRegularExpression placeholder(QStringLiteral(
            R"(^(?:No Job Name|Microsoft Word(?:\b.*)?|Untitled|New Document|Document1)\b)"),
            QRegularExpression::CaseInsensitiveOption);
        const bool brokenTitle = placeholder.match(title).hasMatch() ||
            (value.value("titleSource").toString() != QStringLiteral("manual") &&
             title.contains(QStringLiteral("All publication charges"), Qt::CaseInsensitive)) ||
            (!manual && value.value("titleSource").toString() != QStringLiteral("manual") &&
             value.value("titleSource").toString() != QStringLiteral("legacy_preserved") &&
             !title.isEmpty() && !credibleTitle(title));
        const QString authorSource=value.value("authorsSource").toString();
        const bool verifiedProviderAuthor = authorSource == QStringLiteral("Crossref") ||
            authorSource == QStringLiteral("OpenAlex") ||
            authorSource == QStringLiteral("Crossref + OpenAlex");
        const bool brokenAuthor = authorSource != QStringLiteral("manual") &&
            (author.contains(QStringLiteral("as well as"), Qt::CaseInsensitive) ||
            author.contains(QStringLiteral("the energetics"), Qt::CaseInsensitive) ||
            author.contains(QStringLiteral("validation as"), Qt::CaseInsensitive) ||
            author.contains(QStringLiteral("lactamase inhibitors"), Qt::CaseInsensitive) ||
            author.contains(QStringLiteral("towards"), Qt::CaseInsensitive) ||
            (authorSource != QStringLiteral("manual") &&
             authorSource != QStringLiteral("first_page_verified") && !verifiedProviderAuthor) ||
            (!author.isEmpty() && !credibleAuthor(author)));
        if (brokenTitle) {
            if (!title.isEmpty()) value["legacyTitle"] = value.value("title");
            value.remove("title");
        }
        if (brokenAuthor) value.remove("authors");
        return value;
    }

    bool save(const QString& identity, QVariantMap value, bool manual = true) {
        if (identity.isEmpty()) return false;
        const QString target = resolve(identity);
        // Additive DOI field support. Legacy bibliography values stay intact.
        value["manual"] = manual;
        if (manual) {
            const QVariantMap previous = record(identity);
            for (const char* field : {"title", "authors", "citation", "journal", "year", "volume", "page", "doi", "journal_abbreviation", "issue", "article_number", "senior_author_override"}) {
                const QString name = QString::fromLatin1(field);
                if (!value.contains(name + QStringLiteral("Source")) || value.value(name) != previous.value(name))
                    value[name + QStringLiteral("Source")] = QStringLiteral("manual");
            }
        }
        auto store = settings();
        store.setValue(key(target), value);
        store.sync();
        if (store.status() != QSettings::NoError) return false;
        if (changed) changed(target);
        return true;
    }

    // Additive field-level merge. Existing manual edits, citation pastes and
    // paper identity take priority; neither metadata lookup nor an error may
    // overwrite them. DOI is ALWAYS checked against the PDF evidence first.
    bool mergeVerifiedOnline(const QString& identity, const QString& expectedDoi,
                             const QVariantMap& incoming, const QString& diagnostic = {}) {
        const QString expected=Metadata::normalizeDoi(expectedDoi);
        const QString returned=Metadata::normalizeDoi(incoming.value("doi").toString());
        if(identity.isEmpty() || expected.isEmpty() || returned != expected) return false;
        QVariantMap entry=record(identity);
        // A disagreement between independent DOI-matched providers is NOT
        // permission to silently write whichever provider returned first.
        if(incoming.value("metadata_status").toString()==QStringLiteral("needs_review")) {
            entry["metadata_status"]=QStringLiteral("needs_review");
            entry["metadata_diagnostic"]=incoming.value("metadata_diagnostic").toString();
            save(identity,entry,false);return false;
        }
        const QString oldDoi=Metadata::normalizeDoi(entry.value("doi").toString());
        if(!oldDoi.isEmpty() && oldDoi!=expected) {
            entry["metadata_status"]=QStringLiteral("needs_review");
            entry["metadata_diagnostic"]=QStringLiteral("stored_doi_conflict");
            save(identity,entry,false);return false;
        }
        const QString localTitle=entry.value("title").toString();
        const QString externalTitle=incoming.value("title").toString();
        if(!localTitle.isEmpty() && !externalTitle.isEmpty() &&
           !Metadata::compatibleTitle(localTitle,externalTitle) &&
           (entry.value("titleSource").toString()==QStringLiteral("manual") ||
            entry.value("titleSource").toString()==QStringLiteral("pdf_metadata"))) {
            entry["metadata_status"]=QStringLiteral("needs_review");
            entry["metadata_diagnostic"]=QStringLiteral("pdf_api_title_conflict");
            save(identity,entry,false);return false;
        }
        // A clear conflict with printed PDF evidence or a manually corrected
        // year/volume/page is a review issue, not an opportunity to replace
        // or combine data belonging to different publication stages.
        for(const char* candidate : {"year","volume","page"}) {
            const QString field=QString::fromLatin1(candidate);
            const QString previous=entry.value(field).toString().simplified();
            const QString received=incoming.value(field).toString().simplified();
            const QString provenance=entry.value(field+QStringLiteral("Source")).toString();
            if(!previous.isEmpty() && !received.isEmpty() && previous!=received &&
               (provenance==QStringLiteral("manual") ||
                provenance==QStringLiteral("first_page_verified"))) {
                entry["metadata_status"]=QStringLiteral("needs_review");
                entry["metadata_diagnostic"]=QStringLiteral("pdf_field_conflict:")+field;
                save(identity,entry,false);return false;
            }
        }
        bool modified=false, conflict=false;
        if(oldDoi.isEmpty()){entry["doi"]=expected; entry["doiSource"]=QStringLiteral("pdf_front_matter");modified=true;}
        QVariantMap sources=entry.value("field_sources").toMap();
        for (const char* k : {"title","journal","journal_abbreviation","year","volume",
                               "issue","page","article_number","authors","authors_structured"}) {
            const QString key=QString::fromLatin1(k);
            const QVariant value=incoming.value(key);
            if(value.toString().simplified().isEmpty() &&
               !(key==QStringLiteral("authors_structured") && !value.toStringList().isEmpty()))
                continue;
            if(entry.value(key+"Source").toString()==QStringLiteral("manual") ||
               sources.value(key).toString()==QStringLiteral("manual"))continue;
            if(!entry.value(key).toString().isEmpty() ||
               (key==QStringLiteral("authors_structured") &&
                !entry.value(key).toStringList().isEmpty())) {
                // The first-page author extractor stores only the LAST author.
                // The full verified author list is more useful, but does not
                // override a manual-author field.
                if(key==QStringLiteral("authors") &&
                   entry.value("authorsSource").toString()==QStringLiteral("first_page_verified")) {
                    entry[key]=value; modified=true;
                    sources[key]=incoming.value("metadata_source").toString();
                } else if(key==QStringLiteral("title") &&
                          Metadata::compatibleTitle(entry.value(key).toString(),value.toString())) {
                    continue;
                } else if(key!=QStringLiteral("authors") &&
                          key!=QStringLiteral("authors_structured") &&
                          entry.value(key).toString().simplified()!=value.toString().simplified()) {
                    // Preserve both data values until user review.
                    conflict=true;
                }
                continue;
            }
            entry[key]=value;
            entry[key+"Source"]=incoming.value("metadata_source").toString();
            sources[key]=incoming.value("field_sources").toMap()
                .value(key,incoming.value("metadata_source")).toString();
            modified=true;
        }
        if(entry.value("field_sources").toMap()!=sources){entry["field_sources"]=sources;modified=true;}
        if(conflict || incoming.value("metadata_status").toString()==QStringLiteral("needs_review")){
            entry["metadata_status"]=QStringLiteral("needs_review");
            entry["metadata_diagnostic"]=QStringLiteral("source_field_conflict");
        } else {
            entry["metadata_status"]=Metadata::publicationStatus(entry);
            if(!diagnostic.isEmpty())entry["metadata_last_error"]=diagnostic;
        }
        entry["metadata_checked_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        entry["metadata_source"]=incoming.value("metadata_source").toString();
        Q_UNUSED(modified);
        return save(identity,entry,false);
    }
    bool markLookupStarted(const QString& identity) {
        if(identity.isEmpty())return false;
        QVariantMap entry=record(identity);
        if(entry.value("metadata_status").toString().isEmpty()) {
            entry["metadata_status"]=QStringLiteral("loading");
            return save(identity,entry,false);
        }
        return true;
    }
    bool recordLookupFailure(const QString& identity, const QString& status) {
        if(identity.isEmpty())return false;
        QVariantMap entry=record(identity);
        entry["metadata_last_error"]=status;
        if(entry.value("metadata_status").toString().isEmpty() ||
           entry.value("metadata_status").toString()==QStringLiteral("loading"))
            entry["metadata_status"]=status==QStringLiteral("offline")?
                QStringLiteral("offline"):QStringLiteral("partial");
        return save(identity,entry,false);
    }

    // Merge only fields actually changed in the dialog against its opening
    // snapshot. Background extraction may have updated other fields meanwhile.
    bool saveEdits(const QString& identity, const QVariantMap& before, const QVariantMap& edited) {
        if (identity.isEmpty()) return false;
        QVariantMap current = record(identity);
        bool modified = false;
        for (const char* key : {"title", "authors", "citation", "journal", "year", "volume", "page", "doi", "journal_abbreviation", "issue", "article_number", "senior_author_override"}) {
            const QString field = QString::fromLatin1(key);
            if (edited.value(field).toString().simplified() == before.value(field).toString().simplified()) continue;
            current[field] = edited.value(field);
            current[field + QStringLiteral("Source")] = QStringLiteral("manual");
            modified = true;
        }
        if (!modified) return true;
        if(edited.value("doi").toString().simplified()!=before.value("doi").toString().simplified()) {
            const QString chosen=Metadata::normalizeDoi(edited.value("doi").toString());
            if(!edited.value("doi").toString().trimmed().isEmpty() && chosen.isEmpty())return false;
            current["doi"]=chosen;
            current["doiSource"]=QStringLiteral("manual");
            current["metadata_status"]=QStringLiteral("needs_review");
            current["metadata_diagnostic"]=QStringLiteral("manual_doi_changed");
        }
        current["source"] = QStringLiteral("Manual field edits");
        return save(identity, current, false);
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
        if (entry.value("documentKind").toString() == QStringLiteral("supporting_information")) return;
        bool updated = false;
        const auto fill = [&entry, &updated](const QString& key, const QString& text, int maxLength) {
            const QString value = text.simplified().left(maxLength);
            if (entry.value(key + QStringLiteral("Source")).toString() != QStringLiteral("manual") &&
                entry.value(key).toString().isEmpty() && !value.isEmpty()) {
                entry[key] = value;
                entry[key + QStringLiteral("Source")] = QStringLiteral("pdf_metadata");
                updated = true;
            }
        };
        if (credibleTitle(title)) fill(QStringLiteral("title"), title, 4096);
        // PDF Info "Author" is often a creator/first-author field.
        // It cannot establish the final listed author, so never auto-import it.
        Q_UNUSED(authors);
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

    // Explicit user-requested reset. Only verified DOI-matched provider data
    // may replace manually edited fields; a failed lookup preserves all data.
    bool restoreVerifiedOnline(const QString& identity, const QString& expectedDoi,
                               const QVariantMap& incoming) {
        const QString expected = Metadata::normalizeDoi(expectedDoi);
        if (identity.isEmpty() || expected.isEmpty() ||
            Metadata::normalizeDoi(incoming.value("doi").toString()) != expected ||
            incoming.value("metadata_status").toString() == QStringLiteral("needs_review") ||
            !credibleTitle(incoming.value("title").toString())) return false;
        QVariantMap entry = record(identity);
        const QString existingDoi = Metadata::normalizeDoi(entry.value("doi").toString());
        if (!existingDoi.isEmpty() && existingDoi != expected) return false;
        const QString provider = incoming.value("metadata_source").toString();
        for (const char* name : {"title", "journal", "journal_abbreviation", "year",
                                 "volume", "issue", "page", "article_number",
                                 "authors", "authors_structured"}) {
            const QString field = QString::fromLatin1(name);
            const QVariant replacement = incoming.value(field);
            if (replacement.toString().simplified().isEmpty() &&
                !(field == QStringLiteral("authors_structured") &&
                  !replacement.toStringList().isEmpty())) continue;
            entry[field] = replacement;
            entry[field + QStringLiteral("Source")] = provider;
        }
        // Otherwise a previously pasted citation still masks all restored fields.
        entry.remove("citation");
        entry.remove("citationSource");
        entry.remove("senior_author_override");
        entry.remove("senior_author_overrideSource");
        entry["doi"] = expected;
        entry["doiSource"] = provider;
        entry["manual"] = false;
        entry["source"] = QStringLiteral("Verified DOI refresh");
        entry["metadata_source"] = provider;
        entry["field_sources"] = incoming.value("field_sources");
        entry["metadata_status"] = Metadata::publicationStatus(entry);
        entry["metadata_diagnostic"] = QString();
        entry["metadata_checked_at"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        return save(identity, entry, false);
    }

    // The local first-page path is used when no DOI is available. Do not
    // erase a user field unless credible printed evidence replaces it.
    bool restoreFromFirstPage(const QString& identity, const QVariantMap& proposal) {
        if (identity.isEmpty() || proposal.isEmpty() ||
            proposal.value("documentKind").toString() == QStringLiteral("supporting_information"))
            return false;
        QVariantMap entry = record(identity);
        bool changed = false;
        const auto replace = [&entry, &proposal, &changed](const char* name, bool valid) {
            const QString field = QString::fromLatin1(name);
            const QString next = proposal.value(field).toString().simplified();
            if (!valid || next.isEmpty()) return;
            entry[field] = next;
            entry[field + QStringLiteral("Source")] = QStringLiteral("first_page_verified");
            changed = true;
        };
        replace("title", credibleTitle(proposal.value("title").toString()));
        replace("authors", proposal.value("authorEvidence").toString() ==
                           QStringLiteral("first-page-terminal-and") &&
                           credibleAuthor(proposal.value("authors").toString()));
        replace("journal", !proposal.value("journal").toString().isEmpty());
        static const QRegularExpression year(QStringLiteral(R"(^[12]\d{3}$)"));
        replace("year", year.match(proposal.value("year").toString()).hasMatch());
        static const QRegularExpression volume(QStringLiteral(R"(^\d{1,4}$)"));
        replace("volume", volume.match(proposal.value("volume").toString()).hasMatch());
        static const QRegularExpression pages(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,7})(?:[-–]\d{1,7})?$)"),
                                               QRegularExpression::CaseInsensitiveOption);
        const QString proposedPage = proposal.value("page").toString();
        const bool articleInLegacyPage = !proposal.value("article_number").toString().isEmpty() &&
            proposedPage.compare(proposal.value("article_number").toString(),Qt::CaseInsensitive)==0;
        replace("page", !articleInLegacyPage && pages.match(proposedPage).hasMatch());
        static const QRegularExpression articles(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,8})$)"),
                                                 QRegularExpression::CaseInsensitiveOption);
        replace("article_number", articles.match(proposal.value("article_number").toString()).hasMatch());
        if (!changed) return false;
        entry.remove("citation");
        entry.remove("citationSource");
        entry.remove("senior_author_override");
        entry.remove("senior_author_overrideSource");
        entry["manual"] = false;
        entry["source"] = QStringLiteral("PDF first-page refresh");
        return save(identity, entry, false);
    }

    // Copy bibliography without changing either source or field provenance.
    // Never overwrite an existing destination record.
    bool cloneTo(const QString& sourceIdentity, const QString& destinationIdentity) {
        if (sourceIdentity.isEmpty() || destinationIdentity.isEmpty() ||
            resolve(sourceIdentity) == resolve(destinationIdentity)) return false;
        const QVariantMap value = record(sourceIdentity);
        if (value.isEmpty()) return true;
        auto store = settings();
        if (!store.value(key(destinationIdentity)).toMap().isEmpty()) return false;
        store.setValue(key(destinationIdentity), value);
        store.sync();
        if (store.status() != QSettings::NoError) return false;
        if (changed) changed(destinationIdentity);
        return true;
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
