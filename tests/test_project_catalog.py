# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026

"""Headless Project Catalog regression tests for project-library workflows."""
import json
from pathlib import Path
import tempfile
import unittest

from tools.dev.projects import (
    LauncherError, ProjectManager, TEMPLATES, MANIFEST, STARTUP_SCENE,
)


class ProjectCatalogTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="ns-project-browser-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name) / "engine"
        self.root.mkdir()
        self.manager = ProjectManager(self.root)

    def test_all_templates_create_native_scenes(self):
        for template in TEMPLATES:
            with self.subTest(template=template.id):
                p = self.manager.create("Sample " + template.id, template=template.id)
                self.assertTrue(p.can_open, p.status_reason)
                self.assertEqual(template.id, p.template)
                scene = (p.directory / STARTUP_SCENE).read_bytes()
                self.assertTrue(scene.startswith(b"NEF8"))
                self.assertTrue((p.directory / "config").is_dir())
                self.assertEqual("NorthStarCpp", json.loads(
                    (p.directory / MANIFEST).read_text(encoding="utf8"))["engine"])

    def test_game_templates_are_script_first(self):
        for template in ("sandbox", "physics"):
            with self.subTest(template=template):
                game=self.manager.create("Example "+template,template=template)
                source=(game.directory/"scripts/main.js").read_text("utf8")
                self.assertIn("Example "+template,source)
                self.assertIn("engine.player.move",source)
                self.assertIn("engine.on",source)
                self.assertNotIn("__NORTHSTAR_PROJECT_TITLE__",source)
                meta=json.loads((game.directory/MANIFEST).read_text("utf8"))
                self.assertEqual(meta["game_mode"],"explore")
                self.assertEqual(meta["scripting"]["entry"],"scripts/main.js")

    def test_favorite_and_recent_persist_after_manager_restart(self):
        p = self.manager.create("Memory")
        self.manager.set_favorite(p, True)
        self.manager.mark_opened(p)
        later = ProjectManager(self.root).get(p.directory)
        self.assertTrue(later.favorite)
        self.assertTrue(later.last_opened)
        self.assertEqual(p.name, later.name)

    def test_duplicate_copies_assets_but_keeps_original(self):
        p = self.manager.create("Source", template="physics")
        (p.directory / "assets" / "texture.bin").write_bytes(b"data")
        other = self.manager.duplicate(p, "Copy")
        self.assertTrue(other.can_open)
        self.assertEqual(b"data", (other.directory / "assets" / "texture.bin").read_bytes())
        self.assertEqual("Source", self.manager.get(p.directory).name)
        self.assertEqual("Copy", json.loads(
            (other.directory / MANIFEST).read_text("utf8"))["name"])
        with self.assertRaises(LauncherError):
            self.manager.duplicate(p, "Copy")

    def test_rename_and_edit_preserves_scene_and_unknown_metadata(self):
        p = self.manager.create("Old")
        path = p.directory / MANIFEST
        data = json.loads(path.read_text("utf8"))
        data["custom_plugin_config"] = {"k": "v"}
        path.write_text(json.dumps(data), encoding="utf8")
        changed = self.manager.rename(p, "New")
        changed = self.manager.update_details(changed, description="In development",
                                              category="Simulation", tags=["test", "physics"])
        self.assertEqual("New", changed.name)
        self.assertEqual("Simulation", changed.category)
        self.assertEqual(("test", "physics"), changed.tags)
        self.assertTrue(changed.scene_path.exists())
        self.assertEqual({"k": "v"}, json.loads(path.read_text("utf8"))["custom_plugin_config"])

    def test_external_project_register_and_forget_without_deletion(self):
        foreign = self.root.parent / "external"
        foreign.mkdir()
        p = self.manager.create("External One", location=foreign)
        self.assertTrue(p.external)
        imported = ProjectManager(self.root).get(p.directory)
        self.assertTrue(imported.can_open)
        self.assertIn(imported.directory, [x.directory for x in self.manager.list()])
        self.manager.forget(p)
        self.assertNotIn(p.directory, [x.directory for x in self.manager.list()])
        self.assertTrue((p.directory / MANIFEST).exists())
        self.manager.register(p.directory)
        self.assertIn(p.directory, [x.directory for x in self.manager.list()])

    def test_scan_nested_directory_registers_external(self):
        foreign = self.root.parent / "scanned"
        nested = foreign / "group" / "works"
        nested.mkdir(parents=True)
        p = self.manager.create("Discovered", location=nested)
        self.manager.forget(p)
        found = self.manager.scan(foreign, depth=4)
        self.assertEqual([p.directory], [i.directory for i in found])
        self.assertIn(p.directory, [i.directory for i in self.manager.list()])

    def test_missing_scene_and_unsafe_scene_are_flagged(self):
        p = self.manager.create("With Error")
        p.scene_path.unlink()
        missing = self.manager.get(p.directory)
        self.assertEqual("missing_scene", missing.status)
        data = json.loads((p.directory / MANIFEST).read_text("utf8"))
        data["startup_scene"] = "../outside.nscene"
        (p.directory / MANIFEST).write_text(json.dumps(data), encoding="utf8")
        self.assertEqual("invalid", self.manager.get(p.directory).status)

    def test_unsupported_version_and_manifest_are_flagged(self):
        p = self.manager.create("Legacy Compatibility")
        data = json.loads((p.directory / MANIFEST).read_text("utf8"))
        data["engine_version"] = "999.0.0"
        (p.directory / MANIFEST).write_text(json.dumps(data), encoding="utf8")
        self.assertEqual("incompatible", self.manager.get(p.directory).status)
        data["schema_version"] = 999
        (p.directory / MANIFEST).write_text(json.dumps(data), encoding="utf8")
        self.assertEqual("invalid", self.manager.get(p.directory).status)

    def test_hidden_local_project_can_be_imported_again(self):
        p = self.manager.create("Private")
        self.manager.forget(p)
        self.assertNotIn(p.directory, [i.directory for i in self.manager.list()])
        self.manager.register(p.directory)
        self.assertEqual(["Private"], [i.name for i in self.manager.list()])

    def test_library_corruption_never_overwritten(self):
        p = self.manager.create("Before Error")
        library = self.manager.library_file
        library.write_text("{corrupted", encoding="utf8")
        with self.assertRaisesRegex(LauncherError, "damaged"):
            self.manager.list()
        with self.assertRaisesRegex(LauncherError, "damaged"):
            self.manager.mark_opened(p)
        self.assertEqual("{corrupted", library.read_text("utf8"))

    def test_missing_external_directory_is_visible(self):
        p = self.manager.create("Will disappear", location=self.root.parent)
        self.assertTrue(p.external)
        import shutil
        shutil.rmtree(p.directory)
        missing = self.manager.get(p.directory)
        self.assertEqual("missing", missing.status)
        self.assertIn("missing", [x.status for x in self.manager.list()])

    def test_bad_project_name_and_template_rejected_without_creating_dir(self):
        for name in ("CON", "../hack", "a/b", "bad?", " "):
            with self.subTest(name=name), self.assertRaises(LauncherError):
                self.manager.create(name)
        with self.assertRaises(LauncherError):
            self.manager.create("Fake Template", template="nonexistent")
        self.assertFalse((self.root / "Projects" / "Fake_Template").exists())


if __name__ == "__main__":
    unittest.main()
