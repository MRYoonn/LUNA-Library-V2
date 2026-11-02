// SPDX-License-Identifier: MIT
#pragma once
// Offline, first-page-only title candidate analysis for scientific PDF layouts.
// No PDF content, filenames, user notes or candidates leave this machine.
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace luna::TitleExtraction {
struct Line {
    QString text;
    double x = 0, y = 0, fontSize = 0;
};

inline bool isExcluded(QString text) {
    text = text.simplified();
    if (text.size() < 9 || text.size() > 230 ||
        text.contains(QStringLiteral("doi.org"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("http"), Qt::CaseInsensitive) ||
        text.contains(QLatin1Char('@')) ||
        text.contains(QStringLiteral("All publication charges"), Qt::CaseInsensitive))
        return true;
    static const QRegularExpression bad(QString::fromUtf8(
        R"(^(?:(?:research|review|original|full|open access)\s+(?:article|paper)|(?:article|abstract|introduction|keywords?|contents|references|communication|letters?|perspectives?|editorial|supplementary|supporting|revised supporting|table of contents|graphical abstract|copyright|©|published|received|accepted|correspondence|author contributions|funding|acknowledg(?:e)?ments|view article online|cite this|journal homepage|volume|issue|article in press)\b|(?:www\.|doi:|10\.\d{4,9}/)|(?:journal of the american chemical society|chemical science|nature communications|organic letters|angewandte chemie|acs catalysis)$))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression affiliation(QString::fromUtf8(
        R"(\b(?:department of|university|institution|institute of|faculty of|school of|laboratory of|research center)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression authorList(QString::fromUtf8(
        R"((?:,\s*[\p{Lu}][\p{Ll}\p{M}.'’-]+\s+[\p{Lu}][\p{Ll}\p{M}.'’-]+.*(?:\band\b|&)|\b[\p{Lu}][\p{Ll}\p{M}.'’-]+\s+[\p{Lu}][\p{Ll}\p{M}.'’-]+\s*,\s*[\p{Lu}]))"));
    return bad.match(text).hasMatch() || affiliation.match(text).hasMatch() ||
           authorList.match(text).hasMatch();
}

inline bool isSupportingInformation(const QList<Line>& lines) {
    // Supplementary covers often quote the parent article's DOI and title.
    // Never attribute that DOI or a guessed title to the SI PDF itself.
    for (int i=0; i<lines.size() && i<18; ++i)
        if (lines[i].text.contains(QStringLiteral("Supporting Information"), Qt::CaseInsensitive) ||
            lines[i].text.contains(QStringLiteral("Supplementary Information"), Qt::CaseInsensitive))
            return true;
    return false;
}

// Return "title" only when layout evidence is strong and unambiguous.
// Lower-confidence proposals are review-only; no remote title search here.
inline QVariantMap infer(const QList<Line>& source, double pageHeight) {
    if (source.isEmpty() || pageHeight < 100 || isSupportingInformation(source)) return {};
    QList<Line> lines;
    QList<double> sizes;
    for (const Line& raw : source) {
        const QString clean = raw.text.simplified();
        if (clean.isEmpty() || raw.y < 10 || raw.y > pageHeight*0.75 ||
            raw.fontSize < 5 || raw.fontSize > 72) continue;
        lines.append({clean,raw.x,raw.y,raw.fontSize});
        // The page's ordinary body/author font gives a relative baseline.
        if (raw.y < pageHeight*0.75 && raw.fontSize < 32)
            sizes.append(raw.fontSize);
    }
    if (lines.size() < 3 || sizes.size() < 3) return {};
    std::sort(lines.begin(),lines.end(),[](const Line& a,const Line& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    std::sort(sizes.begin(),sizes.end());
    const double baseline = sizes[sizes.size()/2];
    if (baseline <= 0) return {};
    struct Proposal {QString title;int score;double font;};
    QList<Proposal> proposals;
    for (int i=0;i<lines.size() && i<75;++i) {
        const Line& start = lines[i];
        if (start.fontSize < qMax(12.0,baseline*1.26) ||
            start.y > pageHeight*0.62 || isExcluded(start.text)) continue;
        QString title = start.text;
        int end = i;
        for (int j=i+1;j<lines.size() && j<=i+3;++j) {
            const Line& next = lines[j],&prev = lines[j-1];
            const double dy = next.y-prev.y;
            if (dy < 3 || dy > qMax(33.0,start.fontSize*2.65) ||
                std::abs(next.x-start.x) > 145 ||
                next.fontSize < start.fontSize*0.79 ||
                next.fontSize > start.fontSize*1.23 ||
                isExcluded(next.text) || title.size()+next.text.size()>230) break;
            title += (title.endsWith(QLatin1Char('-')) ? QString() : QStringLiteral(" ")) + next.text;
            end=j;
        }
        if (isExcluded(title) || title.size() < 18 ||
            title.split(QLatin1Char(' '),Qt::SkipEmptyParts).size()<3) continue;
        const double relative = start.fontSize/baseline;
        int score = 60 + int(qMin(20.0,(relative-1.0)*25.0)) +
                    (start.y < pageHeight*0.5 ? 8 : 0) +
                    (title.size() >= 28 && title.size() <= 190 ? 7 : 0) +
                    (end>i ? 5 : 0);
        proposals.append({title,qMin(score,100),start.fontSize});
    }
    if (proposals.isEmpty()) return {};
    std::sort(proposals.begin(),proposals.end(),[](const Proposal& a,const Proposal& b) {
        return a.score!=b.score ? a.score>b.score : a.font>b.font;
    });
    const auto& best=proposals.first();
    // A similarly prominent DIFFERENT heading means the page is ambiguous.
    for (int i=1;i<proposals.size();++i) {
        const auto& other=proposals[i];
        if (other.score < best.score-8) break;
        if (other.title!=best.title &&
            !other.title.startsWith(best.title) &&
            !best.title.startsWith(other.title) &&
            !other.title.endsWith(best.title) &&
            !best.title.endsWith(other.title))
            return {{"titleCandidate",best.title},{"titleConfidence",best.score},
                    {"titleStatus",QStringLiteral("needs_review")}};
    }
    if (best.score < 86 || best.font < 13.0)
        return {{"titleCandidate",best.title},{"titleConfidence",best.score},
                {"titleStatus",QStringLiteral("needs_review")}};
    return {{"title",best.title},{"titleSource",QStringLiteral("first_page_layout")},
            {"titleConfidence",best.score},{"titleStatus",QStringLiteral("verified_layout")}};
}
} // namespace luna::TitleExtraction
