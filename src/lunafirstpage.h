// SPDX-License-Identifier: MIT
#pragma once
#include "lunabibliography.h"
#include "pdfdocument.h"
#include "pdftextlayoutgenerator.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfoptionalcontent.h"
#include <algorithm>

namespace luna {
// Runs on an owned document snapshot and worker-local PDF4QT caches. No viewer
// pointers survive a tab close, document replacement or library destruction.
inline QVariantMap extractFirstPage(pdf::PDFDocument& document, QString* diagnostic = nullptr) {
    const auto* catalog = document.getCatalog();
    if (!catalog || catalog->getPageCount() < 1) return {};
    const auto* page = catalog->getPage(0);
    if (!page) return {};
    pdf::PDFFontCache fonts(128, 128);
    fonts.setDocument(pdf::PDFModifiedDocument(&document, nullptr));
    pdf::PDFCMSManager manager(nullptr);
    manager.setDocument(&document);
    auto cms = manager.getCurrentCMS();
    pdf::PDFICCProfileCache profiles;
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    // PDFTextLayoutGenerator emits text positions in PDF page space. Its device
    // matrix does not transform text glyph positions; map those explicitly below.
    pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::getDefaultFeatures(), page,
        &document, &fonts, &profiles, cms.data(), &activity,
        QTransform(), pdf::PDFMeshQualitySettings());
    generator.processContents();
    const auto layout = generator.createTextLayout();
    struct Line { double x, y; QString text; };
    std::vector<Line> lines;
    const QTransform visual(1, 0, 0, -1, 0, page->getMediaBox().height());
    for (const auto& block : layout.getTextBlocks())
        for (const auto& line : block.getLines()) {
            QString text;
            const auto& characters = line.getCharacters();
            for (size_t i = 0; i < characters.size(); ++i) {
                const auto& current = characters[i];
                if (i && !current.character.isSpace()) {
                    const auto& previous = characters[i - 1];
                    if (!previous.character.isSpace() &&
                        QLineF(previous.position, current.position).length() > previous.advance * 1.2)
                        text += QLatin1Char(' ');
                }
                text += current.character;
            }
            const QPointF position = visual.map(line.getTopLeft());
            lines.push_back({position.x(), position.y(), text});
        }
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    QString text;
    for (const auto& line : lines) {
        text += line.text + QLatin1Char('\n');
        if (text.size() >= 32000) break;
    }
    if (diagnostic) *diagnostic = text;
    const auto headings = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < headings.size() && i < 16; ++i)
        if (headings[i].contains(QStringLiteral("Supporting Information"), Qt::CaseInsensitive) ||
            headings[i].contains(QStringLiteral("Supplementary Information"), Qt::CaseInsensitive))
            return {{"documentKind", QStringLiteral("supporting_information")}};
    QVariantMap evidence = Bibliography::firstPageCitation(text);
    if (evidence.isEmpty()) return {}; // Includes SI: no own publication evidence.
    const auto identity = Bibliography::firstPageIdentity(text);
    if (identity.contains("title")) evidence["title"] = identity.value("title");
    const auto author = Bibliography::firstPageLastAuthor(text);
    if (!author.isEmpty()) {
        evidence["authors"] = author;
        evidence["authorEvidence"] = QStringLiteral("first-page-terminal-and");
    }
    return evidence;
}
}
