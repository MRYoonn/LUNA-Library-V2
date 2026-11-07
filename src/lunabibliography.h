// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QSettings>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QFileInfo>
#include <QSet>
#include <QVariantMap>
#include <QString>
#include "lunametadataengine.h"
#include "lunafrontmatter.h"
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
        const QString pages = entry.value("page").toString().simplified().isEmpty()
            ? entry.value("article_number").toString().simplified()
            : entry.value("page").toString().simplified();
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
        // ACS main PDFs often contain a "Supporting Information" navigation
        // link. The independent SI Finder/Lab module found substring-based
        // classification incorrectly discarded ordinary article metadata.
        if (FrontMatter::isSupplementCover(text)) return {};
        // Exact publisher footer evidence beats truncated "Cite this" first-page
        // fragments; abstain when the matching publisher masthead is absent.
        const QVariantMap printed = FrontMatter::publisherCitation(text);
        if (!printed.isEmpty()) return printed;
        static const QRegularExpression pattern(QStringLiteral(
            R"((?<journal>J\.?\s*Am\.?\s*Chem\.?\s*Soc\.?|J\.?\s*Org\.?\s*Chem\.?|J\.?\s*Med\.?\s*Chem\.?|ACS\s+Med\.?\s*Chem\.?\s*Lett\.?|J\.?\s*Phys\.?\s*Chem\.?\s*[ABC]\.?|Org\.?\s*Lett\.?|ACS\s+Catal\.?|ACS\s+Omega|Angew\.?\s*Chem\.?\s*(?:Int\.?\s*Ed\.?)?|Chem\.?\s*Sci\.?|Chem\.?\s*Commun\.?|Chem\.?\s*Eur\.?\s*J\.?|Adv\.?\s*Synth\.?\s*Catal\.?|Org\.?\s*Biomol\.?\s*Chem\.?|Nat(?:ure)?\.?\s*Chem\.?|Nature|Science)\s*[,;:]?\s*(?<year>(?:19|20)\d{2})\s*[,;:]\s*(?<volume>\d{1,4})(?:\s*\(\s*\d{1,3}\s*\))?\s*[,;:]\s*(?<page>(?:e\d{5,9}|\d{1,7})(?:\s*-\s*\d{1,7})?))"),
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
                // Skip early body/reference strings that merely contain an
                // article citation. Require a printed citation start or the
                // publisher's explicit "Cite This" label.
                const QString prefix = candidate.left(
                    match.capturedStart(QStringLiteral("journal"))).simplified();
                static const QRegularExpression printCue(QStringLiteral(
                    R"(^(?:Cite\s+(?:This|this article)|How to cite)\s*:?$)"),
                    QRegularExpression::CaseInsensitiveOption);
                if (!prefix.isEmpty() && !printCue.match(prefix).hasMatch())
                    continue;
                QVariantMap entry{{"journal", match.captured(QStringLiteral("journal")).simplified()},
                        {"year", match.captured(QStringLiteral("year"))},
                        {"volume", match.captured(QStringLiteral("volume"))},
                        {"page", match.captured(QStringLiteral("page")).remove(QLatin1Char(' '))}};
                // RSC's "Cite this" sidebar often prints only the START page.
                // Do not assert that as the complete page range.
                if (entry.value("journal").toString().contains(QStringLiteral("Sci"), Qt::CaseInsensitive) &&
                    !entry.value("page").toString().contains(QLatin1Char('-')))
                    entry.remove("page");
                return entry;
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
            QString line = candidate.simplified();
            // Nature and other publisher PDFs can encode affiliation labels as
            // Unicode superscript digits separated by modifier-letter commas
            // (U+02D2). Normalize ONLY the author-list proof string: without
            // this, a valid "First Name¹˒², Next Name³ &\\nLast Name⁴" fails
            // the strict comma-delimited author-list and suffix checks.
            // Neither the PDF text nor any persisted metadata is rewritten.
            static constexpr ushort digitSuperscripts[] = {
                0x2070,0x00b9,0x00b2,0x00b3,0x2074,
                0x2075,0x2076,0x2077,0x2078,0x2079
            };
            for (int digit = 0; digit < 10; ++digit)
                line.replace(QChar(digitSuperscripts[digit]), QChar('0' + digit));
            line.replace(QChar(0x02d2), QLatin1Char(','));
            if (!line.isEmpty()) lines.append(line);
            if (lines.size() >= 75) break;
        }
        static const QRegularExpression terminal(QStringLiteral(
            R"((?:^|[\s,])(?:and|&)\s+([\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}))"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression superscript(QStringLiteral(
            R"(^(?:[\s*†‡]|[\p{Ll}\p{N}]+(?:\s*,\s*[\p{Ll}\p{N}]+)*)*$)"));
        static const QRegularExpression pairedNames(QStringLiteral(
            R"([\p{Lu}][\p{L}\p{M}'’.\-]*(?:\s+[\p{Lu}][\p{L}\p{M}'’.\-]*){1,5}(?:\s*[0-9*†‡]+(?:,[0-9]+)*)?\s*,\s*(?:[a-dA-D0-9*†‡]{1,4}\s+)?[\p{Lu}])"));
        QString found;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            // Do not search abstract/body sentences for another terminal name.
            static const QRegularExpression bodyBoundary(QStringLiteral(
                R"(^(?:ABSTRACT\b|Abstract\b|Introduction\b|A\s+R\s+T\s+I\s+C\s+L\s+E\b))"),
                QRegularExpression::CaseInsensitiveOption);
            if (bodyBoundary.match(line).hasMatch()) break;
            QString authorRow = line;
            // RSC and publisher PDF text blocks can put the final "and" at
            // the end of a row, with the final named author on the next row.
            // Join *only* that explicit conjunction boundary; never join
            // arbitrary neighboring text and guess a last author.
            static const QRegularExpression trailingAnd(QStringLiteral(
                R"((?:^|[,;\s])(?:and|&)\s*$)"),
                QRegularExpression::CaseInsensitiveOption);
            if (trailingAnd.match(line).hasMatch() && i + 1 < lines.size()) {
                // Nature sidebars can y-sort dates/superscripts between the
                // terminal conjunction and the final name in the author column.
                qsizetype next = i + 1;
                static const QRegularExpression sidebar(QStringLiteral(
                    R"(^(?:(?:Received|Accepted|Published online):.*|Check for updates|[\d,\s*†‡]+)$)"));
                while (next < lines.size() && next <= i + 3 && sidebar.match(lines[next]).hasMatch()) ++next;
                if (next < lines.size() && next <= i + 4 && lines[next].size() <= 90)
                    authorRow += QLatin1Char(' ') + lines[next];
            }
            const auto match = terminal.match(authorRow);
            if (!match.hasMatch()) continue;
            if (!superscript.match(authorRow.mid(match.capturedEnd())).hasMatch()) continue;
            const QString candidate = match.captured(1).simplified();
            if (!credibleAuthor(candidate)) continue;
            QString conjunctionPrefix=authorRow.left(match.capturedStart(1)).simplified();
            bool verifiedNatureInlineAmpersand = false;
            // Real PDF4QT extraction on three checksum-pinned Nature covers
            // contains standalone "& Last Name" lines after a typeset author
            // list. These are trustworthy ONLY with explicit Nature journal
            // evidence and a comma-separated pair of personal names above.
            // Do not accept a floating "&" joined to a later name: that can
            // name a NON-final contributor on spatially reordered pages.
            if (i < 24 && line.startsWith(QStringLiteral("& ")) &&
                firstPageCitation(firstPageText).value("journal").toString()
                    .compare(QStringLiteral("Nature Communications"), Qt::CaseInsensitive) == 0) {
                for (qsizetype back = 1; back <= 12 && back <= i; ++back) {
                    if (pairedNames.match(lines[i - back]).hasMatch()) {
                        verifiedNatureInlineAmpersand = true;
                        break;
                    }
                }
            }
            if (conjunctionPrefix.endsWith(QLatin1Char('&'))) {
                conjunctionPrefix.chop(1);
                conjunctionPrefix.remove(QRegularExpression(QStringLiteral(R"([\p{N},*†‡\s]+$)")));
                if (!verifiedNatureInlineAmpersand &&
                    !pairedNames.match(conjunctionPrefix).hasMatch() &&
                    !credibleAuthor(conjunctionPrefix.simplified())) continue;
            }
            bool hasAuthorList = pairedNames.match(line).hasMatch() ||
                verifiedNatureInlineAmpersand;
            // Angewandte two-author reviews use "First Name and Last Name"
            // without a comma. Require its printed journal citation as well as
            // exactly two credible personal names on that complete row.
            if (!hasAuthorList && firstPageCitation(firstPageText).value("journal").toString()
                    .startsWith(QStringLiteral("Angew"), Qt::CaseInsensitive)) {
                QString lead = authorRow.left(match.capturedStart(1)).simplified();
                lead.remove(QRegularExpression(QStringLiteral(R"(\s+(?:and|&)$)")));
                hasAuthorList = credibleAuthor(lead);
            }
            for (qsizetype delta = 1; !hasAuthorList && delta <= 7; ++delta) {
                if (i >= delta && pairedNames.match(lines[i - delta]).hasMatch())
                    hasAuthorList = true;
                // A FUTURE author list cannot establish that a preceding
                // title ending in "and Industrial Applications" is a name.
                // Ambiguous reordered covers should abstain, not use that title.
            }
            // RSC Chemical Science sometimes prints one contributor per
            // vertical PDF text row ("Patrick Charchar,", "Gavin E. Reid,",
            // ... "and Marie-Isabel Aguilar"). No single line then matches
            // pairedNames. Permit ONLY an explicitly terminal "and Name",
            // backed by >=2 preceding full comma-terminated PERSON rows
            // within the tight front-matter region and a printed journal cue.
            // Do not infer from lone names, title conjunctions or body prose.
            if (!hasAuthorList && i < 32 &&
                line.startsWith(QStringLiteral("and "), Qt::CaseInsensitive)) {
                const QString journal = firstPageCitation(firstPageText).value("journal").toString();
                const bool rscScience = journal.contains(QStringLiteral("Chem. Sci"),
                        Qt::CaseInsensitive) ||
                    journal.compare(QStringLiteral("Chemical Science"), Qt::CaseInsensitive) == 0;
                if (rscScience) {
                    int precedingNamedCommas = 0;
                    for (qsizetype back = 1; back <= 12 && back <= i; ++back) {
                        const QString previous = lines[i - back];
                        if (bodyBoundary.match(previous).hasMatch()) break;
                        if (!previous.endsWith(QLatin1Char(','))) continue;
                        const QString priorName = previous.left(previous.size() - 1).simplified();
                        if (credibleAuthor(priorName)) ++precedingNamedCommas;
                    }
                    hasAuthorList = precedingNamedCommas >= 2;
                }
            }
            if (!hasAuthorList) continue;
            if (!found.isEmpty() && found != candidate) return {};
            found = candidate;
        }
        return found;
    }

    static QString heliyonLastAuthor(const QString& text) {
        if (firstPageCitation(text).value("journal").toString()!=QStringLiteral("Heliyon")) return {};
        const QStringList rows=FrontMatter::lines(text, 12000);
        static const QRegularExpression kind(QStringLiteral(R"(^(?:Review|Research) article$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression affiliation(QStringLiteral(
            R"(^(?:[a-z]\s+)?(?:Department|School|Faculty|Institute|Research|College|Centre|Center|Laboratory)\b)"));
        static const QRegularExpression firstName(QStringLiteral(
            R"(^[\p{Lu}][\p{L}\p{M}.'’\-]*(?:\s+[\p{Lu}][\p{L}\p{M}.'’\-]*){1,5}(?:\s+[a-z])?\s*[,\*])"));
        qsizetype start=-1, end=-1, label=-1;
        for (qsizetype i=0; i<rows.size() && i<24; ++i) {
            if (kind.match(rows[i]).hasMatch()) { label=i; continue; }
            if (label<0) continue;
            if (affiliation.match(rows[i]).hasMatch()) { end=i; break; }
            if (start<0 && i<=label+6 && firstName.match(rows[i]).hasMatch()) start=i;
        }
        if (start<0 || end<=start || end-start>6) return {};
        QString block=rows.mid(start,end-start).join(QLatin1Char(' '));
        block.remove(QRegularExpression(QStringLiteral(R"([*†‡]+)")));
        QStringList names;
        for (QString part : block.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            part=part.simplified();
            if (part.isEmpty() || QRegularExpression(QStringLiteral(R"(^[a-z]$)")).match(part).hasMatch()) continue;
            part.remove(QRegularExpression(QStringLiteral(R"(\s+[a-z]$)")));
            if (!credibleAuthor(part)) return {};
            names.append(part);
        }
        return names.size()>=2 ? names.last() : QString();
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
        if (FrontMatter::isSupplementCover(text)) return {};
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
        // Publisher PDF Info /Title sometimes contains only an ACS printer
        // spool name (e.g. jo5c01153 1..9), not a paper headline.
        if (FrontMatter::isPrinterJobTitle(text)) return false;
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
        // A visible but unmapped glyph proves the old local extraction was
        // incomplete. Archive machine-owned titles; manual/provider values win.
        if (proposal.value("titleGlyphStatus").toString()==QStringLiteral("unmapped") ||
            proposal.value("titleConflict").toString()==QStringLiteral("embedded_metadata")) {
            const QString source=value.value("titleSource").toString();
            if (source==QStringLiteral("pdf_metadata") || source==QStringLiteral("first_page_layout") ||
                source==QStringLiteral("first_page_verified") || source==QStringLiteral("publisher_cover_verified")) {
                if (!value.value("title").toString().isEmpty()) {
                    value["legacyTitle"]=value.take("title");
                    value["titleSource"]=QStringLiteral("first_page_layout_review");
                    changedField=true;
                }
            }
        }
        // Typography ambiguity must remain a review-only suggestion, never
        // masquerade as verified bibliography or trigger PDF renaming.
        // Manual fields and existing machine-verified title/author are intact.
        const QString reviewTitle=proposal.value("titleCandidate").toString().simplified();
        if (proposal.value("titleStatus").toString() == QStringLiteral("needs_review") &&
            credibleTitle(reviewTitle) &&
            reviewTitle != value.value("title").toString().simplified() &&
            reviewTitle != value.value("titleCandidate").toString().simplified()) {
            value["titleCandidate"] = reviewTitle;
            value["titleCandidateSource"] = QStringLiteral("first_page_layout_review");
            value["titleConfidence"] = qBound(0, proposal.value("titleConfidence").toInt(), 100);
            changedField = true;
        }
        const auto fill = [&value, &proposal, &changedField](const char* key, bool good) {
            const QString field = QString::fromLatin1(key);
            const QString next = proposal.value(field).toString().simplified();
            if (!good || next.isEmpty() ||
                value.value(field + QStringLiteral("Source")).toString() == QStringLiteral("manual")) return;
            const QString old = value.value(field).toString().simplified();
            const bool strongerPrintedTitle =
                field == QStringLiteral("title") &&
                value.value(QStringLiteral("titleSource")).toString() == QStringLiteral("pdf_metadata") &&
                (proposal.value("titleSource").toString() == QStringLiteral("first_page_layout") ||
                 proposal.value("titleSource").toString() == QStringLiteral("publisher_cover_verified")) &&
                Metadata::normalizedTitle(old) != Metadata::normalizedTitle(next);
            const bool completePrintedRange = field == QStringLiteral("page") &&
                value.value(QStringLiteral("pageSource")).toString() == QStringLiteral("first_page_verified") &&
                !old.contains(QLatin1Char('-')) && !old.contains(QChar(0x2013)) &&
                next.startsWith(old + QLatin1Char('-'));
            if (!old.isEmpty() && !strongerPrintedTitle && !completePrintedRange) return;
            value[field] = next.left(4096);
            const QString suggestedSource = proposal.value(field + QStringLiteral("Source")).toString();
            value[field + QStringLiteral("Source")] =
                field == QStringLiteral("title") && suggestedSource == QStringLiteral("first_page_layout")
                    ? QStringLiteral("first_page_layout") :
                  field == QStringLiteral("title") && suggestedSource == QStringLiteral("publisher_cover_verified")
                    ? QStringLiteral("publisher_cover_verified") :
                  field == QStringLiteral("title") && suggestedSource == QStringLiteral("pdf_metadata")
                    ? QStringLiteral("pdf_metadata") : QStringLiteral("first_page_verified");
            changedField = true;
        };
        fill("title", credibleTitle(proposal.value("title").toString()));
        // Re-extraction is an explicit user action. A confirmed final author
        // supersedes an earlier incomplete/incorrect AUTO value such as Hao Fang,
        // but never changes a user-entered manual author.
        const QString proposedAuthor = proposal.value("authors").toString().simplified();
        const QString authorEvidence=proposal.value("authorEvidence").toString();
        const QString providerAuthor=value.value("field_sources").toMap().value("authors").toString();
        if ((authorEvidence == QStringLiteral("first-page-terminal-and") ||
             authorEvidence == QStringLiteral("publisher-bounded-author-block")) &&
            credibleAuthor(proposedAuthor) &&
            value.value("authorsSource").toString() != QStringLiteral("manual") &&
            value.value("authorsSource").toString() != QStringLiteral("Crossref") &&
            value.value("authorsSource").toString() != QStringLiteral("OpenAlex") &&
            value.value("authorsSource").toString() != QStringLiteral("Crossref + OpenAlex") &&
            providerAuthor != QStringLiteral("Crossref") &&
            providerAuthor != QStringLiteral("OpenAlex") &&
            providerAuthor != QStringLiteral("Crossref + OpenAlex") &&
            value.value("authors").toString().simplified() != proposedAuthor) {
            value["authors"] = proposedAuthor;
            value["authorsSource"] = QStringLiteral("first_page_verified");
            value["authorEvidence"] = authorEvidence;
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
        fill("page", pagePattern.match(proposal.value("page").toString()).hasMatch());
        // Nature Communications publishes article numbers rather than a
        // conventional page interval. Keep a provider/print-sourced number.
        static const QRegularExpression articleNumberPattern(QStringLiteral(R"(^(?:[1-9]\d{0,8}|e\d{5,9})$)"),
                                                               QRegularExpression::CaseInsensitiveOption);
        fill("article_number", articleNumberPattern.match(
            proposal.value("article_number").toString()).hasMatch());
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
        // Repair display of historic first-author caches only when a *saved*
        // DOI-matched provider record already contains a trusted ordered list.
        // A legacy "Hao Fang" authors string must never outrank the final
        // contributor in that list. Never infer trust from a bare QStringList.
        // The raw QSettings remains intact for rollback until an explicit
        // confirmed re-extraction writes the improved field.
        const QString originalAuthorSource = value.value("authorsSource").toString();
        const QString structuredSource =
            value.value("authors_structuredSource").toString().isEmpty()
                ? value.value("field_sources").toMap()
                    .value("authors_structured").toString()
                : value.value("authors_structuredSource").toString();
        const auto verifiedProvider = [](const QString& source) {
            return source == QStringLiteral("Crossref") ||
                   source == QStringLiteral("OpenAlex") ||
                   source == QStringLiteral("Crossref + OpenAlex");
        };
        const QStringList ordered = value.value("authors_structured").toStringList();
        if (!manual && originalAuthorSource != QStringLiteral("manual") &&
            verifiedProvider(structuredSource) &&
            !Metadata::normalizeDoi(value.value("doi").toString()).isEmpty() &&
            !ordered.isEmpty() && credibleAuthor(ordered.constLast())) {
            const QString verifiedLast = ordered.constLast().simplified();
            const QString savedLast = lastListedAuthor(value.value("authors").toString());
            if (savedLast != verifiedLast) {
                if (!value.value("authors").toString().isEmpty() &&
                    !value.contains("legacyAuthors"))
                    value["legacyAuthors"] = value.value("authors");
                value["authors"] = ordered.join(QStringLiteral(", "));
                value["authorsSource"] = structuredSource;
                value["authorEvidence"] = QStringLiteral("doi-matched-ordered-authors");
                value["authorsReconciledFromProvider"] = true;
            }
        }
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

    // Public, side-effect-free trust checks shared by the UI and native Qt
    // tests. A stale "complete" status is not evidence of ordered authors.
    static bool needsVerifiedAuthorRefresh(const QVariantMap& entry) {
        if (entry.value("manual").toBool() ||
            entry.value("authorsSource").toString()==QStringLiteral("manual") ||
            entry.value("documentKind").toString()==QStringLiteral("supporting_information"))
            return false;
        const QString source=entry.value("authorsSource").toString();
        const QString orderedSource=entry.value("authors_structuredSource").toString().isEmpty()
            ? entry.value("field_sources").toMap().value("authors_structured").toString()
            : entry.value("authors_structuredSource").toString();
        const bool trustedOrdered=(orderedSource==QStringLiteral("Crossref") ||
            orderedSource==QStringLiteral("OpenAlex") ||
            orderedSource==QStringLiteral("Crossref + OpenAlex")) &&
            !entry.value("authors_structured").toStringList().isEmpty();
        return entry.value("authors").toString().isEmpty() ||
               source==QStringLiteral("legacy_unknown") ||
               (source==QStringLiteral("first_page_verified") && !trustedOrdered);
    }

    static QString trustedSavedDoi(const QVariantMap& entry, bool explicitRestore=false) {
        if (entry.value("documentKind").toString()==QStringLiteral("supporting_information"))
            return {};
        const QString doi=Metadata::normalizeDoi(entry.value("doi").toString());
        if (doi.isEmpty()) return {};
        if (explicitRestore) return doi;
        const QString source=entry.value("doiSource").toString();
        for(const char* accepted : {"manual","page1","pdf_front_matter",
                                   "first_page_verified","Crossref","OpenAlex","metadata"})
            if(source==QString::fromLatin1(accepted)) return doi;
        return {};
    }

    // Lazily migrate a VERIFIED DOI-matched ordered author list on first
    // document open. Keep the wrong auto string in legacyAuthors and never
    // rewrite a user-owned record or an explicitly edited author.
    bool reconcileVerifiedAuthorship(const QString& identity) {
        if (identity.isEmpty()) return false;
        const QVariantMap original = settings().value(key(resolve(identity))).toMap();
        if (original.value("manual").toBool() ||
            original.value("documentKind").toString() == QStringLiteral("supporting_information") ||
            original.value("authorsSource").toString() == QStringLiteral("manual"))
            return false;
        QVariantMap verified = record(identity);
        if (!verified.value("authorsReconciledFromProvider").toBool())
            return false;
        verified.remove("authorsReconciledFromProvider");
        return save(identity, verified, false);
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
        // An old extractor could persist an unproven ordered author list.
        // Replace only that unowned cache with a full DOI-matched provider
        // list, keeping the earlier value for recovery. A conflict between
        // two previously DOI-verified lists is review-only, not an overwrite.
        const QStringList newOrdered=incoming.value("authors_structured").toStringList();
        const QStringList oldOrdered=entry.value("authors_structured").toStringList();
        const QString oldOrderedSource=entry.value("authors_structuredSource").toString().isEmpty()
            ? sources.value("authors_structured").toString()
            : entry.value("authors_structuredSource").toString();
        const bool oldOrderedVerified=oldOrderedSource==QStringLiteral("Crossref") ||
            oldOrderedSource==QStringLiteral("OpenAlex") ||
            oldOrderedSource==QStringLiteral("Crossref + OpenAlex");
        const bool orderedManual=oldOrderedSource==QStringLiteral("manual") ||
            entry.value("authorsSource").toString()==QStringLiteral("manual");
        const bool verifiedAuthorConflict=!newOrdered.isEmpty() && !oldOrdered.isEmpty() &&
            oldOrdered!=newOrdered && !orderedManual && oldOrderedVerified;
        if (!newOrdered.isEmpty() && !oldOrdered.isEmpty() && oldOrdered!=newOrdered &&
            !orderedManual) {
            if (!oldOrderedVerified) {
                entry["legacyStructuredAuthors"]=oldOrdered;
                entry["authors_structured"]=newOrdered;
                entry["authors_structuredSource"]=incoming.value("metadata_source").toString();
                sources["authors_structured"]=incoming.value("metadata_source").toString();
                modified=true;
            } else {
                entry["authorListReviewCandidate"]=newOrdered;
                conflict=true;
            }
        }
        for (const char* k : {"title","journal","journal_abbreviation","year","volume",
                               "issue","page","article_number","authors","authors_structured"}) {
            const QString key=QString::fromLatin1(k);
            // Do not let a replacement "authors" string slip past a verified
            // ordered-list conflict. Keep BOTH candidates for manual review.
            if (verifiedAuthorConflict &&
                (key==QStringLiteral("authors") ||
                 key==QStringLiteral("authors_structured"))) continue;
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
                    entry[key+"Source"]=incoming.value("metadata_source").toString();
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
        const QString authorEvidence=proposal.value("authorEvidence").toString();
        replace("authors", (authorEvidence == QStringLiteral("first-page-terminal-and") ||
                           authorEvidence == QStringLiteral("publisher-bounded-author-block")) &&
                           credibleAuthor(proposal.value("authors").toString()));
        replace("journal", !proposal.value("journal").toString().isEmpty());
        static const QRegularExpression year(QStringLiteral(R"(^[12]\d{3}$)"));
        replace("year", year.match(proposal.value("year").toString()).hasMatch());
        static const QRegularExpression volume(QStringLiteral(R"(^\d{1,4}$)"));
        replace("volume", volume.match(proposal.value("volume").toString()).hasMatch());
        static const QRegularExpression pages(QStringLiteral(R"(^(?:e\d{5,9}|\d{1,7})(?:[-–]\d{1,7})?$)"),
                                               QRegularExpression::CaseInsensitiveOption);
        replace("page", pages.match(proposal.value("page").toString()).hasMatch());
        static const QRegularExpression articleNumberPattern(QStringLiteral(R"(^(?:[1-9]\d{0,8}|e\d{5,9})$)"),
                                                               QRegularExpression::CaseInsensitiveOption);
        replace("article_number", articleNumberPattern.match(proposal.value("article_number").toString()).hasMatch());
        if (!changed) return false;
        entry.remove("citation");
        entry.remove("citationSource");
        entry.remove("senior_author_override");
        entry.remove("senior_author_overrideSource");
        entry["manual"] = false;
        entry["source"] = QStringLiteral("PDF first-page refresh");
        return save(identity, entry, false);
    }

    // Only user-selected PDFs create these links. Persist in the existing SI
    // bibliography sidecar so another PC can recognize a same-folder parent.
    bool attachManualSI(const QString& identity, const QString& parentPdfName) {
        if(identity.isEmpty() || parentPdfName.isEmpty() ||
           QFileInfo(parentPdfName).fileName()!=parentPdfName ||
           parentPdfName.contains(QLatin1Char('/')) ||
           parentPdfName.contains(QLatin1Char('\\')) ||
           QFileInfo(parentPdfName).suffix().compare(
               QStringLiteral("pdf"),Qt::CaseInsensitive)!=0)return false;
        QVariantMap item=record(identity);
        item["documentKind"]=QStringLiteral("supporting_information");
        item["manualSI"]=true;
        item["manualSiParentFile"]=parentPdfName;
        item["manualSiRelationSource"]=QStringLiteral("user_selected_pdf");
        return save(identity,item,false);
    }
    bool detachManualSI(const QString& identity) {
        if(identity.isEmpty())return false;
        QVariantMap item=record(identity);
        if(!item.value("manualSI").toBool())return false;
        item.remove("manualSI");
        item.remove("manualSiParentFile");
        item.remove("manualSiRelationSource");
        return save(identity,item,false);
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
