# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Ensure maintained UI, documentation and source files stay English-only."""
import unittest

from tools.check_english_text import check


class EnglishLocalizationTests(unittest.TestCase):
    def test_no_cyrillic_in_maintained_project_sources(self):
        result = check()
        self.assertEqual(0, result["remaining_cyrillic_lines"], result["violations"][:20])
        self.assertGreater(result["files_scanned"], 100)


if __name__ == "__main__":
    unittest.main()
