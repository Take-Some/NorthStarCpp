# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026

import json
import unittest
from tools.dev import cli as northstarcpp

class NorthStarCppShellTests(unittest.TestCase):
    def setUp(self):
        self.config = northstarcpp.config_load()
    def test_active_cmake_project_exists(self):
        self.assertTrue((northstarcpp.ROOT / "CMakeLists.txt").is_file())
        self.assertTrue((northstarcpp.ROOT / "engine/northstarcpp/CMakeLists.txt").is_file())
    def test_escape_protection(self):
        with self.assertRaises(northstarcpp.ShellError):
            northstarcpp.inside("../outside")
    def test_build_isolation(self):
        for kind in ("Debug", "Release", "RelWithDebInfo"):
            path = northstarcpp.build_path(self.config, kind)
            self.assertEqual(northstarcpp.ROOT, path.parents[1])
    def test_full_game_not_marked_ported(self):
        self.assertNotIn("game", northstarcpp.PORTED_TARGETS)
        self.assertIn("northstarcpp_math", northstarcpp.PORTED_TARGETS)
        self.assertIn("northstarcpp_portability_smoke", northstarcpp.PORTED_TARGETS)
    def test_engine_name(self):
        self.assertEqual("NorthStarCpp", self.config["engine_name"])
    def test_json_config(self):
        self.assertTrue(json.dumps(self.config))

if __name__ == "__main__":
    unittest.main()
