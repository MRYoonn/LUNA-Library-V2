"""Regression contract for the real Win32 library-card lifetime replay.

Synthetic fixtures only. The 150%-DPI second repetition previously failed
before any library-open request, despite having a visible, later-populated card.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = (ROOT / "tools/probe_editor_lifetime_windows.py").read_text("utf-8")


class LifetimeCardInputContracts(unittest.TestCase):
    def test_card_title_row_used_instead_of_blank_thumbnail(self):
        self.assertIn("library_row_y=315", PROBE)
        self.assertIn("default=315", PROBE)
        self.assertIn("round(350*scale)", PROBE)
        self.assertIn("round(library_row_y*scale)", PROBE)

    def test_search_renders_before_single_real_double_click(self):
        start = PROBE.index("def open_library(letter,editor,generation):")
        end = PROBE.index("open_library('A',1,1)", start)
        section = PROBE[start:end]
        for token in (
            "key(0x4b,True)", "key(ord(letter))",
            "time.sleep(1.5)",
            "wait(lambda:user.GetForegroundWindow()==main",
            "user.ClientToScreen(main,ctypes.byref(point))",
            "for _ in range(2):user.mouse_event(2,0,0,0,0);user.mouse_event(4,0,0,0,0)",
            "wait(lambda:event('document-load-completed',editor,generation)",
            "loaded.get('editorOpenOrigin')!='library-open'",
        ):
            self.assertIn(token, section, token)
        self.assertLess(section.index("time.sleep(1.5)"),
                        section.index("user.ClientToScreen"))
        self.assertLess(section.index("user.ClientToScreen"),
                        section.index("document-load-completed"))
        self.assertNotIn("QMetaObject", section)
        self.assertNotIn("openDocument(", section)

    def test_no_removed_lifetime_assertions(self):
        for token in (
            "for scale in (1,1.25,1.5):",
            "for repeat in range(2):",
            "firstEditorDestroyed=True",
            "survivorSearchThenClose=True",
            "tenEmptyKeysStable=True",
            "if not accepted:raise RuntimeError('Focused lifetime replay failed",
            "if not exits or any(int(code,16)!=0 for code in exits)",
        ):
            self.assertIn(token, PROBE)


if __name__ == "__main__":
    unittest.main()
