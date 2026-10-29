// SPDX-License-Identifier: MIT
#pragma once
#include "lunaannotationtools.h"
#include "lunareadertoolbar.h"
#include <QFrame>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>
#include <QFont>
#include <cmath>
#include <map>
#include <set>

namespace luna {
class NoteText final : public QPlainTextEdit {
public:
    NoteText(pdfviewer::PDFEditorMainWindow& editor, QWidget* parent) : QPlainTextEdit(parent), editor_(editor) { setUndoRedoEnabled(false); }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        const char* name = event->matches(QKeySequence::Undo) ? "actionUndo" : event->matches(QKeySequence::Redo) ? "actionRedo" : nullptr;
        if (name) { if (auto* action = editor_.findChild<QAction*>(name); action && action->isEnabled()) action->trigger(); event->accept(); return; }
        QPlainTextEdit::keyPressEvent(event);
    }
private:
    pdfviewer::PDFEditorMainWindow& editor_;
};

// Native annotations remain the only note data. Cards are transient viewport
// widgets; /C, /Contents and /Open go through PDF4QT's normal undo/save path.
class StickyNotes final : public QObject {
    struct Card { QFrame* frame; NoteText* text; QToolButton* fold; QToolButton* color; QLabel* heading; QString appearance; pdf::PDFInteger page; };
public:
    explicit StickyNotes(pdfviewer::PDFEditorMainWindow& editor) : QObject(&editor), editor_(editor) {
        setObjectName("LunaStickyNotesController");
        auto* surface = editor.getProgramController()->getPdfWidget()->getDrawWidget()->getWidget();
        layer_ = new QScrollArea(surface); layer_->setObjectName("LunaStickyNotes"); layer_->setWidgetResizable(true);
        layer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layer_->setFrameShape(QFrame::NoFrame);
        auto* contents = new QWidget(layer_); row_ = new QVBoxLayout(contents); row_->setContentsMargins(2,2,2,2); row_->setSpacing(6); row_->addStretch();
        layer_->setWidget(contents); layer_->hide();
        layer_->setStyleSheet("QScrollArea#LunaStickyNotes, QScrollArea#LunaStickyNotes > QWidget > QWidget {background:transparent;}");
        connect(&poll_, &QTimer::timeout, this, [this] { synchronize(); }); poll_.start(100);
    }
    void focus(pdf::PDFObjectReference ref) {
        synchronize();
        if (auto card=cards_.find(ref); card!=cards_.end())
            for (const auto& item:editor_.getProgramController()->getPdfWidget()->getAnnotationManager()->getPageAnnotations(card->second.page).annotations)
                if (item.annotation->getSelfReference()==ref && item.annotation->getType()==pdf::AnnotationType::Text && !static_cast<const pdf::PDFTextAnnotation*>(item.annotation.get())->isOpen()) {
                    mutate(ref, [](pdf::PDFDocumentBuilder* builder,pdf::PDFObjectReference r) { builder->setAnnotationOpenState(r,true); });
                    synchronize(); break;
                }
        if (auto it = cards_.find(ref); it != cards_.end()) { layer_->ensureWidgetVisible(it->second.frame); it->second.text->setFocus(); }
    }
private:
    bool mutate(pdf::PDFObjectReference reference, const std::function<void(pdf::PDFDocumentBuilder*,pdf::PDFObjectReference)>& edit) {
        auto* c = editor_.getProgramController(); auto* m = c->getPdfWidget()->getAnnotationManager();
        if (!c->getDocument() || !c->canClose() || !editor_.isEnabled() || !m->isModificationAllowed()) return false;
        bool allowed = false;
        for (pdf::PDFInteger p = 0; p < pdf::PDFInteger(c->getDocument()->getCatalog()->getPageCount()) && !allowed; ++p)
            for (const auto& item : m->getPageAnnotations(p).annotations)
                if (item.annotation->getSelfReference() == reference && item.annotation->getType() == pdf::AnnotationType::Text) allowed = m->canModifyAnnotation(item);
        if (!allowed) return false;
        pdf::PDFDocumentModifier modifier(c->getDocument()); modifier.markAnnotationsChanged();
        edit(modifier.getBuilder(), reference); modifier.getBuilder()->updateAnnotationAppearanceStreams(reference);
        if (!modifier.finalize()) return false;
        m->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags())); return true;
    }
    Card create(pdf::PDFObjectReference reference, pdf::PDFInteger page) {
        auto* frame = new QFrame(layer_->widget()); frame->setObjectName("LunaStickyCard");
        auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(8,6,8,6); layout->setSpacing(4);
        auto* header = new QHBoxLayout; auto* heading = new QLabel(frame); heading->setTextFormat(Qt::PlainText); header->addWidget(heading,1);
        auto* color = new QToolButton(frame); color->setObjectName("LunaStickyColor"); color->setIcon(readerIcon("note")); color->setIconSize({18,18});
        color->setToolTip(QStringLiteral("이 메모지 색상 변경")); color->setAccessibleName(color->toolTip()); color->setPopupMode(QToolButton::InstantPopup);
        auto* palette = new QMenu(color); color->setMenu(palette);
        connect(palette, &QMenu::aboutToShow, palette, [this, palette, reference] {
            palette->clear(); addAnnotationColors(palette, "reader/noteColor", [this, reference](QColor value) {
                mutate(reference, [value](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference r) { builder->setAnnotationColor(r, value); }); synchronize();
            });
        });
        auto* fold = new QToolButton(frame); fold->setObjectName("LunaStickyFold"); fold->setIconSize({18,18});
        header->addWidget(color); header->addWidget(fold); layout->addLayout(header);
        auto* text = new NoteText(editor_, frame); text->setObjectName("LunaStickyContents"); text->setAcceptDrops(false);
        text->setPlaceholderText(QStringLiteral("메모 내용")); text->setFont(QFont(QStringLiteral("Segoe UI"),10)); text->setMinimumHeight(80); text->setMaximumHeight(180);
        layout->addWidget(text);
        connect(text, &QPlainTextEdit::textChanged, this, [this, text, reference] {
            const QString value = text->toPlainText();
            mutate(reference, [value](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference r) { builder->setAnnotationContents(r, value); });
        });
        connect(fold, &QToolButton::clicked, this, [this, reference, text] {
            const bool open = !text->isVisible();
            mutate(reference, [open](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference r) { builder->setAnnotationOpenState(r, open); }); synchronize();
        });
        row_->insertWidget(row_->count()-1, frame); return {frame, text, fold, color, heading, {}, page};
    }
    void synchronize() {
        auto* c = editor_.getProgramController(); auto* widget = c->getPdfWidget(); const auto* doc = c->getDocument();
        // Match the native tab host's readiness boundary. A document pointer
        // can be set before annotation models and load callbacks are settled.
        if (!editor_.isEnabled() || !c->canClose()) return;
        const QString path = c->getOriginalFileName();
        if (!doc || path != path_) {
            for (auto& pair : cards_) delete pair.second.frame;
            cards_.clear(); path_ = path;
        }
        if (!doc) { layer_->hide(); return; }
        const auto snapshot = widget->getDrawWidgetProxy()->getSnapshot();
        // Layout can be momentarily empty while the native annotation manager
        // replaces its document. Keep the same editors and their focus then.
        if (snapshot.items.empty()) return;
        std::set<pdf::PDFObjectReference> visible;
        int totalHeight = 4;
        for (const auto& page : snapshot.items) {
            for (const auto& item : widget->getAnnotationManager()->getPageAnnotations(page.pageIndex).annotations) {
                if (item.annotation->getType() != pdf::AnnotationType::Text || !widget->getAnnotationManager()->isAnnotationSelectable(item)) continue;
                const auto reference = item.annotation->getSelfReference(); visible.insert(reference);
                if (!cards_.contains(reference)) cards_.emplace(reference, create(reference,page.pageIndex));
                auto& card = cards_.at(reference); const auto* note = static_cast<const pdf::PDFTextAnnotation*>(item.annotation.get());
                const bool editable = widget->getAnnotationManager()->canModifyAnnotation(item) && editor_.isEnabled() && c->canClose();
                card.text->setReadOnly(!editable); card.color->setEnabled(editable); card.fold->setEnabled(editable);
                const QString value = note->getContents();
                if (card.text->toPlainText() != value) { QSignalBlocker block(card.text); const int position = card.text->textCursor().position(); card.text->setPlainText(value); auto cursor = card.text->textCursor(); cursor.setPosition(qMin(position, int(value.size()))); card.text->setTextCursor(cursor); }
                const QColor color = note->getColor().empty() ? QColor("#fff2a6") : note->getDrawColorFromAnnotationColor(note->getColor(),1.0);
                auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v+.055)/1.055,2.4); };
                const double luminance = .2126*linear(color.redF()) + .7152*linear(color.greenF()) + .0722*linear(color.blueF());
                const QString foreground = luminance > .179 ? "#000000" : "#ffffff";
                const QString appearance = color.name() + foreground;
                if (appearance != card.appearance) {
                    card.frame->setStyleSheet(QString("QFrame#LunaStickyCard{background:%1;border:1px solid #b8bfcc;border-radius:6px;} QLabel,QPlainTextEdit,QToolButton{color:%2;background:transparent;} QPlainTextEdit{border:0;} QToolButton{border:0;padding:2px;}").arg(color.name(),foreground));
                    card.color->setIcon(readerIcon("note",QColor(foreground))); card.appearance = appearance;
                }
                card.heading->setText(QStringLiteral("메모 · %1 페이지").arg(page.pageIndex+1));
                card.text->setVisible(note->isOpen()); card.fold->setIcon(readerIcon(note->isOpen() ? "previous" : "next",QColor(foreground)));
                card.fold->setToolTip(note->isOpen() ? QStringLiteral("메모 접기") : QStringLiteral("메모 펼치기")); card.fold->setAccessibleName(card.fold->toolTip());
                card.frame->show(); totalHeight += note->isOpen() ? 150 : 36;
            }
        }
        for (auto it = cards_.begin(); it != cards_.end();) {
            bool exists = false;
            if (it->second.page >= 0 && it->second.page < pdf::PDFInteger(doc->getCatalog()->getPageCount()))
                for (const auto& item : widget->getAnnotationManager()->getPageAnnotations(it->second.page).annotations)
                    exists |= item.annotation->getSelfReference() == it->first && item.annotation->getType() == pdf::AnnotationType::Text;
            if (!exists) { delete it->second.frame; it = cards_.erase(it); }
            else { it->second.frame->setVisible(visible.contains(it->first)); ++it; }
        }
        const QSize size = layer_->parentWidget()->size();
        layer_->setGeometry(qMax(0,size.width()-250),4,qMin(246,size.width()),qMax(0,qMin(totalHeight,size.height()-8)));
        layer_->setVisible(!visible.empty()); if (!visible.empty()) layer_->raise();
    }
    pdfviewer::PDFEditorMainWindow& editor_;
    QScrollArea* layer_; QVBoxLayout* row_; QTimer poll_; QString path_;
    std::map<pdf::PDFObjectReference,Card> cards_;
};
inline void focusStickyNote(pdfviewer::PDFEditorMainWindow& window, pdf::PDFObjectReference reference) {
    if (auto* notes = dynamic_cast<StickyNotes*>(window.findChild<QObject*>("LunaStickyNotesController"))) notes->focus(reference);
}
}
