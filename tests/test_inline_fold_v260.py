"""v260 must keep tabs PDF-only and both 36px restores at original panel positions."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
TABS = (ROOT / "src/lunatabs.cpp").read_text(encoding="utf-8")
READER = (ROOT / "src/lunareadertoolbar.cpp").read_text(encoding="utf-8")
AUTO = (ROOT / "src/lunaautofilename.h").read_text(encoding="utf-8")


class InlinePanelV260(unittest.TestCase):
    def test_in_place_36px_original_order_and_independent_controls(self):
        for item in (
            'static constexpr int foldWidth_ = 36',
            'navigationSlot_->setObjectName(QStringLiteral("LunaNavigationSlot"))',
            'documentsSlot_->setObjectName(QStringLiteral("LunaDocumentsSlot"))',
            'split_->addWidget(navigationSlot_)',
            'split_->addWidget(documentsSlot_)',
            'navigationFold_->setFixedWidth(foldWidth_)',
            'documentsFold_->setFixedWidth(foldWidth_)',
            'navigationSlot_->setMaximumWidth(showNav ? 300 : foldWidth_)',
            'documentsSlot_->setMaximumWidth(showDocs ? 850 : foldWidth_)',
            'library->navigationFold()->layout()->addWidget(restoreNav)',
            'library->documentsFold()->layout()->addWidget(restoreDocs)',
            'Both folded panels did not remain in order beside the PDF',
        ):
            self.assertIn(item, LIBRARY, item)
        self.assertNotIn('LunaPanelRestoreRail', LIBRARY)
        self.assertNotIn('LunaPanelRestoreRail', TABS)
        self.assertNotIn('LunaPanelRestoreRail', READER)
        self.assertNotIn('tabs_->setCornerWidget(rail, Qt::TopLeftCorner)', TABS)

    def test_saved_width_and_real_ten_cycle_test(self):
        for item in (
            'library/expandedNavigationWidth',
            'library/expandedDocumentsWidth',
            'library/expandedDockWidth',
            'observed == parentWidget() && event->type() == QEvent::Resize',
            'QTimer::singleShot(0, this, [this]',
            'savePanelState();',
            'expandedDockPoll->setInterval(200)',
            'if (settings.value(QStringLiteral("library/expandedDockWidth")).toInt() == actualWidth)',
            'settings.setValue(QStringLiteral("library/expandedDockWidth"), actualWidth)',
            'expandedDockPoll->start()',
        ):
            self.assertIn(item, LIBRARY)
        # Qt's two-string QSettings(organization, application) constructor
        # ALWAYS reads NativeFormat, regardless of setDefaultFormat(IniFormat).
        # The Windows UI audit writes IniFormat; never compare to registry.
        self.assertNotIn('QSettings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))', LIBRARY)
        self.assertNotIn('QSettings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))', TABS)
        self.assertIn('const int saved = QSettings(QSettings::defaultFormat(), QSettings::UserScope,', TABS)
        self.assertIn('const int storedWidth = qBound(432, QSettings(QSettings::defaultFormat(), QSettings::UserScope,', LIBRARY)
        self.assertIn('qBound(432, QSettings(QSettings::defaultFormat(), QSettings::UserScope,', LIBRARY)
        for item in (
            'phase("ten real independent library/paper panel toggle cycles")',
            'for (int turn = 0; turn < 10; ++turn)',
            'navSlot->width() == 36 && docsSlot->width() == 36',
            'Folding and restoring panels lost the user\'s saved dock width',
            'Expanded dock width was not persisted after manual resize',
            'qAbs(actual - 690) <= 4 && saved == actual',
            'settledDockWidthDiagnostics',
        ):
            self.assertIn(item, TABS)

    def test_v259_icon_bibliography_and_file_rename_unchanged(self):
        self.assertIn('tabs_->tabBar()->setIconSize(QSize(19,19))', TABS)
        self.assertIn('articleTitle.isEmpty() ? QFileInfo(display).fileName() : articleTitle', TABS)
        self.assertIn('renameExtractedTitleFile(const QString& identity, QString* reason = nullptr)', LIBRARY)
        self.assertIn('supporting_information', AUTO)


if __name__ == "__main__":
    unittest.main()

