"""LUNA PDF shell icon + inactive/active moon tab contracts.

The actual Windows EXE resources, registry writes and tab-paint behavior are
verified by the real Windows CI, not by these source-level checks.
"""
import struct
import unittest
from pathlib import Path
from tools.generate_pdf_icon import SIZES, PNG_MAGIC, make_icon

ROOT=Path(__file__).resolve().parents[1]
ASSOC=(ROOT/"src/lunapdfassociation.h").read_text(encoding="utf-8")
TABS=(ROOT/"src/lunatabs.cpp").read_text(encoding="utf-8")
MOON=(ROOT/"src/lunatabmoon.h").read_text(encoding="utf-8")
OVERLAY=(ROOT/"tools/apply_overlay.py").read_text(encoding="utf-8")
WIN=(ROOT/".github/workflows/build-windows.yml").read_text(encoding="utf-8")
PROBE=(ROOT/"tools/probe_pdf_icon_windows.py").read_text(encoding="utf-8")
NATIVE=(ROOT/"tests/native_dialog/pdfassociation.cpp").read_text(encoding="utf-8")
CMAKE=(ROOT/"tests/native_dialog/CMakeLists.txt").read_text(encoding="utf-8")


class LunaIconContracts(unittest.TestCase):
    def test_pdf_multisize_icon_valid_ico_png_entries(self):
        data=make_icon()
        self.assertEqual(struct.unpack_from("<HHH",data), (0,1,len(SIZES)))
        for i,dimension in enumerate(SIZES):
            width,height,colors,reserved,planes,bpp,length,offset=struct.unpack_from(
                "<BBBBHHII",data,6+i*16)
            self.assertEqual(width,0 if dimension==256 else dimension)
            self.assertEqual(height,0 if dimension==256 else dimension)
            self.assertEqual((colors,reserved,planes,bpp),(0,0,1,32))
            self.assertGreater(length,75)
            self.assertLessEqual(offset+length,len(data))
            self.assertEqual(data[offset:offset+8],PNG_MAGIC)
            self.assertEqual(struct.unpack_from(">II",data,offset+16),(dimension,dimension))
        self.assertGreater(len(data),5000)
        self.assertLess(len(data),200000)

    def test_original_blue_exe_icon_preserved_and_pdf_red_icon_embedded(self):
        self.assertIn('folder / "LUNA-Moon.ico": (own.parent / "assets/icons/LUNA-Moon.ico").read_bytes()',OVERLAY)
        self.assertIn('folder / "LUNA-PDF.ico": make_icon()',OVERLAY)
        self.assertIn('1 ICON DISCARDABLE "LUNA-Moon.ico"',OVERLAY)
        self.assertIn('2 ICON DISCARDABLE "LUNA-PDF.ico"',OVERLAY)
        self.assertIn('target_link_libraries(LUNA PRIVATE advapi32 shell32)',OVERLAY)
        self.assertIn("folder / \"lunapdfassociation.h\"",OVERLAY)
        self.assertIn("folder / \"lunatabmoon.h\"",OVERLAY)
        self.assertIn('tools/probe_pdf_icon_windows.py "$output/LUNA.exe"',WIN)
        self.assertIn('if (root/"LUNA-PDF.ico").exists():',PROBE)
        self.assertIn('for resource_id in (1,2):',PROBE)
        self.assertIn('resource ID {resource_id}',PROBE)

    def test_default_app_registration_only_on_explicit_user_click(self):
        for token in (
            'registerCurrentUser(const QString& exePath, QString* failure=nullptr,',
            'HKEY_CURRENT_USER',
            'Classes\\LUNA.Library.PDF',
            'Classes\\.pdf\\OpenWithProgids',
            'RegisteredApplications',
            'FileAssociations',
            'SHChangeNotify(SHCNE_ASSOCCHANGED',
            'QStringLiteral("\\\",-2")',
            'LUNA.exe',
        ):
            self.assertIn(token,ASSOC,token)
        for token in (
            'LunaRegisterPdfAssociation',
            'QStringLiteral("PDF 기본 앱으로 설정…")',
            'luna::PdfAssociation::registerCurrentUser(exe,&failure)',
            'ms-settings:defaultapps',
            'Windows 정책상 앱이 사용자의 기본 앱을 강제로 바꾸지는 않습니다.',
        ):
            self.assertIn(token,TABS,token)
        self.assertNotIn('Classes\\.pdf\\UserChoice',ASSOC)
        self.assertNotIn('RegDeleteTreeW',ASSOC)
        self.assertIn('if(prefix!=QStringLiteral("Software\\\\")',ASSOC)
        self.assertIn('native_pdf_default_apps_candidate',CMAKE)
        self.assertIn('Must not create Windows .pdf UserChoice',NATIVE)

    def test_moon_tab_drawn_as_outline_and_state_follows_active_document(self):
        for token in (
            "painter.setBrush(Qt::NoBrush)",
            "moon.cubicTo(",
            'QColor(QStringLiteral("#3473AE"))',
            'QColor(QStringLiteral("#9AA5B3"))',
        ):
            self.assertIn(token,MOON,token)
        for token in (
            'setIconSize(QSize(23,23))',
            'tabs_->addTab(editor, luna::tabMoonIcon(',
            'void updateMoonTabStates()',
            'tabs_->setTabIcon(i,luna::tabMoonIcon(selected))',
            'selected?QColor(QStringLiteral("#193E71"))',
            'QColor(QStringLiteral("#89929F"))',
            'connect(tabs_, &QTabWidget::currentChanged',
            'tabMoved',
            'Blue active / grey inactive outline-moon tabs did not render',
        ):
            self.assertIn(token,TABS,token)


if __name__=="__main__":
    unittest.main()
