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
