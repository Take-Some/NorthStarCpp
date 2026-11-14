# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Project Browser model: discovery, templates, metadata, favourites and recent projects.

Only the project manifest is modified by rename/update; forgetting a project
NEVER deletes user files. The local library is an atomic JSON index, not a
replacement for the individual project manifests.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any

from .cli import ROOT
from .scene_binary import encode_legacy_template

MANIFEST = "northstarcpp.project.json"
STARTUP_SCENE = "main.nscn"
LIBRARY_NAME = "project_library.json"
CATALOG_SCHEMA = 1
PROJECT_SCHEMA = 2
MAX_MANIFEST_BYTES = 128 * 1024
WIN_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *("COM" + str(i) for i in range(1, 10)),
    *("LPT" + str(i) for i in range(1, 10)),
}


class LauncherError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProjectTemplate:
    id: str
    title: str
    category: str
    description: str
    accent: str
    scene: str


def _scene(*lines: str) -> str:
    return "NorthStarCpp.Scene 2\n" + "\n".join(lines) + "\n"


TEMPLATES: tuple[ProjectTemplate, ...] = (
    ProjectTemplate("blank", "Blank Project", "General",
        "An empty scene for starting a new project.", "#64748b", _scene()),
    ProjectTemplate("sandbox", "Sandbox", "Games",
        "A player and landmarks for prototyping movement.",
        "#4cbdc3", _scene(
            'cube "Player" 0 0 0 1.5 46b9d8 1 1.5',
            'cube "Landmark A" -3 0 3 1 f2a65a 0 0',
            'cube "Landmark B" 3 0 3 1 9bd783 0 0')),
    ProjectTemplate("physics", "Physics Playground", "Simulation",
        "A playground for testing gravity, collisions, and dynamic bodies.",
        "#d99458", _scene(
            'cube "Platform" 0 0 0 8 64748b 0 0',
            'cube "Falling Body A" -1 8 0 1 ffcc66 0 2',
            'cube "Falling Body B" 1 11 0 1 f28c91 0 3',
            'cube "Player" 3 0 0 1 46b9d8 1 1')),
    ProjectTemplate("city", "City Block", "Worlds",
        "Starter geometry for prototyping a city block.",
        "#b6686d", _scene(
            'cube "Road Marker" 0 0 0 6 29313b 0 0',
            'cube "Building A" -5 0 1 3 8294a4 0 0',
            'cube "Building B" 5 0 1 3 8294a4 0 0',
            'cube "Player" 0 0 -5 1 46b9d8 1 1')),
    ProjectTemplate("architecture", "Architecture", "Visualization",
        "A basic scene for visualization and layout.",
        "#8d83c8", _scene(
            'cube "Floor" 0 0 0 9 7e8897 0 0',
            'cube "Volume A" -2 3 0 2 a7b8ca 0 0',
            'cube "Volume B" 2 3 0 2 e0caa6 0 0')),
)
TEMPLATE_MAP = {t.id: t for t in TEMPLATES}


@dataclass(frozen=True)
class Project:
    name: str
    directory: Path
    description: str = ""
    category: str = "General"
    template: str = "blank"
    engine_version: str = ""
    tags: tuple[str, ...] = ()
    startup_scene: str = STARTUP_SCENE
    game_mode: str = "scene"
    favorite: bool = False
    last_opened: str = ""
    created_at: str = ""
    modified_at: str = ""
    status: str = "ready"
    status_reason: str = ""
    external: bool = False

    @property
    def can_open(self) -> bool:
        return self.status == "ready"

    @property
    def scene_path(self) -> Path:
        return self.directory / self.startup_scene

    @property
    def thumbnail_path(self) -> Path | None:
        path = self.directory / "thumbnail.png"
        if path.is_file() and not _is_redirect(path):
            return path
        return None


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _key(path: Path) -> str:
    return os.path.normcase(str(Path(path).expanduser().resolve()))


def _is_redirect(path: Path) -> bool:
    return path.is_symlink() or bool(getattr(path, "is_junction", lambda: False)())


