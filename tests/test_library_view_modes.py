"""Regression contracts for LUNA Library list/card and smooth-scrolling controls."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LibraryModes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = (ROOT/"src/lunalibrary.cpp").read_text(encoding="utf-8")
        cls.qss = (ROOT/"src/lunalibrary.qss").read_text(encoding="utf-8")

    def test_two_display_modes_have_same_model_and_retain_user_data(self):
        for term in (
            "static constexpr int kCardHeight = 322",
            "static constexpr int kCompactHeight = 112",
            "void paintCompact(QPainter* painter",
            "readerIcon(QStringLiteral(\"memo-inline\"))",
            "static QRect compactFavoriteRect(const QRect& r)",
            "static QRect compactMemoRect(const QRect& r)",
            "void setPaperViewMode(bool compact)",
            "delegate->setCompact(compact)",
            "delegate->setCompact(compactPaperMode_)",
            "library/paperViewMode",
            "list->setViewMode(QListView::ListMode)",
            "list->setFlow(QListView::TopToBottom)",
            "list->setWrapping(false)",
            "list->setEditTriggers(QAbstractItemView::NoEditTriggers)",
            "QMetaObject::invokeMethod(list, \"doItemsLayout\", Qt::DirectConnection)",
        ):
            self.assertIn(term, self.cpp, term)
        self.assertNotIn("list->setViewMode(QListView::IconMode)", self.cpp)

    def test_one_line_citation_spacing_and_smaller_list_thumbnail(self):
        for term in (
            "const bool oneLineTitle = titleMetrics.horizontalAdvance(title) <= heading.width();",
            "oneLineTitle ? citationRect(r).translated(0, -19) : citationRect(r)",
            "const QRect thumb(r.left() + 11, r.top() + 12, thumbWidth, thumbHeight)",
            "const int left = compactTextLeft(r)",
            "const int thumbWidth = compactThumbWidth(r)",
            "qBound(34, (r.width() - 70) / 2, 72)",
            "r.top()+49, width, 20",
            "r.top()+80, left+width",
        ):
            self.assertIn(term, self.cpp, term)
        # Two-line title positions and the 112px compact-row envelope stay fixed.
        self.assertIn("static constexpr int kCompactHeight = 112", self.cpp)
        self.assertIn("titleLayout.createLine()", self.cpp)
        self.assertIn("QTextLine line = layout.createLine();", self.cpp)

    def test_narrow_document_pane_resizes_cards_and_preserves_controls(self):
        for term in (
            "return {1, compact_ ? kCompactHeight : kCardHeight};",
            "const int height = qMin(164, qMax(48, content.width() * 2 / 3));",
            "return QRect(content.left(), thumbnailRect(r).bottom() + 13, content.width(), 45);",
            "titleMetrics.elidedText(text, Qt::ElideRight, available)",
            "QFontMetrics(captionFont).elidedText(metadata, Qt::ElideRight, width)",
            "note.isEmpty() ? QStringLiteral(\"메모 추가\") : note, Qt::ElideRight",
            "title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed)",
            "documents_->setMinimumWidth(195)",
            "list->setResizeMode(QListView::Adjust)",
        ):
            self.assertIn(term, self.cpp, term)

    def test_buttons_order_selection_and_brand_typography(self):
        for name in ("LunaListViewMode", "LunaCardViewMode"):
            self.assertIn(name, self.cpp)
            self.assertIn("QToolButton#"+name, self.qss)
        self.assertLess(self.cpp.index("titleRow->addWidget(listMode);"),
                        self.cpp.index("titleRow->addWidget(cardMode);"))
        self.assertLess(self.cpp.index("titleRow->addWidget(cardMode);"),
                        self.cpp.index("titleRow->addWidget(addFile);"))
        self.assertIn("paperViewGroup->setExclusive(true);", self.cpp)
        self.assertIn("listMode->setChecked(compactPaperMode_)", self.cpp)
        self.assertIn("cardMode->setChecked(!compactPaperMode_)", self.cpp)
        self.assertIn("background:#E3EFFF", self.qss)
        self.assertIn("headingFont.setPixelSize(18)", self.cpp)
        self.assertIn("font-size:18px; font-weight:700", self.qss)
        self.assertIn("brandFont.setPixelSize(20)", self.cpp)

    def test_icons_are_svg_and_allowlisted(self):
        whitelist=(ROOT/"tools/preflight_public.py").read_text(encoding="utf-8")
        overlay=(ROOT/"tools/apply_overlay.py").read_text(encoding="utf-8")
        self.assertIn('glob("ui-*.svg")', overlay)
        for name in ("list-mode", "card-mode"):
            svg=(ROOT/"assets/icons"/("ui-"+name+".svg")).read_text(encoding="utf-8")
            self.assertIn('<svg ', svg)
            self.assertIn('stroke="currentColor"', svg)
            self.assertIn('assets/icons/ui-'+name+'.svg', whitelist)
            self.assertIn('readerIcon(QStringLiteral("'+name+'"))', self.cpp)

    def test_paper_only_window_can_shrink_without_hidden_navigation_minimum(self):
        self.assertIn('split_->setChildrenCollapsible(true);', self.cpp)
        self.assertIn('setMinimumWidth(0);', self.cpp)
        self.assertNotIn('setMinimumWidth(432);', self.cpp)
        self.assertIn('setMinimumWidth((hasNavigation ? 225 : 0) + (hasDocuments ? 195 : 0) + 12)', self.cpp)
        self.assertIn('setMaximumWidth(both ? 850 : 290)', self.cpp)
        self.assertIn('dock->setMinimumWidth(1)', self.cpp)
        self.assertIn('dock->setMaximumWidth(both ? 850 : 300)', self.cpp)
        self.assertIn('dock->updateGeometry()', self.cpp)
        self.assertIn('const int width = both ? 562 : (hasNavigation ? 270 : 240)', self.cpp)
        self.assertIn('QTimer::singleShot(0, dock, [dock, width, both]', self.cpp)
        self.assertIn('(dock->widget()->maximumWidth() == 850) != both', self.cpp)
        self.assertIn('window->resizeDocks({dock}, {width}, Qt::Horizontal)', self.cpp)
        self.assertIn('navigation_->setVisible(navigation_->isHidden())', self.cpp)
        self.assertIn('documents_->setVisible(documents_->isHidden())', self.cpp)

    def test_paper_header_and_pdf_tabs_share_top_alignment_and_no_native_tab_rule(self):
        tabs = (ROOT/'src/lunatabs.cpp').read_text(encoding='utf-8')
        self.assertIn('layout->setContentsMargins(7, 7, 0, 7)', self.cpp)
        self.assertIn('layout->setContentsMargins(0, 7, 0, 0); layout->setSpacing(0)', self.cpp)
        self.assertIn('libraryHeader->setFixedHeight(41)', self.cpp)
        self.assertIn('title->setFixedHeight(41)', self.cpp)
        self.assertIn('return {218, 41};', tabs)
        self.assertIn('tabs_->tabBar()->setDrawBase(false)', tabs)
        self.assertIn('QTabBar#LunaPdfTabBar { background:#F3F6FB; border:0; border-top:0; }', self.qss)
        self.assertNotIn('setCornerWidget(corner, Qt::TopLeftCorner)', tabs)

    def test_independent_panel_collapse_controls_remain_reachable(self):
        for token in (
            'QStringLiteral("LunaNavigationCollapseButton")',
            'QStringLiteral("LunaDocumentsCollapseButton")',
            'QStringLiteral("LunaNavigationRestoreButton")',
            'QStringLiteral("LunaDocumentsRestoreButton")',
            'QStringLiteral("LunaLibraryHeader")',
            'QStringLiteral("LunaPaperHeader")',
            'QStringLiteral("LunaPanelRestoreRail")',
            'libraryHeading->layout()->addWidget(panelButton(',
            'paperHeading->addWidget(panelButton(',
            'restoreRail->move(8, window.menuBar()->height() + 95)',
            'void toggleNavigation()', 'void toggleDocuments()',
        ):
            self.assertIn(token, self.cpp, token)
        self.assertNotIn('QStringLiteral("LunaCombinedPanelToggle")', self.cpp)
        self.assertNotIn('addIndependentPanelButtons(corner, row)', self.cpp)
        self.assertIn('navigation_->setVisible(navigation_->isHidden())', self.cpp)
        self.assertIn('documents_->setVisible(documents_->isHidden())', self.cpp)
        self.assertIn('void focusSearch() { documents_->show(); updateMinimum();', self.cpp)

    def test_user_configurable_default_opening_zoom_preserves_restored_view(self):
        for token in (
            'QStringLiteral("LunaDefaultOpeningZoom")',
            'QStringLiteral("기본 시작 배율 설정…")',
            'QStringLiteral("reader/defaultZoomPercent"), 200',
            "25, 500, 5, &accepted",
            "proxy->zoom(double(percent) / 100.0)",
            'if (!editor.property("LunaPreserveReopenView").toBool())',
            'preferences.sync()',
        ):
            self.assertIn(token, self.cpp, token)
        self.assertIn("c->getPdfWidget()->getDrawWidgetProxy()->zoom(zoom)", self.cpp)

    def test_background_watcher_cannot_restart_incremental_pdf_scan(self):
        self.assertIn("localSync_->filesChanged = [this] {", self.cpp)
        self.assertIn("if (folderTimer_->isActive() || allTimer_->isActive()) return;", self.cpp)
        self.assertIn("refreshFileLists();", self.cpp)
        self.assertIn("list->count() == 1000 && !library->folderTimer_->isActive()", self.cpp)

    def test_native_toolbar_geometry_uses_panel_headings_not_tab_buttons(self):
        reader = (ROOT/"src/lunareadertoolbar.cpp").read_text(encoding="utf-8")
        self.assertNotIn('QStringLiteral("LunaCombinedPanelToggle")', reader)
        for term in (
            'QStringLiteral("LunaNavigationCollapseButton")',
            'QStringLiteral("LunaDocumentsCollapseButton")',
            'QStringLiteral("LunaPanelRestoreRail")',
            'QStringLiteral("LunaLibraryHeader")',
            'QStringLiteral("LunaPaperHeader")',
            'Panel heading control is clipped or detached',
            'Toolbar overflow button overlaps a control',
        ):
            self.assertIn(term, reader, term)
        self.assertIn('controls->hide();', self.cpp)
        self.assertNotIn('addIndependentPanelButtons(documentHeader, documentRow);', self.cpp)
        self.assertNotIn('addIndependentPanelButtons(corner, row);', self.cpp)

    def test_wheel_uses_short_animation_but_not_for_touchpad(self):
        for term in (
            "class SmoothLibraryWheel final : public QObject",
            "animation_->setDuration(155)",
            "animation_->setEasingCurve(QEasingCurve::OutCubic)",
            "wheel->phase() != Qt::NoScrollPhase",
            "qBound(scrollbar->minimum(), baseline - delta, scrollbar->maximum())",
            "new SmoothLibraryWheel(navigationScroll, 58)",
            "new SmoothLibraryWheel(tree_, 58)",
            "tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel)",
            "new SmoothLibraryWheel(list, 96)",
            "list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel)",
        ):
            self.assertIn(term, self.cpp, term)

if __name__ == "__main__":
    unittest.main()
