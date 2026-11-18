# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Game release validation, threat cases and deterministic metadata tests."""
import json
from pathlib import Path
import tempfile
import unittest

from tools.build_game import (
    CAPABILITIES, GameBuildError, build_manifest, checked_relative,
    copy_game_assets, copy_game_scripts, copy_game_animations,
    copy_bundled_runtime, project_info, verify_game,
)


class GameReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="northstarcpp-game-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "project"
        self.project.mkdir()
        self.scene = self.project / "main.nscene"
        self.scene.write_text(
            'NorthStarCpp.Scene 2\ncube "Box" 0 0 0 1 46b9d8 0 0\n',
            encoding="utf-8")
        (self.project / "northstarcpp.project.json").write_text(json.dumps({
            "engine": "NorthStarCpp", "name": "Test Example",
            "schema_version": 1}), encoding="utf-8")

    def fake_release(self):
        folder = self.root / "package"
        (folder / "bin").mkdir(parents=True)
        (folder / "game").mkdir()
        (folder / "bin/northstarcpp_player.exe").write_bytes(b"fake-test-executable")
        (folder / "bin/bugtrap.exe").write_bytes(b"fake-test-crash-reporter")
        (folder / "game/main.nscene").write_bytes(self.scene.read_bytes())
        (folder / "launch.cmd").write_text("@echo off\n", encoding="utf-8")
        providers = []
        for capability in sorted(CAPABILITIES):
            name = capability.split(".", 1)[1] + "_test.dll"
            file = folder / "bin" / name
            file.write_bytes(capability.encode("ascii"))
            providers.append({"capability": capability, "provider": "test",
                              "path": "bin/" + name})
        build_manifest(folder, "Test Example",
                       {"version": "0.1.0", "build_type": "Release", "host_abi": 1},
                       "bullet", providers, 0)
        return folder

    def test_project_and_assets_export(self):
        name, project, scene = project_info(self.project)
        self.assertEqual(name, "Test Example")
        (self.project / "assets/materials").mkdir(parents=True)
        (self.project / "assets/materials/test.bin").write_bytes(b"shaderdata")
        dest = self.root / "game"
        self.assertEqual(copy_game_assets(project, dest, scene), 1)
        self.assertEqual((dest / "main.nscene").read_bytes(), self.scene.read_bytes())
        self.assertEqual((dest / "assets/materials/test.bin").read_bytes(), b"shaderdata")

    def test_binary_scene_package_and_crc_validation(self):
        from tools.dev.scene_binary import encode_legacy_template
        old_scene = self.scene
        scene = self.project / "main.nscn"
        scene.write_bytes(encode_legacy_template(
            'NorthStarCpp.Scene 2\ncube "Player" 0 0 0 1 46b9d8 1 1\n'))
        (self.project / "northstarcpp.project.json").write_text(json.dumps({
            "engine": "NorthStarCpp", "name": "Binary Example",
            "schema_version": 2, "startup_scene": "main.nscn"
        }), encoding="utf-8")
        name, project, selected = project_info(self.project)
        self.assertEqual(selected, scene)
        target = self.root / "binary-game"
        self.assertEqual(copy_game_assets(project, target, selected), 0)
        self.assertEqual((target / "main.nscn").read_bytes(), scene.read_bytes())
        corrupt = bytearray(scene.read_bytes())
        corrupt[-1] ^= 0xFF
        scene.write_bytes(corrupt)
        with self.assertRaises(GameBuildError):
            project_info(self.project)
        self.scene = old_scene

    def test_v3_scene_can_exceed_legacy_size_limit(self):
        self.scene.write_bytes(b"NorthStarCpp.Scene 3\n" + (b"# scenery\n" * 62000))
        self.assertGreater(self.scene.stat().st_size, 512*1024)
        self.assertEqual(project_info(self.project)[2], self.scene)
        self.scene.write_bytes(b"NorthStarCpp.Scene 2\n" + (b"# scenery\n" * 62000))
        with self.assertRaises(GameBuildError):
            project_info(self.project)

    def test_animation_clips_exported_and_manifest_verified(self):
        folder=self.project/"animations/props"
        folder.mkdir(parents=True)
        data={
            "schema":1,
            "type":"scene-transform",
            "interpolation":"linear",
            "keyframes":[
                {"time":0,"translation":[0,0,0]},
                {"time":2,"translation":[0,2,0]}
            ]
        }
        (folder/"lift.anim.json").write_text(json.dumps(data),encoding="utf8")
        dst=self.root/"game"
        self.assertEqual(copy_game_animations(self.project,dst),1)
        self.assertEqual(
            json.loads((dst/"animations/props/lift.anim.json").read_text("utf8")),
            data)
        pkg=self.fake_release()
        target=pkg/"game/animations/props/lift.anim.json"
        target.parent.mkdir(parents=True)
        target.write_text(json.dumps(data),encoding="utf8")
        manifest=json.loads((pkg/"manifest.json").read_text("utf8"))
        build_manifest(pkg,"Test Example",
                       {"version":"0.1.0","build_type":"Release","host_abi":1},
                       "bullet",manifest["selected_plugins"],0,animations=1)
        self.assertEqual(verify_game(pkg)["animation_files"],1)
        build_manifest(pkg,"Test Example",
                       {"version":"0.1.0","build_type":"Release","host_abi":1},
                       "bullet",manifest["selected_plugins"],0,animations=0)
        with self.assertRaisesRegex(GameBuildError,"animation asset count"):
            verify_game(pkg)

    def test_invalid_animation_clip_rejected(self):
        folder=self.project/"animations"
        folder.mkdir()
        file=folder/"broken.anim.json"
        file.write_text('{"schema":2,"type":"scene-transform","keyframes":[]}')
        with self.assertRaisesRegex(GameBuildError,"Unsupported animation schema"):
            copy_game_animations(self.project,self.root/"game")
        file.write_text("broken json")
        with self.assertRaisesRegex(GameBuildError,"Invalid animation JSON"):
            copy_game_animations(self.project,self.root/"game")

    def test_animation_clip_keyframe_validation(self):
        folder=self.project/"animations"
        folder.mkdir()
        file=folder/"invalid.anim.json"
        template={
            "schema":1, "type":"scene-transform",
            "keyframes":[
                {"time":0,"translation":[0,0,0]},
                {"time":2,"translation":[0,2,0]}
            ]
        }
        for bad in (
            {"schema":1,"type":"scene-transform","keyframes":[]},
            {**template,"keyframes":[
                {"time":0,"translation":[0,0,0]},
                {"time":0,"translation":[0,1,0]}]},
            {**template,"keyframes":[
                {"time":0,"scale":[1,1,1]},
                {"time":2,"scale":[0,1,1]}]},
            {**template,"keyframes":[
                {"time":0,"translation":[0,0,0]},
                {"time":2,"translation":[100001,0,0]}]},
        ):
            file.write_text(json.dumps(bad),encoding="utf8")
            with self.assertRaises(GameBuildError):
                copy_game_animations(self.project,self.root/"game")

    def test_obj_scene_dependency_is_exported_and_missing_is_rejected(self):
        self.scene.write_text(
            'NorthStarCpp.Scene 3\n'
            'scene "Asset Test"\n'
            'entity 3 0 "Pedestal" "Default" "" "" "assets/models/pedestal.obj" '
            '0 0 0 1 c88f6e 0 0 0 0 0 1 1 1 0 1000000 1 1\n',
            encoding="utf8")
        with self.assertRaisesRegex(GameBuildError,"Missing or redirected OBJ"):
            copy_game_assets(self.project,self.root/"game",self.scene)
        folder=self.project/"assets/models"
        folder.mkdir(parents=True)
        (folder/"pedestal.obj").write_text(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n",encoding="utf8")
        self.assertEqual(
            copy_game_assets(self.project,self.root/"game",self.scene),1)
        self.assertTrue((self.root/"game/assets/models/pedestal.obj").is_file())

    def test_script_component_resources_exported(self):
        self.scene.write_text(
            'NorthStarCpp.Scene 3\n'
            'entity 1 0 "Box" "Default" "" "" "" 0 0 0 1 46b9d8 0 0 '
            '0 0 0 1 1 1 0 1000000 1 1\n'
            'script 1 "scripts/behaviors/cube.js"\n',encoding="utf8")
        source=self.project/"scripts/behaviors"
        source.mkdir(parents=True)
        (source/"cube.js").write_text("engine.on('entity.update',()=>{});",encoding="utf8")
        (self.project/"scripts/main.js").write_text("engine.on('start',()=>{});",encoding="utf8")
        count=copy_game_scripts(self.project,self.root/"game",self.scene)
        self.assertEqual(count,2)
        self.assertTrue((self.root/"game/assets/scripts/behaviors/cube.js").exists())
        self.assertTrue((self.root/"game/assets/scripts/main.js").exists())

    def test_reject_missing_script_component_resource(self):
        self.scene.write_text(
            'NorthStarCpp.Scene 3\n'
            'script 1 "scripts/missing.js"\n',encoding="utf8")
        (self.project/"scripts").mkdir()
        with self.assertRaisesRegex(GameBuildError,"Missing scene script"):
            copy_game_scripts(self.project,self.root/"game",self.scene)

    def test_script_manifest_requires_provider(self):
        pkg=self.fake_release()
        (pkg/"game/assets/scripts").mkdir(parents=True)
        (pkg/"game/assets/scripts/main.js").write_text("engine.on('start',()=>{});",encoding="utf8")
        providers=[]
        for cap in sorted(CAPABILITIES):
            providers.append({"capability":cap,"provider":"test",
                              "path":"bin/"+cap.split(".",1)[1]+"_test.dll"})
        build_manifest(pkg,"Test Example",
                       {"version":"0.1.0","build_type":"Release","host_abi":1},
                       "bullet",providers,0,scripts=1)
        with self.assertRaisesRegex(GameBuildError,"scripting provider"):
            verify_game(pkg)
        (pkg/"bin/scripting_v8.dll").write_bytes(b"mock v8 marker")
        providers.append({"capability":"engine.scripting","provider":"V8",
                          "path":"bin/scripting_v8.dll"})
        build_manifest(pkg,"Test Example",
                       {"version":"0.1.0","build_type":"Release","host_abi":1},
                       "bullet",providers,0,scripts=1)
        self.assertTrue(verify_game(pkg)["gameplay_scripting_supported"])

    def test_copy_v8_native_import_closure(self):
        engine = self.root / "engine"
        source_bin = engine / "bin"
        target_bin = self.root / "output-bin"
        source_bin.mkdir(parents=True)
        target_bin.mkdir()
        for name in ("libv8.dll", "libicuuc78.dll"):
            (source_bin / name).write_bytes(name.encode("ascii"))
        registry = {
            "files": {"bin/libv8.dll": {}, "bin/libicuuc78.dll": {}},
            "imports": {
                "bin/scripting_v8.dll": [
                    {"name": "libv8.dll", "status": "bundled"}],
                "bin/libv8.dll": [
                    {"name": "libicuuc78.dll", "status": "bundled"}],
                "bin/libicuuc78.dll": [],
            },
        }
        plugins = [{"path": "bin/scripting_v8.dll"}]
        self.assertEqual(copy_bundled_runtime(engine, target_bin, registry, plugins), 2)
        self.assertEqual((target_bin / "libv8.dll").read_bytes(), b"libv8.dll")
        self.assertEqual((target_bin / "libicuuc78.dll").read_bytes(), b"libicuuc78.dll")
        self.assertEqual(copy_bundled_runtime(engine, target_bin, registry, plugins), 0)
        (target_bin / "libv8.dll").write_bytes(b"wrong CRT binary")
        with self.assertRaisesRegex(GameBuildError, "Conflicting native runtime DLL"):
            copy_bundled_runtime(engine, target_bin, registry, plugins)

    def test_reject_scene_parent_traversal(self):
        manifest = self.project / "northstarcpp.project.json"
        manifest.write_text(json.dumps({
            "engine": "NorthStarCpp", "name": "test",
            "startup_scene": "../outside.nscene"}), encoding="utf-8")
        with self.assertRaises(GameBuildError):
            project_info(self.project)
        for invalid in ("../escape", "/absolute", "C:/Windows/file", "a//b", "a/./b"):
            with self.subTest(value=invalid):
                with self.assertRaises(GameBuildError):
                    checked_relative(self.project, invalid)

    def test_package_checksums_and_modification(self):
        pkg = self.fake_release()
        self.assertEqual(verify_game(pkg)["game_name"], "Test Example")
        (pkg / "game/main.nscene").write_bytes(b"altered")
        with self.assertRaisesRegex(GameBuildError, "checksum mismatch"):
            verify_game(pkg)

    def test_package_rejects_missing_provider(self):
        pkg = self.fake_release()
        (pkg / "bin/assets_test.dll").unlink()
        with self.assertRaises(GameBuildError):
            verify_game(pkg)

    def test_package_rejects_new_unregistered_files(self):
        pkg = self.fake_release()
        (pkg / "bin/unregistered.dll").write_bytes(b"unknown")
        with self.assertRaises(GameBuildError):
            verify_game(pkg)

    def test_untrusted_asset_symlink_rejected(self):
        assets = self.project / "assets"
        assets.mkdir()
        outside = self.root / "private.txt"
        outside.write_text("private", encoding="utf-8")
        try:
            (assets / "redirect.txt").symlink_to(outside)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Windows symlink creation unavailable: " + str(error))
        with self.assertRaisesRegex(GameBuildError, "Symlink"):
            copy_game_assets(self.project, self.root / "game", self.scene)


if __name__ == "__main__":
    unittest.main()
