"""LUNA document-type icon and opt-in Windows association safety contracts.

Pure synthetic fixtures; no system default app is changed by Python tests.
"""
from pathlib import Path
import struct
import unittest
from tools.make_pdf_icon import make_ico, SIZES
from tools.preflight_public import APPROVED_BINARY

ROOT=Path(__file__).resolve().parents[1]
TAB=(ROOT/"src/lunatabs.cpp").read_text(encoding="utf-8")
REG=(ROOT/"src/lunafileassociation.h").read_text(encoding="utf-8")
OVERLAY=(ROOT/"tools/apply_overlay.py").read_text(encoding="utf-8")
BUILD=(ROOT/".github/workflows/build-windows.yml").read_text(encoding="utf-8")
NATIVE=(ROOT/"tests/native_dialog/pdfassociation.cpp").read_text(encoding="utf-8")
QRC_ACTIVE=(ROOT/"assets/icons/ui-moon-active.svg").read_text(encoding="utf-8")
QRC_INACTIVE=(ROOT/"assets/icons/ui-moon-inactive.svg").read_text(encoding="utf-8")


class PdfIconAndAssociation(unittest.TestCase):
    def test_red_pdf_ico_has_multiple_real_embedded_png_sizes(self):
        icon=make_ico()
        self.assertEqual(icon[:4],bytes.fromhex("00000100"))
        count=struct.unpack_from("<H",icon,4)[0]
        self.assertEqual(count,len(SIZES))
        sizes=[]
        for i in range(count):
            w,h,colors,reserved,planes,bits,size,offset=struct.unpack_from(
                "<BBBBHHII",icon,6+i*16)
            side=w or 256
            sizes.append(side)
            self.assertEqual(h or 256,side)
            self.assertEqual((colors,reserved,planes,bits),(0,0,1,32))
            self.assertLess(offset+size,len(icon)+1)
            self.assertEqual(icon[offset:offset+8],bytes.fromhex("89504e470d0a1a0a"))
            pw,ph=struct.unpack_from(">II",icon,offset+16)
            self.assertEqual((pw,ph),(side,side))
        self.assertEqual(tuple(sizes),SIZES)
        self.assertGreater(len(icon),1000)
        self.assertLess(len(icon),500000)

    def test_exe_blue_moon_is_immutable_and_pdf_icon_only_for_explorer(self):
        self.assertIn("assets/icons/LUNA-Moon.ico",APPROVED_BINARY)
        self.assertIn('folder / "luna_icon.rc": b\'IDI_ICON1 ICON DISCARDABLE "LUNA-Moon.ico"',OVERLAY)
        self.assertIn('Copy-Item \\'tools/LUNA-Update.ps1\\'',BUILD)
        self.assertIn('python -B tools/make_pdf_icon.py --out "$output/LUNA-PDF.ico"',BUILD)
        self.assertIn('LUNA-PDF.ico',REG)
        self.assertNotIn('LUNA-Moon.ico',REG)
        self.assertNotIn('LUNA-PDF.ico',TAB)  # PDF-tab glyph must not be document icon.

    def test_active_and_inactive_tab_crescents_have_different_colors(self):
        self.assertIn('QIcon(QStringLiteral(":/luna/icons/ui-moon-inactive.svg"))',TAB)
        self.assertIn('updateMoonTabIcons();',TAB)
        self.assertIn('i==tabs_->currentIndex()?active:inactive',TAB)
        self.assertIn('tabs_->setIconSize(QSize(21,21));',TAB)
        self.assertIn('#356DA7',QRC_ACTIVE)
        self.assertIn('#9AA1AA',QRC_INACTIVE)
        self.assertIn('icons/ui-moon-active.svg',TAB)
        self.assertIn('icons/ui-moon-inactive.svg',TAB)
        self.assertIn('glob("ui-*.svg")',OVERLAY)

    def test_registration_never_forces_windows_default_or_tampers_userchoice(self):
        for token in (
            'HKEY_CURRENT_USER', 'LUNA.Library.PDF', 'LUNA-PDF.ico',
            'Software\\Classes\\.pdf\\OpenWithProgids',
            'Software\\RegisteredApplications',
            'Software\\LUNA Library\\Capabilities',
            'SHCNE_ASSOCCHANGED',
            'ms-settings:defaultapps',
            'readValue(',
            'bool registerPdf(', 'bool unregisterPdf(',
            'RegDeleteValueW', 'RegDeleteTreeW',
        ):
            self.assertIn(token,REG,token)
        for forbidden in ('HKEY_LOCAL_MACHINE','UserChoice','RegOverridePredefKey',
                          'SetUserChoice','assoc.exe','ftype.exe'):
            # UserChoice may appear as an explanatory comment, never as code.
            if forbidden=='UserChoice':
                self.assertNotIn('Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts',
                                 REG)
            else:
                self.assertNotIn(forbidden,REG)
        self.assertIn('LunaRegisterPdfAssociation',TAB)
        self.assertIn('LunaUnregisterPdfAssociation',TAB)
        self.assertIn('PdfAssociation::registerPdf(&error)',TAB)
        self.assertIn('PdfAssociation::unregisterPdf(&error)',TAB)
        self.assertIn('PdfAssociation::openDefaultAppsSettings()',TAB)
        self.assertIn('HKEY_CURRENT_USER',NATIVE)
        self.assertIn('native_pdf_association_registry',
                      (ROOT/"tests/native_dialog/CMakeLists.txt").read_text(encoding="utf-8"))


if __name__=="__main__":
    unittest.main()
