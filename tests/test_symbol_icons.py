"""Lossless user-supplied LUNA colour-symbol resources and UI contract."""
from pathlib import Path
import base64
import hashlib
import json
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "symbol_icons"
IDS = ("star heart crown gem flame sparkles bookmark pin flag trophy "
       "book flask atom microscope graduation lightbulb puzzle target network brain "
       "check clock timer calendar hourglass refresh archive inbox send layers "
       "moon sun coffee palette leaf gift smile music camera rocket").split()
SHA256 = "025b271e0c08746f1edac3fffa6d07fcf68fd91438b1caefe69bc519ecfa9e73"


class SymbolIconPack(unittest.TestCase):
    def test_exact_user_supplied_40_icons_not_placeholders(self):
        encoded = "".join((ASSETS / f"svg_bundle.part{i}.b85").read_text(encoding="ascii").strip()
                          for i in range(1, 5))
        raw = zlib.decompress(base64.b85decode(encoded))
        self.assertEqual(hashlib.sha256(raw).hexdigest(), SHA256)
        self.assertEqual(len(encoded), 13403)
        originals = json.loads(raw)
        self.assertEqual(set(originals), set(IDS))
        for name, svg in originals.items():
            self.assertIn("linearGradient", svg, name)
            root = ET.fromstring(svg)
            self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg", name)
            self.assertIn("viewBox", root.attrib)

    def test_qt_package_and_ui_use_namespaced_ids(self):
        source = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
        overlay = (ROOT / "tools/apply_overlay.py").read_text(encoding="utf-8")
        for term in (
            'tabs->addTab(symbols, QStringLiteral("일반"))',
            'makeGrid(QStringLiteral("폴더"), 20',
            'QStringLiteral("symbol:") + QString::fromLatin1(preset.id)',
            'kSymbolIconPresets[]',
            'LunaFolderIconTabs', 'LunaSymbolSearch', 'LunaSymbolCategory',
            'selected = id;', 'metadata.insert(QStringLiteral("iconId"), iconId)',
            'QStringLiteral(":/luna/symbols/")',
        ):
            self.assertIn(term, source, term)
        for name in IDS:
            self.assertIn(f'{{"{name}"', source)
        self.assertIn("luna_symbol_icons.qrc", overlay)
        self.assertIn(SHA256, overlay)
        self.assertIn("set(symbols) != set(symbol_ids)", overlay)
        self.assertIn('prefix="/luna/symbols"', overlay)

    def test_original_folder_collection_and_license_are_preserved(self):
        source = (ROOT / "src/lunalibrary.cpp").read_text(encoding="utf-8")
        self.assertIn('{"general","일반 폴더"}', source)
        self.assertIn('{"temporary","임시"}', source)
        self.assertIn('return QStringLiteral("general");', source)
        self.assertIn('if (id.startsWith(QStringLiteral("symbol:")))', source)
        self.assertIn('LUNA.folder.json', source)
        self.assertIn('metadata.insert(QStringLiteral("isImportant"), false)', source)
        notice = (ROOT / "LICENSE_NOTICE.md").read_text(encoding="utf-8")
        self.assertIn("Font Awesome Free 6.7.2", notice)
        self.assertIn("CC BY 4.0", notice)


if __name__ == "__main__":
    unittest.main()
