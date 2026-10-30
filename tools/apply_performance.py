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


if __name__ == '__main__':
    apply(Path(sys.argv[1]))
