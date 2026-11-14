# V8 15.8.62 Developer Sidecar — Refactoring and Performance

Date: 2026-11-14
Scope: engine/northstarcpp/modules/scripting/v8_sidecar/v8_sidecar.cpp,
tools/v8_provider_sidecar/v8_worker.js, tests/scripting_smoke.cpp.
Development-only integration; production dist unchanged.

## Improvements

1. Hex IPC decoder validates each input pair once using an ASCII lookup
   table and builds a single percent-encoded sequence for URI decoding,
   avoiding repeated parseInt and intermediate string concatenation.
2. Benchmarks showed an array-based hex encoder would be slower in this V8
   build; the original faster string-concatenating encoder was retained.
3. Engine capability proxies and native method closures are now cached per
   Realm, reducing allocations in per-frame engine.* invocation paths.
4. Realm bootstrap source is serialized only once and reused after reset.
5. Ordinary JS exceptions now preserve persistent JS state and the running
   subprocess; protocol corruption, timeouts and fatal errors still terminate
   it. The native smoke asserts state continuity after a caught exception.

## Decoder microbenchmark: V8 15.8.62 on this machine

| Payload (chars) | Iterations | Old decoder | Updated decoder | Speed ratio |
| --- | ---: | ---: | ---: | ---: |
| 64 | 2,000 | 24.392 ms | 9.217 ms | 2.65x |
| 4,096 | 500 | 364.552 ms | 101.180 ms | 3.60x |
| 65,536 | 50 | 579.982 ms | 176.981 ms | 3.28x |

These are single-run local JS microbenchmarks, NOT end-to-end frame-rate,
CPU/GPU usage, or statistically reliable p95/p99 measurements.
The array-based encoder was slower (64-char: 8.47 vs 7.505 ms,
65536-char: 207.171 vs 113.651 ms), so it was reverted.

## Acceptance

- Sidecar Release DLL rebuilt successfully.
- Native scripting smoke rebuilt and extended for state preservation.
- verify.py --e2e passed four tests: V8 ABI2 execution, script-service
  override, installed Player source game, installed Player FAT/DB archive game.
- Native test FPS --warp --check-scene also passed.
- Provider and worker SHA-256 entries refreshed in sidecar-provenance.json.
- Production dist and the QA engine package were not replaced.

## Remaining

Developer d8 is not a secure, reviewed embeddable V8 SDK for public release.
Blocking Windows pipe writes and process-per-context startup are remaining
candidates for later optimization, requiring cancellation and stress tests.
