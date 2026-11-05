// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "lunatitleextractor.h"
namespace luna {
// Shared by production PDF4QT and the offline snapshot evaluator. No I/O,
// persistence or network: callers explicitly supply the bounded PDF evidence.
inline QVariantMap firstPageEvidence(const QStringList& pages,
    const QList<TitleExtraction::Line>& firstPageLines, double firstPageHeight,
    const QString& infoText, const QString& embeddedTitle) {
    const QString first=pages.isEmpty()?QString():pages.first();
    const auto candidates=Metadata::collectCandidates(pages,infoText);
    // Do not classify an ACS main-paper navbar hyperlink as an SI cover.
    if (FrontMatter::isSupplementCover(first))
        return {{"documentKind",QStringLiteral("supporting_information")},
                {"doiStatus",QStringLiteral("needs_review")}};
    QVariantMap evidence=Bibliography::firstPageCitation(first);
    const QVariantMap titleLayout=TitleExtraction::infer(firstPageLines,firstPageHeight);
    const QVariantMap textualTitle=Bibliography::firstPageIdentity(first);
    // Publisher-specific text title fallback from scheduled accuracy module:
    // require ACS or RSC masthead, author boundary and strong citation cues.
    const QString acs=FrontMatter::acsTitle(first);
    const QString rsc=acs.isEmpty() ? FrontMatter::rscTitle(first) : QString();
    const QString publisherTitle=!acs.isEmpty() ? acs : !rsc.isEmpty()
        ? rsc : FrontMatter::natureArticleTitle(first);
    // A prominent geometric title stays first; bounded publisher cover titles
    // rescue PDFs where font sizes are indistinguishable from author text.
    const QString title=titleLayout.value("title").toString();
    if (!title.isEmpty() && Bibliography::credibleTitle(title)) {
        evidence["title"]=title;
        evidence["titleSource"]=QStringLiteral("first_page_layout");
    } else if (Bibliography::credibleTitle(publisherTitle)) {
        evidence["title"]=publisherTitle;
        evidence["titleSource"]=QStringLiteral("publisher_cover_verified");
    } else if (!textualTitle.value("title").toString().isEmpty()) {
        evidence["title"]=textualTitle.value("title");
        evidence["titleSource"]=QStringLiteral("first_page_verified");
    }
    // Weak/competing typography evidence is useful in the manual editor,
    // but MUST NEVER become an automatically trusted title or PDF filename.
    const QString reviewTitle=titleLayout.value("titleCandidate").toString().simplified();
    if (titleLayout.value("titleStatus").toString()==QStringLiteral("needs_review") &&
        Bibliography::credibleTitle(reviewTitle) &&
        reviewTitle!=evidence.value("title").toString().simplified()) {
        evidence["titleCandidate"]=reviewTitle;
        evidence["titleConfidence"]=titleLayout.value("titleConfidence");
        evidence["titleStatus"]=QStringLiteral("needs_review");
    }
    if (evidence.isEmpty() && !Metadata::acceptableIdentity(candidates)) return {};
    if(Bibliography::credibleTitle(embeddedTitle)) {
        const QString pdfTitle=embeddedTitle.simplified();
        QString printed = first.left(6000).normalized(QString::NormalizationForm_KC).toCaseFolded();
        printed.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
        const QString normalized = Metadata::normalizedTitle(pdfTitle);
        const bool corroborated = normalized.size() >= 18 && printed.contains(normalized);
        const QString trusted = evidence.value("title").toString();
        // PDF Info is only corroborating evidence. Stale exporter titles or
        // mojibake must never replace the actual printed heading or rename PDFs.
        if (trusted.isEmpty() && corroborated) {
            evidence["title"]=pdfTitle;
            evidence["titleSource"]=QStringLiteral("first_page_verified");
        } else if (!trusted.isEmpty() && !Metadata::compatibleTitle(trusted,pdfTitle)) {
            evidence["titleStatus"]=QStringLiteral("needs_review");
        } else if (trusted.isEmpty()) {
            evidence["titleCandidate"]=pdfTitle;
            evidence["titleStatus"]=QStringLiteral("needs_review");
        }
    }
    // A geometric heading with an unmapped visible glyph is incomplete even
    // when its font looks authoritative. Keep it review-only; never fill holes
    // from stale PDF metadata or promote it to a filename.
    bool unreliableHeadline=false;
    for (const auto& row : firstPageLines)
        if (row.unreliable && row.fontSize>=12 && row.y<firstPageHeight*0.62 &&
            !TitleExtraction::isExcluded(row.text)) unreliableHeadline=true;
    if (unreliableHeadline) {
        const QString uncertain=evidence.take("title").toString();
        if (Bibliography::credibleTitle(uncertain)) evidence["titleCandidate"]=uncertain;
        evidence.remove("titleSource");
        evidence["titleStatus"]=QStringLiteral("needs_review");
        evidence["titleGlyphStatus"]=QStringLiteral("unmapped");
    }
    const QString terminalAuthor=Bibliography::firstPageLastAuthor(first);
    const QString blockAuthor=Bibliography::heliyonLastAuthor(first);
    const QString author=!terminalAuthor.isEmpty() ? terminalAuthor : blockAuthor;
    if(!author.isEmpty()) {
        evidence["authors"]=author;
        evidence["authorEvidence"]=!terminalAuthor.isEmpty()
            ? QStringLiteral("first-page-terminal-and") : QStringLiteral("publisher-bounded-author-block");
    }
    if(Metadata::acceptableIdentity(candidates)) {
        evidence["doi"]=candidates.first().doi;
        evidence["doiSource"]=candidates.first().location;
        evidence["doiScore"]=candidates.first().score;
    } else if(!candidates.isEmpty()) {
        evidence["doiStatus"]=QStringLiteral("needs_review");
        evidence["doiCandidates"]=QStringList{candidates.first().doi};
    }
    return evidence;
}
}
