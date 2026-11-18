# NorthStarCpp — Documentation Consolidation Audit

Date: 2026-11-18. Scope: repository-owned Markdown documentation in the engine, project, tooling and reference trees. No engine sources, asset contents or generated production binaries were changed by this documentation pass.

## Consolidation decisions

- **One current status entry point:** [Current Status](CURRENT_STATUS.md) distinguishes verified focused QA, known limitations and release gates. Per-test evidence remains with the dated engineering report that produced it.
- **One documentation catalog:** [Documentation Index](INDEX.md) is generated, not a separately maintained metadata list. It covers engine-wide specifications, historical engineering reports, projects, templates and migration snapshots.
- **One ownership policy:** [Documentation Policy](DOC_POLICY.md) specifies which document owns a contract, how to cite dated QA and when repeated material is intentionally self-contained.
- **Current Quick Start:** [START_HERE.md](START_HERE.md) uses CMake presets, Scene V3 and native Editor/Player rather than missing example files or the removed player ownership flag. Original Scene V2, Bullet and launcher instructions are preserved in [history](history/START_HERE_SCENE_V2_ARCHIVE_2026-11-18.md).
- **One copy of architecture validation commands:** [ENGINE_LAYOUT.md](ENGINE_LAYOUT.md) now links to the sequence in [ARCHITECTURE.md](ARCHITECTURE.md).
- **YMAT format ownership:** [Asset Pipeline](ASSET_PIPELINE_P1_FOUNDATION_2026-11-18.md) links to [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md) rather than repeating the three converted input paths and 160-file source audit.
- **Gameplay evidence ownership:** [Current Status](CURRENT_STATUS.md) links to the original [Production Gates QA](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md) instead of duplicating test names, counts and CPU timing samples.
- **Historical reconciliation:** Early YMAT kind-30, Gameplay runtime phase and Unreal parity reports retain their original findings with a clear link explaining later supersession. The 1.0 plan retains older failed-test evidence as a dated snapshot.
- **README consistency:** Version badge now matches CMake's 0.7.0 source version. Outdated dated-readiness links have been distinguished from current engineering status.

## Removed nonessential mirrored text

Three engine-wide Sea of Rust research/QA notes were mirrors of full project-owned originals: the native-render corruption fix, asset research, and Bastion visual rework. Their old filenames remain valid as small forwarding documents. The complete source evidence remains under Projects/MovingCitiesPrototype/docs. A one-line SDK module-name discrepancy between two Bastion reports was explicitly retained instead of silently discarded.

Other repeated long paragraphs remain intentionally in standalone project templates, instantiated project READMEs and immutable migration/rollback snapshots. Those copies are exempt from the duplicate-text gate because deleting them would damage template portability or historical reproducibility.

## Verification

The new tools/docs_audit.py checks Markdown headings, relative local file links, catalog freshness and exact repeated long prose paragraphs across all documented roots. Its documented exception list covers self-contained template/snapshot copies; any new unclassified repeated long paragraph fails the check.

The tool is registered as the northstarcpp_documentation_audit CTest through the existing Python3 configuration. This is a documentation-only check and does not assert that all existing engineering claims are automatically validated by their text.

No project files were deleted, no commit/push occurred, and production dist was not overwritten.
