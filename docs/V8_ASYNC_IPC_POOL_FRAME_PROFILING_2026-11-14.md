# V8 15.8.62 — Overlapped IPC, worker pool and frame profiling

Date: 2026-11-14
Project root: C:/Users/AidenFox/Documents/NorthStarCpp
Status: developer-only implementation. Production dist unchanged.

## Implementation

1. Replaced the parent-facing synchronous anonymous Windows pipes with
   overlapped named-pipe I/O. Both ReadFile and WriteFile can be pending.
   WaitForMultipleObjects responds to I/O completion, subprocess exit,
   and explicit cancellation without busy polling.
2. CancelIoEx aborts pending operations; completion is awaited before the
   OVERLAPPED request/event are released. This prevents use-after-free
   on timed-out or cancelled I/O. Deadline also covers request writes.
3. The optional northstar_v8_sidecar_cancel(context) entrypoint can be
   called from another thread during evaluation. Caller must not race
   cancellation with context destruction. Generic engine.scripting ABI 2
   is unchanged and evaluate remains synchronous to the host.
4. Context initialization is lazy; a reset-before-release pool retains
   up to three idle d8 processes. Active contexts are independent, with
   separate processes and pipes. The pool never shares JS global state.
5. Exact ScriptRuntime::emit_event() source-form events bypass repeated
   Realm.eval compilation via a cached Realm.shared event dispatch.
   Arbitrary JavaScript still follows the original Realm.eval code path.

## 1,000,000-event soak

Reproduce:

    python tools/v8_provider_sidecar/stress.py --frames 1000000 --contexts 6

Checks: six concurrently active V8 contexts, warm process reuse with no
state leak, 540 kB input, explicit cancellation of a busy JS loop,
cancellation of a blocked overlapped WriteFile, timeout/restart,
one million engine-event IPC roundtrips and native resource accounting.

| Quantity | Measurement |
| --- | ---: |
| Script IPC p50 | 0.0244 ms |
| Script IPC p95 | 0.0471 ms |
| Script IPC p99 | 0.0773 ms |
| Script IPC maximum | 6.6518 ms |
| Script IPC mean | 0.0274 ms |
| Cancellation during infinite loop | 130.334 ms |
| Cancellation during blocked write | 166.207 ms |
| Total test wall time | 29.952 s |
| V8 private bytes before | 22,999,040 |
| V8 private bytes after | 32,223,232 |
| V8 private-byte growth | 9,224,192 |
| Native host handles before/after | 117 / 117 |

Memory checkpoints after 500k, 750k and 1m calls: 32,108,544,
32,145,408 and 32,182,272 private bytes respectively. The tested
workload stabilized after initial JIT/heap warmup; that does not prove
absence of leaks for arbitrary or long-lived game scripts.

Before optimizing this specific engine-event path, a 300k-call
run saw about 30.8 MB of private-byte growth. After optimization,
a separate 300k-call run saw about 7.0 MB. These are local and
non-controlled benchmark runs, not a universal speedup guarantee.

This test measures native/Python C ABI + IPC + V8 event-dispatch CPU
roundtrip latency; it is NOT a Vulkan or game-render frame-time profile.

## Installed Player: two 1,000-frame WARP runs

Reproduce:

    python tools/v8_provider_sidecar/frame_benchmark.py

Both runs use the installed QA Player, the same 40-object FPS scene,
30 warmup frames and 1000 observed update deltas.

| Metric | Baseline | 32 engine.scene calls/frame |
| --- | ---: | ---: |
| Observed delta p50 | 33.333 ms | 33.343 ms |
| Observed delta p95 | 33.443 ms | 50.000 ms |
| Observed delta p99 | 50.000 ms | 50.000 ms |
| Observed delta mean | 33.5753 ms | 36.9029 ms |
| Samples clamped at 50 ms | 12 | 166 |
| Wall time | 35.3757 s | 41.2405 s |
| Wall ms per frame including startup | 34.3453 | 40.0393 |

IMPORTANT: engine FPS frame() clamps simulated delta to 50 ms.
This is observed CLAMPED simulation dt, NOT raw frame time or GPU timing.
D3D11 WARP is CPU-rendered; the two tests ran sequentially with
uncontrolled ambient system load. These figures show that the tested
heavy callback workload coincided with longer observed frame intervals,
but are not causally controlled or a Vulkan performance measurement.

## Verification

- Native V8 ABI 2 smoke, plugin-service override: PASS.
- Installed Player scripts from source and FAT/DB archive: PASS.
- Native FPS Player WARP scene smoke: PASS.
- Explicit cancellation, blocked writes, memory and 1m calls: PASS.
- Exact SHA-256 updated for worker and compiled provider in the local
  sidecar-provenance.json. No QA or production dist files replaced.

Reports:
out/v8-official-windows/15.8.62/script-ipc-stress-1000000-report.json
out/v8-official-windows/15.8.62/player-frame-benchmark.json
out/v8-official-windows/15.8.62/verify-report.json

## Unfinished

The OS IPC is overlapped and cancellable, but scripting ABI 2 evaluate()
still blocks its calling thread; a genuine asynchronous task/future API
requires a separate optional contract. Need native CPU frame timings,
unclamped FPS data, Vulkan GPU timestamps on real hardware, and shader/
physics/streaming stress. The official d8 binary remains an unapproved
development shell, not a signed embeddable V8 SDK or production provider.
