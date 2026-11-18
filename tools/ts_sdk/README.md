# NorthStarCpp TypeScript SDK tooling

The SDK ships a pinned **TypeScript 5.9.3** compiler and a persistent
TypeScript Language Service used by the Qt JavaScript/TypeScript editor.

## Setup

```powershell
npm ci --prefix tools/ts_sdk --ignore-scripts --no-audit --no-fund
npm run typecheck --prefix tools/ts_sdk
npm run service-smoke --prefix tools/ts_sdk
```

`tsconfig.json` type-checks the authoritative embedded declarations and
both public SDK TypeScript examples with `strict` and `noEmit`.
It does not require a globally installed `tsc`. No generated JavaScript
or TypeScript package files are committed in `node_modules`.

## Editor completion

`ts_language_service.cjs` runs as a **single persistent subprocess**
for all open Qt source tabs. It accepts newline-delimited JSON requests
containing the in-memory current JavaScript/TypeScript text, UTF-16 cursor
offset and typed prefix. The process uses TypeScript's real symbol
checker, contextual types and completion-entry documentation, including
properties and function methods. It never executes project source.

The Qt editor resolves the service relative to the engine workspace,
or from the optional `NORTHSTARCPP_TS_SERVICE` override. Node must be
available on PATH, and `typescript` installed locally by `npm ci`.
If Node or the tooling is missing, the existing embedded `.d.ts` index
continues to provide basic API completion. The UI thread is never blocked
waiting for the TypeScript checker (requests are debounced by 75ms).

## Tests

`ts_language_service_smoke.cjs` checks contextual TypeScript and
JavaScript completion, typed `UiDeclaration.children`, `UiProperties.width`
and SDK method signatures.

CMake registers `northstarcpp_ui_declarative_types` and
`northstarcpp_ts_language_service` when the local pinned compiler
is installed. The Qt editor integration is validated by
`northstarcpp_qt_text_asset`, including actual semantic completion
of `menu.render`.

## Scope

This is semantic completion with editor-side type inference, not a full
Language Server Protocol implementation. Diagnostics squiggles, rename,
cross-file references, refactoring, and formatting are separate features.
