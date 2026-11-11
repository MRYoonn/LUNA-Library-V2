// SPDX-License-Identifier: MIT
// Explicit read-only rendering probe. Normal application startup is unchanged.
#pragma once
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfpainter.h"
#include "pdfblpainter.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QTimer>

namespace luna {
inline bool compareGridEngines(pdf::PDFDocument& document, const QString& output, double zoom) {
    pdf::PDFFontCache fonts(128,128);
    fonts.setDocument(pdf::PDFModifiedDocument(&document,nullptr));
    pdf::PDFICCProfileCache profiles;
    profiles.setDocument(&document);
    pdf::PDFCMSManager manager(nullptr);
    manager.setDocument(&document);
    auto cms = manager.getCurrentCMS();
    pdf::PDFOptionalContentActivity activity(&document,pdf::OCUsage::View,nullptr);
    auto features = pdf::PDFRenderer::getDefaultFeatures();
    pdf::PDFRenderer renderer(&document,&fonts,&profiles,cms.data(),&activity,features,pdf::PDFMeshQualitySettings());
    const auto* page = document.getCatalog()->getPage(0);
    const auto size = (page->getRotatedMediaBox().size()*zoom).toSize();
    const auto matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page,QRectF(QPointF(),size));
    pdf::PDFPrecompiledPage compiled;
    renderer.compile(&compiled,0);
    for (const bool direct : {false,true}) for (const bool blend : {false,true}) {
        QImage image(size,QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        const QString name = (direct ? "direct-" : "compiled-") + QString(blend ? "blend" : "qt");
        auto paint = [&](QPainter& painter) {
            painter.fillRect(QRect(QPoint(),size),Qt::white);
            if (direct) renderer.render(&painter,matrix,0);
            else compiled.draw(&painter,page->getCropBox(),matrix,features,1.0);
        };
        if (blend) {
            pdf::PDFBLPaintDevice device(image,false);
            QPainter painter(&device); paint(painter);
        } else { QPainter painter(&image); paint(painter); }
        if (!image.save(output+"."+name+".png")) return false;
    }
    return true;
}

inline void startGridProbe(pdfviewer::PDFEditorMainWindow& window, const QString& output) {
    bool zoomOk = false;
    const double zoom = qEnvironmentVariable("LUNA_GRID_ZOOM").toDouble(&zoomOk);
    if (!zoomOk || (zoom!=1.0 && zoom!=1.5 && zoom!=2.0)) {
        QTimer::singleShot(0,qApp,[]{qApp->exit(2);}); return;
    }
    window.resize(1366,768); window.move(16,16); window.raise(); window.activateWindow();
    auto* timer = new QTimer(&window);
    timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,&window,
        [&window,output,zoom,timer,attempt=0,phase=0,observed=QTransform(),stable=QElapsedTimer()]() mutable {
        if (++attempt>300) { timer->stop(); qApp->exit(21); return; }
        auto* controller = window.getProgramController();
        auto* document = controller->getDocument();
        if (!document || !controller->canClose() || document->getCatalog()->getPageCount()<1) return;
        auto* pdfWidget = controller->getPdfWidget();
        auto* proxy = pdfWidget->getDrawWidgetProxy();
        auto* widget = pdfWidget->getDrawWidget()->getWidget();
        if (phase==0) {
            proxy->setPageLayout(pdf::PageLayout::OneColumn); proxy->zoom(zoom); proxy->goToPage(0);
            phase=1; return;
        }
        const auto snapshot=proxy->getSnapshot();
        const auto* page=snapshot.getPageSnapshot(0);
        if (!page || qAbs(proxy->getZoom()-zoom)>.0001 ||
            !proxy->getCompiler()->getCompiledPage(0,pdf::PDFAsynchronousPageCompiler::CompileMode::None)) return;
        if (!stable.isValid() || observed!=page->pageToDeviceMatrix) {
            observed=page->pageToDeviceMatrix; stable.restart(); return;
        }
        if (stable.elapsed()<500) return;
        timer->stop(); widget->repaint(); QGuiApplication::sync();
        const QPoint offset=widget->mapTo(&window,QPoint());
        const auto m=observed;
        const QPixmap native=window.screen()->grabWindow(window.winId());
        qInfo() << "GRID_NATIVE_CAPTURE" << native.size() << "expected" << window.size()*window.devicePixelRatioF();
        if (native.isNull() || native.size()!=window.size()*window.devicePixelRatioF() ||
            !native.save(output+".native.png") || !window.grab().save(output+".png") ||
            !window.screen()->grabWindow(0).save(output+".desktop.png")) { qApp->exit(22); return; }
        QJsonObject result{{"zoom",proxy->getZoom()}, {"devicePixelRatio",window.devicePixelRatioF()},
            {"renderer",int(proxy->getRendererEngine())}, {"nativeWindowCapture",true},
            {"compiledReady",true},
            {"blendBackend",proxy->getRendererEngine()==pdf::RendererEngine::Blend2D_MultiThread ||
                proxy->getRendererEngine()==pdf::RendererEngine::Blend2D_SingleThread},
            {"windowSize",QJsonArray{window.width(),window.height()}},
            {"pageToWindow",QJsonArray{m.m11(),m.m12(),m.m21(),m.m22(),m.dx()+offset.x(),m.dy()+offset.y()}}};
        const bool compare=qEnvironmentVariableIsSet("LUNA_GRID_COMPARE_ENGINES");
        if (compare && !compareGridEngines(*document,output,zoom)) { qApp->exit(23); return; }
        result.insert("comparedDirectAndCompiledQtAndBlend",compare);
        QSaveFile file(output);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson())<0 || !file.commit()) {
            qApp->exit(24); return;
        }
        qApp->exit(0);
    });
    timer->start();
}
}
