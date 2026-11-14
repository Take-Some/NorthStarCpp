# NorthStarCpp — Authorship and Source Provenance

**Project:** NorthStarCpp  
**Developer and maintainer:** Kayla Verner  
**Current development year:** 2026  
**Company:** Kayla's Systems  
**Public signature:** 2026 Kayla's Systems — Kayla Verner

## Headers for project-owned source files

```cpp
// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
```

The date 2026 identifies the NorthStarCpp update, not the original creation date of every included component.

## Third-party source code and notices

Original author and copyright holder notices in `external/` remain intact, including historical Kayla's Systems, Bullet, and Flecs sources. Renaming or adapting source code does not transfer exclusive intellectual-property rights. All original license notices and redistribution terms must be followed.

## Maintenance

```powershell
python tools/update_project_headers.py --check
python tools/update_project_headers.py --apply
python -m unittest discover -s tests -p "test_northstarcpp_branding.py"
```

The header updater targets the project-owned NorthStarCpp layer and creates backup ZIP archives in `.northstarcpp/backups/`. It excludes files containing original third-party copyright notices.

## Project attribution on imported source files

A separate project-maintainer line is added alongside historical copyright notices:

```cpp
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
```

The original Copyright line remains unchanged. Check or reapply this attribution with:

```powershell
python tools/add_northstarcpp_attribution.py --check
python tools/add_northstarcpp_attribution.py --apply
```

The updater creates ZIP backups under `.northstarcpp/backups/` before changing files. It only adds an adjacent comment; it does not rewrite the original notice.
