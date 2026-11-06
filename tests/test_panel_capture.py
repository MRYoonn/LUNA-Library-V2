"""Reject the actual UIA false positive exposed by native screenshot review."""
import unittest
from tools.capture_panel_comparison_windows import NAV, DOCS, panel_state_failures


def measured(state, nav, docs):
    return dict(state=state, window=[24, 47, 1408, 908], controls=[
        dict(name=NAV, rectangle=nav), dict(name=DOCS, rectangle=docs)])


class PanelCaptureTest(unittest.TestCase):
    def test_unchanged_clipped_library_is_not_a_successful_fold(self):
        original = measured('both-open', [248,159,277,188], [548,153,577,182])
        # Actual Build284 UIA geometry: a clipped Library button's center
        # missed the viewport. Y alone incorrectly accepted this screenshot.
        library = measured('library-folded', [248,159,277,188], [548,153,577,182])
        both = measured('both-folded', [248,159,277,188], [264,153,293,182])
        self.assertTrue(panel_state_failures(library, original, 1, True))
        self.assertTrue(panel_state_failures(both, original, 1, True))

    def test_actual_v261_fold_reaches_rails_but_moves_vertically(self):
        original = measured('both-open', [241,159,270,188], [549,153,578,182])
        for state, nav, docs in (
            ('library-folded', [34,515,63,544], [365,153,394,182]),
            ('both-folded', [36,515,65,544], [77,515,106,544]),
            ('both-folded-short', [36,465,65,494], [77,465,106,494]),
            ('papers-folded', [241,159,270,188], [269,515,298,544]),
        ):
            row = measured(state, nav, docs)
            self.assertEqual(panel_state_failures(row, original, 1), [])
            self.assertTrue(panel_state_failures(row, original, 1, True))
