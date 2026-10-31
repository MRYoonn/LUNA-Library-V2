"""Instrument the pinned engine without moving GUI work across threads."""
import sys
from pathlib import Path


def apply(root):
    header = (Path(__file__).resolve().parents[1] / 'src/lunaperformance.h').read_bytes()
    def patch(relative, replacements):
        path = root / relative
        source = path.read_text(encoding='utf-8')
        for old, new in replacements:
            if source.count(old) != 1:
                raise ValueError(f'Non-unique performance anchor: {relative}: {old[:80]}')
            source = source.replace(old, new, 1)
        path.write_text('#include "lunaperformance.h"\n' + source, encoding='utf-8')
        (path.parent / 'lunaperformance.h').write_bytes(header)
    def span(signature, name):
        return (signature + '\n{', signature + '\n{\n    luna::PerformanceSpan lunaSpan("' + name + '");')
    patch('Pdf4QtLibGui/pdfprogramcontroller.h', [
        ('    struct AsyncReadingResult\n    {', '    struct AsyncReadingResult\n    {\n        qint64 lunaWorkerFinished = 0;'),
    ])
    patch('Pdf4QtLibGui/pdfprogramcontroller.cpp', [
        span('void PDFProgramController::openDocument(const QString& fileName)', 'controller-dispatch'),
        span('void PDFProgramController::onDocumentReadingFinished()', 'document-layout-and-install'),
        span('void PDFProgramController::readSettings(Settings settingsFlags)', 'controller-settings'),
        ('    auto readDocument = [this, fileName]() -> AsyncReadingResult',
         '    const auto queuedAt = luna::performanceNow();\n    auto readDocument = [this, fileName, queuedAt]() -> AsyncReadingResult'),
        ('        AsyncReadingResult result;', '        luna::performanceEvent("reader-worker-queue", queuedAt);\n        AsyncReadingResult result;'),
        ('        return result;\n    };\n    m_future = QtConcurrent::run(readDocument);',
         '        result.lunaWorkerFinished = luna::performanceNow();\n        return result;\n    };\n    m_future = QtConcurrent::run(readDocument);'),
        ('    AsyncReadingResult result = m_future.result();',
         '    AsyncReadingResult result = m_future.result();\n    luna::performanceEvent("worker-to-gui-delivery", result.lunaWorkerFinished);'),
        ('            result.signatures = pdf::PDFSignatureHandler::verifySignatures(form, reader.getSource(), parameters);',
         '            const auto signatureStart = luna::performanceNow();\n            result.signatures = pdf::PDFSignatureHandler::verifySignatures(form, reader.getSource(), parameters);\n            luna::performanceEvent("signature-verification", signatureStart);'),
        ('\n        pdf::PDFDocument document = reader.readFromFile(fileName);',
         '\n        const auto parseStart = luna::performanceNow();\n        pdf::PDFDocument document = reader.readFromFile(fileName);\n        luna::performanceEvent("pdf-parse", parseStart);'),
    ])
    patch('Pdf4QtLibCore/sources/pdfdocumentreader.cpp', [
        ('            return readFromBuffer(device->readAll());',
         '            const auto ioStart = luna::performanceNow();\n            const QByteArray bytes = device->readAll();\n            luna::performanceEvent("pdf-file-read", ioStart);\n            return readFromBuffer(bytes);'),
    ])
    patch('Pdf4QtLibGui/pdftexttospeech.cpp', [span('void PDFTextToSpeech::setSettings(const PDFViewerSettings* viewerSettings)', 'speech-engine-settings')])
    patch('Pdf4QtLibCore/sources/pdfcms.cpp', [span('PDFCMSSettings PDFCMSManager::getDefaultSettings() const', 'color-profile-defaults')])
    patch('Pdf4QtLibWidgets/sources/pdfdrawspacecontroller.cpp', [
        ('QColor backgroundColor = darkScheme ? QColor(34, 34, 34) : QColor(Qt::lightGray);',
         'QColor backgroundColor = darkScheme ? QColor(34, 34, 34) : QColor("#DFE5ED");'),
    ])
    patch('Pdf4QtLibWidgets/sources/pdfdrawwidget.cpp', [
        ('void PDFDrawWidget::resizeEvent(QResizeEvent* event)', 'void PDFDrawWidget::resizeEvent(QResizeEvent* event)'),
        ('\n}\n\nvoid PDFDrawWidget::resizeEvent', '''
    if (property("LunaOpenStart").isValid() && isVisible()) {
        auto* proxy = getPDFWidget()->getDrawWidgetProxy();
        for (auto page : proxy->getPagesIntersectingRect(rect())) {
            if (proxy->getCompiler()->getCompiledPage(page, PDFAsynchronousPageCompiler::CompileMode::None)) {
                luna::performanceEvent("request-to-first-page-paint", property("LunaOpenStart").toLongLong());
                setProperty("LunaOpenStart", QVariant());
                break;
            }
        }
    }
}

void PDFDrawWidget::resizeEvent'''),
    ])

    # The pinned PDF4QT text selector draws each independent PDF text run as
    # a separate blue rectangle. Scientific PDFs split one *visual* sentence
    # into many runs, exposing gaps even where the drag selected every word.
    # Paint adjoining runs as one contiguous on-screen band. Text extraction,
    # selection endpoints and stored annotations remain unchanged.
    import hashlib
    selection_path = root / 'Pdf4QtLibCore/sources/pdftextlayout.cpp'
    raw = selection_path.read_bytes()
    normalized = raw.replace(b'\r\n', b'\n')
    blob = hashlib.sha1(b'blob ' + str(len(normalized)).encode() + b'\0' + normalized).hexdigest()
    if blob != 'a50aeb3ca241f41bd54c4d9d49bd5682f1bed2b8':
        raise ValueError('Unrecognized pinned PDFTextLayout source; refuse selection display patch')
    source = normalized.decode('utf-8')
    if source.count('#include <limits>') != 1:
        raise ValueError('PDFTextLayout standard-header anchor mismatch')
    source = source.replace('#include <limits>', '#include <algorithm>\n#include <limits>', 1)
    begin = source.index('void PDFTextSelectionPainter::draw(')
    finish = source.index('QPainterPath PDFTextSelectionPainter::prepareGeometry(', begin)
    old = source[begin:finish]
    if old.count('painter->drawPath(path);') != 1 or old.count('block.getCharacterRangeBoundingPath') != 1:
        raise ValueError('PDFTextSelectionPainter draw anchor mismatch')
    painted = r'''void PDFTextSelectionPainter::draw(QPainter* painter,
                                   PDFInteger pageIndex,
                                   PDFTextLayoutGetter& textLayoutGetter,
                                   const QTransform& matrix,
                                   const PDFColorConvertor& convertor)
{
    Q_ASSERT(painter);
    auto it = m_selection->begin(pageIndex);
    const auto itEnd = m_selection->end(pageIndex);
    if (it == itEnd) return;
    painter->save();

    struct LunaSelectionBand { QRectF rect; QColor color; };
    QList<LunaSelectionBand> fragments;
    const PDFTextLayout& layout = textLayoutGetter;
    const PDFTextBlocks& blocks = layout.getTextBlocks();
    for (; it != itEnd; ++it)
    {
        const PDFTextSelectionColoredItem& item = *it;
        const PDFCharacterPointer& start = item.start;
        const PDFCharacterPointer& end = item.end;
        if (start.blockIndex >= blocks.size() || start.pageIndex != end.pageIndex ||
            start.blockIndex != end.blockIndex) continue;
        const PDFTextBlock& block = blocks[start.blockIndex];
        const QPainterPath path =
            block.getCharacterRangeBoundingPath(start, end, matrix, HEIGHT_INCREASE_FACTOR);
        // Rotated/scaled PDF page matrices and rotated text keep their exact
        // upstream shape, avoiding false bands through tables and figures.
        if (qAbs(block.getAngle()) > 0.5 || qAbs(matrix.m12()) > 0.001 ||
            qAbs(matrix.m21()) > 0.001)
        {
            QColor brush = item.color;
            brush.setAlphaF(SELECTION_ALPHA);
            painter->setPen(convertor.convert(QPen(item.color.darker())));
            painter->setBrush(convertor.convert(QBrush(brush, Qt::SolidPattern)));
            painter->drawPath(path);
            continue;
        }
        for (const QPolygonF& polygon : path.toSubpathPolygons())
        {
            const QRectF rect = polygon.boundingRect().normalized();
            if (rect.isValid() && rect.width() > 0.25 && rect.height() > 0.25)
                fragments.append({rect, item.color});
        }
    }
    std::sort(fragments.begin(), fragments.end(), [](const LunaSelectionBand& a, const LunaSelectionBand& b) {
        if (a.color.rgba() != b.color.rgba()) return a.color.rgba() < b.color.rgba();
        if (qAbs(a.rect.center().y() - b.rect.center().y()) > 0.8)
            return a.rect.center().y() < b.rect.center().y();
        return a.rect.left() < b.rect.left();
    });
    QList<LunaSelectionBand> bands;
    for (const auto& fragment : fragments)
    {
        bool joined = false;
        for (auto& previous : bands)
        {
            const qreal h = qMin(previous.rect.height(), fragment.rect.height());
            const qreal gap = qMax(0.0, qMax(previous.rect.left(), fragment.rect.left()) -
                qMin(previous.rect.right(), fragment.rect.right()));
            // Bridge ordinary inter-word gaps, never the much larger gutter
            // between two PDF columns or neighboring text rows.
            if (previous.color == fragment.color &&
                qAbs(previous.rect.center().y() - fragment.rect.center().y()) <= h * 0.28 &&
                qAbs(previous.rect.height() - fragment.rect.height()) <= h * 0.4 &&
                gap <= qMin(48.0, 2.1 * h))
            {
                previous.rect = previous.rect.united(fragment.rect);
                joined = true;
                break;
            }
        }
        if (!joined) bands.append(fragment);
    }
    painter->setPen(Qt::NoPen);
    for (const auto& band : bands)
    {
        QColor brush = band.color;
        brush.setAlphaF(SELECTION_ALPHA);
        painter->setBrush(convertor.convert(QBrush(brush, Qt::SolidPattern)));
        painter->drawRect(band.rect);
    }
    painter->restore();
}

'''
    selection_path.write_bytes(source[:begin].encode('utf-8') +
        painted.encode('utf-8') + source[finish:].encode('utf-8'))


if __name__ == '__main__':
    apply(Path(sys.argv[1]))