def _safe_scene(value: str) -> bool:
    if not isinstance(value, str) or not value.endswith((".nscn", ".nscene")) or "\x00" in value:
        return False
    if ":" in value or "\\" in value or value.startswith("/"):
        return False
    return not any(x in ("", ".", "..") for x in value.split("/"))


def _atomic_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_name = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix="." + path.name + "-", suffix=".tmp", delete=False
        ) as stream:
            temp_name = stream.name
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_name, path)
    finally:
        if temp_name is not None:
            Path(temp_name).unlink(missing_ok=True)


def _read_json(path: Path) -> dict[str, Any]:
    if _is_redirect(path):
        raise LauncherError("Manifest is a symbolic link or junction.")
    if path.stat().st_size > MAX_MANIFEST_BYTES:
        raise LauncherError("Project manifest exceeds 128 KiB.")
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, ValueError) as exc:
        raise LauncherError("Cannot parse JSON: " + str(exc)) from exc
    if not isinstance(value, dict):
        raise LauncherError("Project manifest must contain a JSON object.")
    return value


def _engine_version(root: Path) -> str:
    config = root / "CMakeLists.txt"
    try:
        text = config.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"project\s*\(\s*NorthStarCpp\s+VERSION\s+(\d+\.\d+\.\d+)", text, re.I)
        return match.group(1) if match else "0.1.0"
    except OSError:
        return "0.1.0"


def _compatible(target: str, current: str) -> bool:
    if not target:
        return True  # pre-version legacy projects
    try:
        a = tuple(int(x) for x in target.split(".")[:3])
        b = tuple(int(x) for x in current.split(".")[:3])
        if len(a) != 3 or len(b) != 3:
            return False
        return a[0] == b[0] and a <= b
    except (TypeError, ValueError):
        return False


