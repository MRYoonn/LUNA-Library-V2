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
inline QVariantMap extractFirstPage(pdf::PDFDocument& document) {
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
    // Work in visual coordinates (top to bottom), independent of content-stream
    // and text-block order. Superscript fragments stay near their author row.
    pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::getDefaultFeatures(), page,
        &document, &fonts, &profiles, cms.data(), &activity,
        QTransform(1, 0, 0, -1, 0, page->getMediaBox().height()), pdf::PDFMeshQualitySettings());
    generator.processContents();
    const auto layout = generator.createTextLayout();
    struct Line { double x, y; QString text; };
    std::vector<Line> lines;
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
            lines.push_back({line.getTopLeft().x(), line.getTopLeft().y(), text});
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
