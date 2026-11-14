# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026

"""Non-GUI regression tests for selecting a physics backend per process."""
import os
from pathlib import Path
import unittest
from unittest.mock import patch

from tools.dev.launcher import LauncherError, RuntimeState, launch


class PhysicsSelectionTests(unittest.TestCase):
    def setUp(self):
        self.state = RuntimeState(
            ready=True, reason="ready",
            executable=Path("northstarcpp_player.exe"),
            cwd=Path("."),
        )

    def test_jolt_selected_in_child_only(self):
        with patch("tools.dev.launcher.subprocess.Popen") as spawn:
            spawn.return_value.pid = 123
            old = os.environ.get("NORTHSTARCPP_PHYSICS_PROVIDER")
            self.assertEqual(123, launch(self.state, physics_provider="Jolt"))
            self.assertEqual("jolt",
                             spawn.call_args.kwargs["env"]["NORTHSTARCPP_PHYSICS_PROVIDER"])
            self.assertEqual(old, os.environ.get("NORTHSTARCPP_PHYSICS_PROVIDER"))

    def test_bullet_selected(self):
        with patch("tools.dev.launcher.subprocess.Popen") as spawn:
            launch(self.state, physics_provider="bullet")
            self.assertEqual("bullet",
                             spawn.call_args.kwargs["env"]["NORTHSTARCPP_PHYSICS_PROVIDER"])

    def test_unknown_provider_rejected(self):
        with patch("tools.dev.launcher.subprocess.Popen") as spawn:
            with self.assertRaises(LauncherError):
                launch(self.state, physics_provider="../../invalid")
            spawn.assert_not_called()


if __name__ == "__main__":
    unittest.main()
