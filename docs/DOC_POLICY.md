# NorthStarCpp — Documentation Policy

Updated: 2026-11-18. Maintainer scope: repository-wide documentation, not third-party source archives.

## One owner per fact

| Subject | Authoritative document | Supporting evidence |
| --- | --- | --- |
| Where to find documents | [Documentation Index](INDEX.md) | Automatically generated from Markdown files |
| Current build and release state | [Current Status](CURRENT_STATUS.md) | Dated QA reports and source/CTest |
| Toolchain, build and launch | [Quick Start](START_HERE.md) | CMakePresets.json, launcher, CI |
| Engine boundaries / provider policy | [Architecture](ARCHITECTURE.md), [Modules](MODULES.md) | ABI headers and tests |
| Gameplay / Player / CharacterMotor | [Gameplay Production Gates](GAMEPLAY_FRAMEWORK_PRODUCTION_GATES_2026-11-18.md) | Earlier phase reports are historical |
| YMAT format | [XML YMAT Migration](YMAT_XML_ONLY_MIGRATION_2026-11-18.md) | YMAT assets, parser and migration tests |
| Asset identity and cooking | [Asset Pipeline P1](ASSET_PIPELINE_P1_FOUNDATION_2026-11-18.md) | Registry/packer tools |
| Release requirements | [1.0 Execution Plan](NORTHSTARCPP_1_0_EXECUTION_PLAN_2026-11-18.md) | Independent run-specific QA logs |

The current state is summarized in [Current Status](CURRENT_STATUS.md). Domain-specific documents own their detailed contracts. The catalog links to source files; it must not reproduce them.

## Document types

**Reference/specification:** Maintained without the date-stamped engineering history in the main body. When implementation changes, update the contract and its validation.

**Dated engineering report:** An immutable-in-meaning record of what was observed, changed, built or blocked at the stated time. Preserve measurements, test commands, code paths, dates, cautions and provenance. A green historical test is not proof about a newer checkout. Do not silently rewrite or erase historical findings.

**Project/template README:** Intended to remain useful if copied into an independent project. Identical text between an instantiated project and its reusable template is *intentional duplication* and must not be automatically removed. Migration snapshot READMEs follow the same archival exception.

## Style and evidence

- One level-one heading naming the document. Use second-level headings for content, third-level for subsections. Use relative Markdown links for local files.
- Prefer a short introductory statement of scope, date and applicability. Avoid copying long explanations of subsystems already defined elsewhere; link to their owner instead.
- Label every observation as **implemented**, **tested**, **not verified**, **known issue**, or **planned**. Tests and builds must include their target/build root, and explicitly distinguish a focused smoke test from a full clean-build/release acceptance.
- State actual units for measurements. CPU wall-time around draw submission is not GPU execution time. Do not turn a test result into an estimated FPS uplift.
- Refer to stable files and API names using source-accurate spelling. Historical paths may be quoted in historical reports but must not become the current Quick Start.
- Remove verbatim duplicate technical paragraphs only when a canonical owner exists and the original details remain available at that owner. Never replace data or old findings with unsupported assertions.
- Do not rename/move project docs or overwrite third-party licensing text to satisfy a formatting preference.
- Preserve unrelated local edits. No automated commit, push or production dist replacement.

## Audit and catalog

From repository root:

~~~powershell
python tools/docs_audit.py --write-index
python tools/docs_audit.py --check
python tools/docs_audit.py --json
~~~

The audit verifies local relative links and heading structure, and reports exact repeated long paragraphs. Exact repeats within project templates and migration snapshots may be legitimate. A regenerated catalog is deterministic and derived from file names and titles; do not manually keep a separate metadata list in sync.