class ProjectManager:
    def __init__(self, root: Path = ROOT):
        self.root = Path(root).expanduser().resolve()
        self.directory = self.root / "Projects"
        self.library_file = self.root / ".northstarcpp" / LIBRARY_NAME
        self.engine_version = _engine_version(self.root)

    @staticmethod
    def folder_name(name: str) -> str:
        label = name.strip()
        if not label or len(label) > 64:
            raise LauncherError("Project name must contain 1–64 characters.")
        if any(ord(c) < 32 or c in '<>:"/\\|?*' for c in label):
            raise LauncherError("Project name contains forbidden path characters.")
        if label.endswith((".", " ")) or label in (".", ".."):
            raise LauncherError("Invalid project name.")
        result = re.sub(r"\s+", "_", label)
        if result.startswith(".") or result.split(".")[0].upper() in WIN_RESERVED:
            raise LauncherError("Reserved Windows filename.")
        return result

    def _library(self) -> dict[str, Any]:
        if not self.library_file.is_file():
            return {"schema": CATALOG_SCHEMA, "projects": {}, "hidden": []}
        try:
            data = _read_json(self.library_file)
        except LauncherError as exc:
            raise LauncherError("Project library is damaged: " + str(exc)) from exc
        if data.get("schema") != CATALOG_SCHEMA or not isinstance(data.get("projects"), dict):
            raise LauncherError("Unsupported project library schema; file was not overwritten.")
        if not isinstance(data.get("hidden", []), list):
            raise LauncherError("Invalid hidden-project list.")
        return data

    def _save(self, data: dict[str, Any]) -> None:
        _atomic_json(self.library_file, data)

    def _project(self, folder: Path, state: dict[str, Any] | None = None) -> Project:
        state = state or {}
        folder = Path(folder).expanduser().resolve()
        common = {
            "favorite": bool(state.get("favorite", False)),
            "last_opened": str(state.get("last_opened", "")),
            "external": self.directory.resolve() not in folder.parents,
        }
        if not folder.is_dir():
            return Project(str(state.get("name", folder.name)), folder,
                           status="missing", status_reason="Project directory not found", **common)
        file = folder / MANIFEST
        if not file.is_file():
            return Project(str(state.get("name", folder.name)), folder,
                           status="invalid", status_reason="Project manifest not found", **common)
        try:
            data = _read_json(file)
            if data.get("engine") != "NorthStarCpp":
                raise LauncherError("Not a NorthStarCpp project.")
            schema = data.get("schema_version", 1)
            if type(schema) is not int or not 1 <= schema <= PROJECT_SCHEMA:
                raise LauncherError("Unsupported project schema version.")
            raw_name = data.get("name", folder.name)
            if not isinstance(raw_name, str) or not raw_name.strip():
                raise LauncherError("Project name is missing.")
            raw_scene = data.get("startup_scene", STARTUP_SCENE)
            mode = data.get("game_mode", "scene")
            if mode not in ("scene", "fps", "explore"):
                raise LauncherError("Unsupported project game_mode")
            if not _safe_scene(raw_scene):
                raise LauncherError("Startup scene path is unsafe.")
            scene = folder / raw_scene
            if not scene.is_file() or _is_redirect(scene):
                status, reason = "missing_scene", "Startup scene missing or redirected"
            elif folder not in scene.resolve().parents or any(
                _is_redirect(x) for x in scene.parents if x == folder or folder in x.parents
            ):
                status, reason = "invalid", "Startup scene resolves outside project root"
            elif not _compatible(data.get("engine_version", ""), self.engine_version):
                status, reason = "incompatible", "Engine version is not compatible"
            else:
                status, reason = "ready", ""
            raw_tags = data.get("tags", [])
            tags = tuple(str(x) for x in raw_tags[:12]) if isinstance(raw_tags, list) else ()
            modified = datetime.fromtimestamp(file.stat().st_mtime, timezone.utc).isoformat(timespec="seconds")
            return Project(
                name=raw_name, directory=folder,
                description=str(data.get("description", ""))[:1024],
                category=str(data.get("category", "General"))[:80],
                template=str(data.get("template", "blank"))[:60],
                engine_version=str(data.get("engine_version", "")),
                tags=tags, startup_scene=raw_scene, game_mode=mode,
                created_at=str(data.get("created_at", "")),
                modified_at=modified, status=status, status_reason=reason, **common,
            )
        except (OSError, LauncherError, TypeError, ValueError) as exc:
            return Project(str(state.get("name", folder.name)), folder,
                           status="invalid", status_reason=str(exc), **common)

    def list(self) -> list[Project]:
        library = self._library()
        found: dict[str, Path] = {}
        hidden = set(library.get("hidden", []))
        if self.directory.is_dir():
            for entry in self.directory.iterdir():
                if entry.is_dir() and not _is_redirect(entry) and (entry / MANIFEST).is_file():
                    found[_key(entry)] = entry
        for key, state in library["projects"].items():
            path = state.get("path") if isinstance(state, dict) else None
            if isinstance(path, str) and path:
                found[_key(Path(path))] = Path(path)
        return sorted(
            (self._project(path, library["projects"].get(key, {}))
             for key, path in found.items() if key not in hidden),
            key=lambda item: (
                not item.favorite,
                not bool(item.last_opened),
                item.last_opened and "".join(chr(255-ord(c)) for c in item.last_opened) or "",
                item.name.casefold(),
            ),
        )

    def get(self, path: Path) -> Project:
        key = _key(path)
        data = self._library()
        return self._project(Path(path), data["projects"].get(key, {}))

    def _track(self, project: Project, **updates: Any) -> None:
        data = self._library()
        key = _key(project.directory)
        record = data["projects"].get(key, {})
        record.update({"path": str(project.directory), "name": project.name, **updates})
        data["projects"][key] = record
        data["hidden"] = [x for x in data.get("hidden", []) if x != key]
        self._save(data)

    def register(self, path: Path) -> Project:
        folder = Path(path).expanduser()
        if folder.name.casefold() == MANIFEST.casefold() and folder.is_file():
            folder = folder.parent
        if _is_redirect(folder):
            raise LauncherError("Linked project directories cannot be imported.")
        project = self._project(folder)
        if project.status in ("invalid", "missing"):
            raise LauncherError("Not a valid project: " + project.status_reason)
        self._track(project)
        return self.get(project.directory)

    def scan(self, folder: Path, depth: int = 3, max_projects: int = 500) -> list[Project]:
        start = Path(folder).expanduser().resolve()
        if not start.is_dir():
            raise LauncherError("Search directory does not exist.")
        if _is_redirect(start):
            raise LauncherError("Cannot scan a redirected directory.")
        depth = min(max(0, int(depth)), 6)
        queue = [(start, 0)]
        found: list[Project] = []
        inspected = 0
        while queue:
            directory, level = queue.pop(0)
            inspected += 1
            if inspected > 5000:
                break
            if (directory / MANIFEST).is_file():
                try:
                    found.append(self.register(directory))
                except LauncherError:
                    pass
                if len(found) >= max_projects:
                    break
                continue
            if level >= depth:
                continue
            try:
                for child in directory.iterdir():
                    if child.is_dir() and not _is_redirect(child) and not child.name.startswith("."):
                        queue.append((child, level + 1))
            except (OSError, PermissionError):
                continue
        return found

    def create(self, name: str, template: str = "sandbox",
               location: Path | None = None, description: str = "",
               category: str | None = None) -> Project:
        folder_name = self.folder_name(name)
        if template not in TEMPLATE_MAP:
            raise LauncherError("Unknown project template: " + template)
        chosen = TEMPLATE_MAP[template]
        parent = self.directory if location is None else Path(location).expanduser().resolve()
        if parent.exists() and (not parent.is_dir() or _is_redirect(parent)):
            raise LauncherError("Project location is not a regular directory.")
        target = parent / folder_name
        if target.exists():
            raise LauncherError("Project already exists: " + str(target))
        parent.mkdir(parents=True, exist_ok=True)
        target.mkdir(exist_ok=False)
        try:
            for subdir in ("assets", "scripts", "config"):
                (target / subdir).mkdir()
            (target / STARTUP_SCENE).write_bytes(encode_legacy_template(chosen.scene))
            # Game templates are script-first. World/ECS/physics stay native.
            if chosen.id in ("sandbox", "physics"):
                template_script = ROOT / "tools" / "templates" / "northstar_first_person.js"
                script_source = template_script.read_text(encoding="utf-8")
                script_source = script_source.replace(
                    '"__NORTHSTAR_PROJECT_TITLE__"',
                    json.dumps(name.strip(), ensure_ascii=False))
                (target / "scripts" / "main.js").write_text(
                    script_source, encoding="utf-8")
            meta = {
                "schema_version": PROJECT_SCHEMA, "engine": "NorthStarCpp",
                "engine_version": self.engine_version, "name": name.strip(),
                "description": description.strip()[:1024],
                "category": category or chosen.category,
                "template": chosen.id, "tags": [], "startup_scene": STARTUP_SCENE,
                "game_mode": "explore" if chosen.id in ("sandbox", "physics") else "scene",
                "created_at": _now(), "state": "scaffold",
            }
            if chosen.id in ("sandbox", "physics"):
                meta["scripting"] = {"runtime": "V8", "entry": "scripts/main.js"}
            _atomic_json(target / MANIFEST, meta)
            (target / "README.md").write_text(
                "# " + name.strip() + "\n\n" + chosen.description + "\n\n"
                "NorthStarCpp project, scene: main.nscn. "
                "JavaScript gameplay can be placed in scripts/main.js; runtime execution requires an installed V8 scripting provider.\n",
                encoding="utf-8",
            )
            return self.register(target)
        except Exception:
            shutil.rmtree(target, ignore_errors=True)
            raise

    def set_favorite(self, project: Project | Path, value: bool) -> Project:
        item = self.get(project.directory if isinstance(project, Project) else project)
        if item.status == "missing":
            raise LauncherError("Cannot favourite a missing project.")
        self._track(item, favorite=bool(value))
        return self.get(item.directory)

    def mark_opened(self, project: Project | Path) -> Project:
        item = self.get(project.directory if isinstance(project, Project) else project)
        if not item.can_open:
            raise LauncherError("Project is not ready: " + item.status_reason)
        self._track(item, last_opened=_now())
        return self.get(item.directory)

    def rename(self, project: Project | Path, name: str) -> Project:
        self.folder_name(name)
        item = self.get(project.directory if isinstance(project, Project) else project)
        if not item.can_open:
            raise LauncherError("Cannot rename a project that is not ready.")
        meta = item.directory / MANIFEST
        data = _read_json(meta)
        data["name"] = name.strip()
        _atomic_json(meta, data)
        self._track(item, name=name.strip())
        return self.get(item.directory)

    def duplicate(self, project: Project | Path, name: str,
                  location: Path | None = None) -> Project:
        item = self.get(project.directory if isinstance(project, Project) else project)
        if not item.can_open:
            raise LauncherError("Cannot duplicate an unavailable project.")
        new_name = self.folder_name(name)
        target_parent = self.directory if location is None else Path(location).expanduser().resolve()
        if not target_parent.is_dir():
            raise LauncherError("Destination parent does not exist.")
        target = target_parent / new_name
        if target.exists():
            raise LauncherError("Destination project already exists.")
        if item.directory == target_parent or item.directory in target_parent.parents:
            raise LauncherError("Cannot duplicate a project into itself.")
        for node in item.directory.rglob("*"):
            if _is_redirect(node):
                raise LauncherError("Project contains redirected files; duplication refused.")
        try:
            shutil.copytree(item.directory, target)
            manifest = target / MANIFEST
            data = _read_json(manifest)
            data["name"] = name.strip()
            data["created_at"] = _now()
            _atomic_json(manifest, data)
            return self.register(target)
        except Exception:
            if target.exists():
                shutil.rmtree(target, ignore_errors=True)
            raise

    def forget(self, project: Project | Path) -> None:
        folder = project.directory if isinstance(project, Project) else Path(project)
        key = _key(folder)
        data = self._library()
        data["projects"].pop(key, None)
        if key not in data["hidden"]:
            data["hidden"].append(key)
        self._save(data)

    def set_thumbnail(self, project: Project | Path, source: Path) -> Project:
        """Install a bounded user-selected PNG thumbnail without changing the project manifest."""
        item = self.get(project.directory if isinstance(project, Project) else project)
        if not item.can_open:
            raise LauncherError("Project is not available for thumbnail changes.")
        source = Path(source).expanduser()
        if not source.is_file() or _is_redirect(source) or source.suffix.lower() != ".png":
            raise LauncherError("Choose a regular PNG image file.")
        size = source.stat().st_size
        if size < 33 or size > 4 * 1024 * 1024:
            raise LauncherError("Preview PNG must be between 33 bytes and 4 MiB.")
        with source.open("rb") as handle:
            header = handle.read(33)
        if header[:8] != bytes((137, 80, 78, 71, 13, 10, 26, 10)) or header[12:16] != b"IHDR":
            raise LauncherError("Invalid PNG image.")
        w = int.from_bytes(header[16:20], "big")
        h = int.from_bytes(header[20:24], "big")
        if not (1 <= w <= 2048 and 1 <= h <= 2048):
            raise LauncherError("Preview dimensions exceed the 2048 px limit.")
        destination = item.directory / "thumbnail.png"
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode="wb", dir=item.directory,
                                             prefix=".thumbnail-", suffix=".png",
                                             delete=False) as handle:
                temp_name = handle.name
                with source.open("rb") as image:
                    shutil.copyfileobj(image, handle)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temp_name, destination)
        finally:
            if temp_name:
                Path(temp_name).unlink(missing_ok=True)
        return self.get(item.directory)

    def update_details(self, project: Project | Path, *,
                       description: str | None = None, tags: list[str] | None = None,
                       category: str | None = None) -> Project:
        item = self.get(project.directory if isinstance(project, Project) else project)
        if not item.can_open:
            raise LauncherError("Cannot edit an unavailable project.")
        data = _read_json(item.directory / MANIFEST)
        if description is not None:
            data["description"] = description.strip()[:1024]
        if tags is not None:
            data["tags"] = [tag.strip()[:40] for tag in tags[:12] if tag.strip()]
        if category is not None:
            data["category"] = category.strip()[:80] or "General"
        _atomic_json(item.directory / MANIFEST, data)
        return self.get(item.directory)
