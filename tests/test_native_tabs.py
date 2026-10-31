"""Source contract checks only; none claim Windows runtime acceptance."""
from pathlib import Path
import ast
import unittest
from tools.smoke_windows import synthetic_pdf

ROOT = Path(__file__).resolve().parents[1]

class NativeTabsContract(unittest.TestCase):
    def test_synthetic_unique_words_preserve_xref(self):
        original = synthetic_pdf(2)
        for word in (b'alpha', b'bravo'):
            altered = original.replace(b'smoke', word)
            self.assertEqual(len(original), len(altered))
            self.assertIn(word, altered)
            self.assertEqual(original[original.index(b'xref\n'):], altered[altered.index(b'xref\n'):])

    def test_scripts_parse(self):
        for path in ('prepare.py', 'run_windows.py'):
            ast.parse((ROOT / 'experiments/native_tabs' / path).read_text(encoding='utf-8'))

    def test_workflow_remains_manual_and_proof_not_released(self):
        text = (ROOT / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
        self.assertIn('workflow_dispatch:', text)
        self.assertNotIn('  push:', text)
        self.assertNotIn('pull_request:', text)
        self.assertIn('native_tabs_proof:', text)
        self.assertIn("if: ${{ !inputs.native_tabs_proof && steps.engine-cache.outputs.cache-hit != 'true' }}", text)
        self.assertIn('Upload LUNA Windows portable artifact\n        if: ${{ !inputs.native_tabs_proof }}', text)

    def test_selection_first_highlight_and_freetext_contract(self):
        adapter = (ROOT / 'src/lunareadertoolbar.cpp').read_text(encoding='utf-8')
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        # pdfdrawspacecontroller.h only forward-declares the compiler.
        # pdfcompiler.h owns the complete type used by getTextLayoutLazy().
        self.assertIn('#include "pdfcompiler.h"', adapter)
        self.assertIn('page->getCropBox().normalized()', adapter)
        self.assertIn('rect.moveLeft(qBound(crop.left()', adapter)
        self.assertIn('rect.moveTop(qBound(crop.top()', adapter)
        for token in (
            'LunaHighlightSwatch%1', 'LunaEraseSelected', 'LunaFreeTextBox',
            'selected->getSelectedText().trimmed().isEmpty()',
            'PDFTextSelectionPainter painter(&selection)',
            'getHiglightArea().getQuadrilaterals()', 'createAnnotationHighlight(',
            'createAnnotationFreeText(', 'QRubberBand::Rectangle',
            'LunaFreeTextFont', 'LunaFreeTextSize',
            'textBox_->setChecked(false)', 'PDFDocumentModifier modifier',
            'isModificationAllowed()', 'c->getDocument() != originalDocument',
            'LunaFreeTextEditDialog', 'setAnnotationContents(annotationRef, nextText)',
            'LunaHighlightColor', 'annotations/highlightColor',
            'setFreeTextAnnotationStyle(annotationRef, style)',
        ):
            self.assertIn(token, adapter, token)
        for token in (
            'Reader phase: eight one-click highlight pens and recolor',
            'Selection-then-color did not write native highlight and return to SelectText',
            'Reader phase: recolor only selected overlap, preserve outer segments',
            'Partly overlapping recolor failed to retain old and new colors',
            'Recolor lost unselected old color or stacked alpha on new color',
            'Recolor Undo changed original saved highlight color or QuadPoints',
            'Expanded marker did not commit pink geometry beyond original green coverage',
            'Selection-first eraser did not preserve native undo',
            'Eraser click did not return to SelectText',
            'Selection-then-click eraser did not remove previous highlight',
            'Reader phase: eraser removes only chosen glyph range',
            'Eraser removed nonselected words from same highlight',
            'Partial eraser Undo did not restore original QuadPoints and color',
            'Reader phase: vertical middle-click autoscroll',
            'Escape did not cancel vertical autoscroll',
            'Reader phase: transparent on-page FreeText note',
            'LUNA default FreeText escaped PDF crop bounds',
            'FreeText note did not participate in native undo',
            'Reader phase: FreeText save and reopen',
            'persistedContentsFound',
            'LUNA FreeText save/reopen changed native annotation counts',
            'Reader phase: PowerPoint-style FreeText select and drag',
            'FreeText could not be dragged using text-selection mode',
            'Reader phase: PowerPoint-style FreeText resize handles',
            'FreeText corner handle did not resize the native PDF text box',
            'Reader phase: FreeText double-click edit',
            'LUNA FreeText double-click edit did not update the annotation',
            'visiblePageRect = visiblePage->rect.intersected(QRectF(widget->rect()))',
            'getPageUnderPoint(boxStart, &localBoxStart)',
            'getPageUnderPoint(boxEnd, &localBoxEnd)',
            'LUNA FreeText edit target offscreen: actual=',
        ):
            self.assertIn(token, smoke, token)
        self.assertNotIn('new QAction(readerIcon(QStringLiteral("highlight"))', adapter)
        self.assertNotIn('add("LunaHighlightSelected")', adapter)
        self.assertIn('add("LunaFreeTextBox")', adapter)
        self.assertIn('add("LunaEraseSelected")', adapter)
        self.assertIn('for (int i = 1; i <= 8; ++i)', adapter)
        self.assertIn('for (int i = 0; i < 8; ++i)', adapter)
        self.assertIn('void armHighlighter(const QColor& next)', adapter)
        self.assertIn('if (highlightArmed_) applyHighlight();', adapter)
        self.assertIn('setHighlightArmed(false);\n        selectText(); // Remain in ordinary native text selection after the click.', adapter)
        self.assertIn('selectionEnd_ = end;', adapter)
        self.assertNotIn('if (ready && highlightArmed_) applyHighlight();', adapter)
        self.assertIn('setAnnotationOpacity(ref, 0.36)', adapter)
        self.assertIn('setAnnotationQuadPoints(edit.ref, edit.remainder)', adapter)
        self.assertIn('const bool sameColor', adapter)
        self.assertIn('const auto& quads = old->getHiglightArea().getQuadrilaterals()', adapter)
        self.assertIn('modifier.getBuilder()->removeAnnotation(page->getPageReference(), edit.ref)', adapter)
        self.assertIn('bool beginTextBoxDrag(QMouseEvent* event)', adapter)
        self.assertIn('void finishTextBoxDrag(const QPoint& to)', adapter)
        self.assertIn('manager->setAnnotationRectangle(annotation, pageRect)', adapter)
        self.assertIn('manager->selectAnnotation(boxReference_, true)', adapter)
        self.assertIn('pageRect = pageRect.intersected(crop)', adapter)
        self.assertIn('boxPreview_->setGeometry(next.toAlignedRect())', adapter)
        self.assertNotIn('manager->mousePressEvent(draw_, event)', adapter)
        self.assertNotIn('manager->mouseMoveEvent(draw_, static_cast<QMouseEvent*>(event))', adapter)
        self.assertNotIn('manager->mouseReleaseEvent(draw_, static_cast<QMouseEvent*>(event))', adapter)
        self.assertIn('manager->isAnnotationSelected(item.annotation->getSelfReference())', adapter)
        # Removing one quad from a multi-line highlight must keep the
        # annotation and its untouched lines. Only remove the PDF annotation
        # entirely when no quadrilaterals remain.
        self.assertIn('getHiglightArea().getQuadrilaterals()', adapter)
        self.assertIn('setAnnotationQuadPoints(edit.ref, edit.remainder)', adapter)
        self.assertIn('updateAnnotationAppearanceStreams(edit.ref)', adapter)
        self.assertIn('if (edit.remainder.isEmpty())', adapter)
        self.assertIn('removeAnnotation(page->getPageReference(), edit.ref)', adapter)
        self.assertIn('for (const QRectF& erased : erasedBands)', adapter)
        self.assertIn('static QList<QRectF> coalesceTextBands(QList<QRectF> fragments)', adapter)
        self.assertIn('const QList<QRectF> joined = coalesceTextBands(std::move(bands));', adapter)
        self.assertIn('erasedBands = coalesceTextBands(std::move(erasedBands));', adapter)
        self.assertEqual(adapter.count('coalesceTextBands(std::move('), 2)
        self.assertIn('const qreal lo = erased.left() - 0.20', adapter)
        self.assertIn('const qreal snap = qMin(6.0, qMax(0.8, 0.48 * original.height()))', adapter)
        self.assertIn('overlap.width() >= 0.82 * original.width()', adapter)
        self.assertIn('if (fullQuad) {', adapter)
        self.assertIn('remainingX.clear();', adapter)
        self.assertIn('selectedChars=%4 remnants=%5', smoke)
        self.assertIn('markedBeforeErase = ann->getHiglightArea().getPath().boundingRect().normalized()', smoke)
        self.assertIn('const QPoint wholeStart =', smoke)
        self.assertIn('const QPoint wholeEnd =', smoke)
        self.assertIn('characters[lastMarkedGlyph].boundingBox.boundingRect().center()', smoke)
        self.assertIn('Recolor/Undo did not restore original green highlight before erase', smoke)
        self.assertIn('Could not find original saved highlight glyph range', smoke)
        self.assertIn('const QRectF now = ann->getHiglightArea().getPath().boundingRect().normalized();', smoke)
        self.assertIn('Expanded recolor Undo changed original highlight geometry', smoke)
        self.assertIn('Reader phase: expanded recolor waits for committed pink geometry', smoke)
        self.assertIn('auto expandedPinkCommitted = [&]()', smoke)
        self.assertIn('bounds.right() > initialGreenBounds.right() + 2.0', smoke)
        self.assertIn('qAbs(rgb[1] - 179.0 / 255.0) < 0.02', smoke)
        self.assertIn('if (!waitFor(expandedPinkCommitted))', smoke)
        self.assertNotIn('if (!waitFor([&] { return annotations(pdf::AnnotationType::Highlight) == 1; }))\n        return fail("Old plus new text was not recolored', smoke)
        self.assertIn('initialGreenBounds', smoke)
        self.assertIn('manager->canDeleteAnnotation(item)', adapter)
        self.assertIn('item.annotation->getType() != pdf::AnnotationType::Highlight', adapter)
        self.assertIn('LunaVerticalAutoscrollMarker', adapter)
        self.assertIn('if (mouse->button() == Qt::MiddleButton)', adapter)
        self.assertIn('scroll->setValue(qBound(scroll->minimum()', adapter)
        self.assertIn('autoscrollTimer_.setInterval(16)', adapter)
        self.assertIn('QPolygon down;', adapter)
        # The PDF4QT native tools stay intact for backwards-compatible Insert menus.
        self.assertIn('checked("actionCreateTextHighlight")', adapter)
        self.assertIn('checked("actionStickyNoteComment")', adapter)

    def test_saved_annotation_reopen_checks_real_pdf_and_not_ui_action_state(self):
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        for token in (
            'Ctrl+S did not persist the current annotated PDF copy',
            'Ctrl+S left the original annotated PDF bytes unchanged',
            'Reader phase: reopened file annotation state independent of selected tool',
            'Annotated copy failed to fully reopen after Ctrl+S',
            'controller->canClose() && !controller->getIsBusy()',
            'sameExistingFile(controller->getOriginalFileName(), saved)',
            'Deleted annotation returned after save and reopen: highlights=%1 notes=%2',
            'Text selection mode unavailable after reopening saved annotations',
        ):
            self.assertIn(token, smoke)
        section = smoke.split('checkpoint("Reader phase: reopened file annotation state independent of selected tool")', 1)[1]
        self.assertLess(section.index('if (annotations(pdf::AnnotationType::Highlight) != 0 ||'),
                        section.index('if (!select->isChecked()) select->trigger();'))
        self.assertNotIn('controller->getDocument() && select->isChecked()', section.split('if (!select->isChecked()) select->trigger();', 1)[0])

    def test_autoscroll_does_not_register_an_application_wide_mouse_filter(self):
        adapter = (ROOT / 'src/lunareadertoolbar.cpp').read_text(encoding='utf-8')
        tabs = (ROOT / 'src/lunatabs.cpp').read_text(encoding='utf-8')
        self.assertIn('draw_->installEventFilter(this);', adapter)
        self.assertNotIn('qApp->installEventFilter(this);', adapter)
        self.assertIn('if (!autoscrollMarker_) autoscrollMarker_ = new VerticalAutoscrollMarker(draw_);', adapter)
        self.assertIn('if (object != draw_) return false;', adapter)
        self.assertIn('native-shell-before-reader-bind', tabs)
        self.assertIn('native-editor-after-reader-prepare', tabs)

    def test_final_freetext_save_reopen_checks_disk_and_load_before_tool(self):
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        for token in (
            'Reader phase: FreeText persisted disk bytes, wait for actual reopen',
            'Ctrl+S did not invoke native FreeText save',
            'Native FreeText changes were not persisted by Ctrl+S',
            'LUNA FreeText persisted document did not reopen: document=%1 busy=%2',
            'Native text-selection tool unavailable after FreeText reopen',
            'readFile(saved) == preFreeTextSave',
            'controller->canClose() && !controller->getIsBusy()',
        ):
            self.assertIn(token, smoke)
        section = smoke.split('checkpoint("Reader phase: FreeText persisted disk bytes, wait for actual reopen")', 1)[1]
        section = section.split('checkpoint("Reader phase: native print dialog open and cancel")', 1)[0]
        self.assertIn('controller->closeDocument();', section)
        self.assertIn('controller->openDocument(saved);', section)
        self.assertIn('annotations(pdf::AnnotationType::FreeText) != 1', section)
        self.assertIn('if (!select->isChecked()) select->trigger();', section)
        self.assertNotIn('controller->getDocument() && select->isChecked() && window.isEnabled()', section)
        self.assertLess(section.index('annotations(pdf::AnnotationType::FreeText) != 1'),
                        section.index('if (!select->isChecked()) select->trigger();'))

    def test_highlight_palette_uses_eight_one_click_marker_tools(self):
        adapter = (ROOT / 'src/lunareadertoolbar.cpp').read_text(encoding='utf-8')
        self.assertIn('LunaHighlightPalette', adapter)
        for label, hex_color in (
            ('노란색', '#FFE049'), ('초록색', '#A3E6A4'),
            ('분홍색', '#FFB3CF'), ('파란색', '#9FCAFF'),
            ('주황색', '#FFC58A'), ('보라색', '#C7B2FF'),
            ('민트색', '#A3E8DF'), ('회색', '#CED4DE'),
        ):
            self.assertIn('{"' + label + '", "' + hex_color + '"}', adapter)
        self.assertIn('connect(palette, &QMenu::aboutToShow', adapter)
        self.assertIn('palette->addAction(chooseColor_)', adapter)
        smoke = (ROOT / 'src/lunaeditorsmoke.cpp').read_text(encoding='utf-8')
        self.assertIn('Reader phase: eight one-click highlight pens and recolor', smoke)
        self.assertIn('Color click without selection armed a marker or modified PDF', smoke)
        self.assertIn('Drag highlight color not written to native PDF annotation', smoke)
        self.assertIn('std::abs(components[0] - 163.0 / 255.0)', smoke)
        self.assertIn('for (qsizetype i = 0; i < expectedColors.size(); ++i)', smoke)
        self.assertIn('annotations/highlightColor', adapter)
        self.assertIn('void reloadHighlightColor()', adapter)
        self.assertIn('reloadHighlightColor(); // Other native tabs may have changed', adapter)
        self.assertIn('reloadHighlightColor();\n        auto* proxy', adapter)
        self.assertNotIn('add("LunaHighlightColor")', adapter)
        self.assertIn('class HighlightSwatchIcon final', adapter)
        self.assertIn('LunaHighlightSwatch%1', adapter)
        self.assertIn('syncSwatches();', adapter)
        self.assertIn('add("actionPrint");', adapter)
        self.assertIn('QPolygonF nib;', adapter)
        self.assertIn('void armEraser()', adapter)
        self.assertIn('eraser_->setCheckable(false);', adapter)
        self.assertIn('swatch->setCheckable(false);', adapter)
        self.assertIn('if (eraserArmed_) eraseSelectedHighlights();', adapter)
        self.assertNotIn('if (ready && eraserArmed_) eraseSelectedHighlights();', adapter)
        self.assertIn('auto* controller = window_.getProgramController();', adapter)
        self.assertIn('if (source == lastDocumentSource_) return;', adapter)
        self.assertIn('eraserArmed_ = false; eraser_->setChecked(false);', adapter)
        self.assertNotIn('eraser_->setEnabled(false);', adapter)

    def test_selection_overlay_keeps_native_text_and_bridges_visual_word_gaps(self):
        source = (ROOT / 'tools/apply_performance.py').read_text(encoding='utf-8')
        # Pinned PDF4QT painter only: do not change PDF text extraction.
        self.assertIn('a50aeb3ca241f41bd54c4d9d49bd5682f1bed2b8', source)
        self.assertIn('LunaSelectionBand', source)
        self.assertIn('const qreal gap = qMax(0.0', source)
        self.assertIn('gap <= qMin(48.0, 2.1 * h)', source)
        self.assertIn('block.getCharacterRangeBoundingPath', source)
        self.assertIn('qAbs(block.getAngle()) > 0.5', source)
        self.assertNotIn('getTextFromSelection(', source)
        ast.parse(source)

    def test_manual_card_tags_not_extracted_from_pdf(self):
        source = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        for token in (
            'library/manualTags', 'void editManualTags(const QString& path)',
            'LunaEditManualTags', 'DocumentCard::tagsRect(rect)',
            'index.data(Qt::UserRole + 8).toStringList()',
            'tags.mid(0, 2)', 'QInputDialog::getText',
            'tags_.insert(newDateKey, movedTags)', 'tags_.remove(oldDateKey)',
            '저자 미확인',
        ):
            self.assertIn(token, source)
        self.assertNotIn('tags = extractKeywords', source)
        self.assertNotIn('recentOpened + authors', source)

    def test_1000_pdf_incremental_scanner_is_bounded_and_diagnostic(self):
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        start = library.index('    void scanBatch(bool all) {')
        end = library.index('    void openItem(', start)
        scan = library[start:end]
        self.assertIn('batchClock.elapsed() < 25', scan)
        self.assertIn('const QIcon fileIcon = style()->standardIcon(QStyle::SP_FileIcon);', scan)
        self.assertIn('if (wasUpdating) list->setUpdatesEnabled(false);', scan)
        self.assertIn('if (wasUpdating) list->setUpdatesEnabled(true);', scan)
        self.assertIn('if (complete) selectActiveCard();', scan)
        self.assertEqual(scan.count('selectActiveCard();'), 1)
        self.assertIn('list->count() == 1000 && !library->folderTimer_->isActive(); }, 15000', library)
        self.assertIn('longestGuiGapMs=%4 beats=%5', library)
        self.assertIn('if (longestGap > 1000)', library)
        self.assertIn('searchMs > 1000', library)
        self.assertIn('if (visible != 1', library)

    def test_large_si_open_defers_nonessential_work_until_after_first_render(self):
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        page_grid = (ROOT / 'src/lunapagegrid.h').read_text(encoding='utf-8')
        for token in (
            'const bool deferForLargePdf = !force && catalog && catalog->getPageCount() >= 80;',
            'delay->start(1200);',
            'if (force) if (auto* pending = delayedCitations_.value(identity, nullptr))',
            'pending->start(0);',
            'delayedCitations_.remove(identity);',
            'bibliography-first-page-worker',
            'bibliography-worker-dispatch',
            'worker->start(QThread::LowPriority)',
            'bibliography_.enrichFromFirstPage(identity, *result)',
        ):
            self.assertIn(token, library, token)
        for token in (
            'if (model() == native.model() && selectionModel() == native.selectionModel()) return;',
            'pendingModelSize_ = true;',
            'if (!isVisible()) return;',
            'if (isVisible()) {',
            'thumbnailSize_(previewHeight);',
            'pendingModelSize_ = false;',
        ):
            self.assertIn(token, page_grid, token)

    def test_public_si_performance_probe_keeps_only_numeric_timings(self):
        probe = (ROOT / 'tools/probe_research_windows.py').read_text(encoding='utf-8')
        ast.parse(probe)
        for token in (
            'LUNA_PERF="1"',
            'capture_output=True, text=True, errors="replace"',
            'PUBLIC_SI_TIMINGS.json',
            '"request-to-first-page-paint"',
            '"bibliography-first-page-worker"',
            '"elapsedMs": elapsed_ms',
            'completed.stderr.splitlines()',
            'SI first-page-paint metric missing',
            'SI PDF parse metric missing',
        ):
            self.assertIn(token, probe, token)
        self.assertNotIn('write_text(completed.stderr', probe)
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        self.assertIn('draw->setProperty("LunaOpenStart", performanceNow());', library)
        self.assertIn('if (qEnvironmentVariableIsSet("LUNA_PERF")) {', library)

    def test_clean_paper_list_keeps_page_preview_on_demand_only(self):
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        tabs = (ROOT / 'src/lunatabs.cpp').read_text(encoding='utf-8')
        guide = (ROOT / 'USER_GUIDE_KO.md').read_text(encoding='utf-8')
        self.assertIn('LunaPagePreview', library)
        self.assertIn('pagePreviewAction_->setChecked(false)', library)
        self.assertIn('viewMenu->addAction(pagePreviewAction_)', library)
        self.assertIn('content->setCurrentIndex(enabled ? 1 : 0)', library)
        self.assertNotIn('mode->addTab(QStringLiteral("페이지 보기"))', library)
        self.assertIn('window.findChild<QTabBar*>(QStringLiteral("LunaContentModes"))', library)
        self.assertIn('preview->trigger()', library)
        self.assertIn('auto* preview = host.findChild<QAction*>(QStringLiteral("LunaPagePreview"))', tabs)
        self.assertIn('!host.findChild<QTabBar*>(QStringLiteral("LunaContentModes"))', tabs)
        self.assertIn('preview->trigger()', tabs)
        self.assertIn('보기 → 페이지 미리보기', guide)

    def test_compact_tabs_and_blue_borderless_selection_contract(self):
        tabs = (ROOT / 'src/lunatabs.cpp').read_text(encoding='utf-8')
        library = (ROOT / 'src/lunalibrary.cpp').read_text(encoding='utf-8')
        style = (ROOT / 'src/lunalibrary.qss').read_text(encoding='utf-8')
        overlay = (ROOT / 'experiments/native_tabs/prepare.py').read_text(encoding='utf-8')
        self.assertIn('setCornerWidget(corner, Qt::TopLeftCorner)', tabs)
        self.assertIn('setExpanding(false)', tabs)
        self.assertIn('Qt::ElideMiddle, 156', tabs)
        self.assertIn('row->insertWidget(0, toggle)', library)
        self.assertIn('QTabBar#LunaPdfTabBar::tab', style)
        self.assertIn('painter->fillPath(selectionPath', overlay)
        self.assertIn('PDFTextSelectionPainter selectionPainter(&m_textSelection)', overlay)
        self.assertIn('QEvent::ShortcutOverride || event->type() == QEvent::KeyPress', tabs)
        self.assertIn('ctrlWClosesOnlyActiveTabAndPreservesWindow', tabs)
        self.assertIn('ctrlWLastPdfLeavesEmptyLunaWindow', tabs)

if __name__ == '__main__':
    unittest.main()
