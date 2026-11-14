# NorthStarCpp — Application Branding and Windows Icons

## Centralized icon directory

All first-party application and compatibility icons are stored in the top-level `icons/` directory. Icon files are tracked as build inputs; released Windows executables embed the relevant resources and do not need adjacent loose ICO files.

The canonical NorthStar Engine brand icon is `icons/logoStar.ico` (64, 128, 256 pixel frames). Preserve the original brand asset without modification.

## Windows application icon mapping

| Application | Source icon |
| --- | --- |
| `northstarcpp_editor.exe` | `icons/northstar-editor.ico` |
| `northstarcpp_project_manager.exe` | `icons/logoStar.ico` |
| `northstarcpp_player.exe` | `icons/logoStar.ico` |
| `northstarcpp_camera_timeline_editor.exe` | `icons/logoStar.ico` |
| `bugtrap.exe` | `icons/Bug.ico` |
| `shader_generator.exe` | `icons/shader_generator.ico` |

CMake configures each icon from `engine/northstarcpp/resources/*.rc.in`. The editor icon uses its own resource template but retains `IDI_NORTHSTARCPP_APP=101` for existing window icon loading. BugTrap has its separate `IDI_BUGTRAP_APP=102` resource, and Shader Generator has `IDI_SHADER_GENERATOR=103`.

The Project Browser continues to bundle the logo through Qt resources at `:/logoStar.ico`; its source is now `icons/logoStar.ico`. Compatibility module resources use source-relative paths to the same centralized directory.

## Separate UI icon vocabulary

**Font Awesome Free** is intended for routine interface actions (folders,
search, plus, favorites, filters, status, etc.) and is *not* an alternative
engine logo. Glyphs should be embedded, use stable glyph mappings and retain
the original license notices before replacing temporary symbols. Including
the official engine logo does not imply that the Font Awesome migration is
complete.

## Validation

Windows-only `northstarcpp_logo_resources` (CTest) opens each native EXE
as a resource container and validates the group-icon payload and frame
dimensions. The BugTrap smoke test checks its icon and version information;
Project Browser smoke verifies its window and brand resource.

Typical commands:

```powershell
cmake --build out/cpp-branded-logo-validation2 --target northstarcpp_windows_logo_smoke
ctest --test-dir out/cpp-branded-logo-validation2 -R "northstarcpp_(logo_resources|bugtrap|project_manager)" --output-on-failure
```

All engine-provider DLLs and native EXEs must continue to share one runtime
directory by default, unless `bootstrap.json` selects an alternative
directory for module discovery.
