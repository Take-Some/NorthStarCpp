#!/usr/bin/env python3
"""V8 developer sidecar stress: cancellation, warm pool, parallel contexts.

Measures JS dispatch roundtrip (host Python -> C ABI -> IPC -> V8 -> IPC),
not Vulkan CPU/GPU frame-time. Does not approve a production release.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import ctypes
import json
import math
from pathlib import Path
import os
import statistics
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
V8 = ROOT / "out/v8-official-windows/15.8.62"
DLL = V8 / "sidecar-build/scripting_v8_sidecar.dll"


class Config(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32),
                ("heap_limit_mb", ctypes.c_uint32),
                ("timeout_ms", ctypes.c_uint32),
                ("max_source_bytes", ctypes.c_uint32)]


class Result(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32),
                ("json_utf8", ctypes.c_void_p),
                ("json_bytes", ctypes.c_uint32)]


class Module(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("host_abi", ctypes.c_uint32),
                ("capability", ctypes.c_char_p), ("provider", ctypes.c_char_p),
                ("version", ctypes.c_char_p), ("api_version", ctypes.c_uint32),
                ("api_size", ctypes.c_uint32), ("api", ctypes.c_void_p),
                ("last_error", ctypes.c_void_p)]


class Api(ctypes.Structure):
    _fields_ = [(name, ctypes.c_void_p) for name in
                ("create", "destroy", "evaluate", "reset", "set_host_call")]


class Runtime:
    def __init__(self) -> None:
        self.lib = ctypes.CDLL(str(DLL.resolve()))
        self.lib.northstar_module_query.argtypes = [
            ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
        self.lib.northstar_module_query.restype = ctypes.POINTER(Module)
        mod = self.lib.northstar_module_query(1, b"engine.scripting", 2)
        if not mod or mod.contents.api_size < ctypes.sizeof(Api):
            raise RuntimeError("Scripting plugin ABI mismatch")
        api = ctypes.cast(mod.contents.api, ctypes.POINTER(Api)).contents
        self._create = ctypes.CFUNCTYPE(
            ctypes.c_void_p, ctypes.POINTER(Config))(api.create)
        self._destroy = ctypes.CFUNCTYPE(None, ctypes.c_void_p)(api.destroy)
        self._evaluate = ctypes.CFUNCTYPE(ctypes.c_int,
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32,
            ctypes.c_char_p, ctypes.POINTER(Result))(api.evaluate)
        self._error = ctypes.CFUNCTYPE(ctypes.c_char_p)(mod.contents.last_error)
        self._reset = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)(api.reset)
        self.lib.northstar_v8_sidecar_cancel.argtypes = [ctypes.c_void_p]
        self.lib.northstar_v8_sidecar_cancel.restype = ctypes.c_int
        self.lib.northstar_v8_sidecar_worker_pid.argtypes = [ctypes.c_void_p]
        self.lib.northstar_v8_sidecar_worker_pid.restype = ctypes.c_ulong
        self.config = Config(ctypes.sizeof(Config), 128, 7000, 1_048_576)

    def create(self, *, timeout: int = 7000) -> int:
        c = Config(ctypes.sizeof(Config), 128, timeout, 1_048_576)
        ctx = self._create(ctypes.byref(c))
        if not ctx:
            raise RuntimeError("V8 create: " + self.error())
        return ctx

    def error(self) -> str:
        raw = self._error()
        return raw.decode("utf-8", errors="replace") if raw else ""

    def evaluate(self, ctx: int, source: str,
                 origin: str = "v8-stress.js") -> str:
        payload = source.encode("utf-8")
        answer = Result(ctypes.sizeof(Result), None, 0)
        rc = self._evaluate(ctx, payload, len(payload),
                            origin.encode("utf-8"), ctypes.byref(answer))
        if rc != 1:
            raise RuntimeError(self.error())
        return ctypes.string_at(answer.json_utf8, answer.json_bytes).decode("utf-8")

    def destroy(self, ctx: int) -> None:
        self._destroy(ctx)

    def pid(self, ctx: int) -> int:
        return int(self.lib.northstar_v8_sidecar_worker_pid(ctx))

    def cancel(self, ctx: int) -> bool:
        return bool(self.lib.northstar_v8_sidecar_cancel(ctx))


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, math.ceil(fraction * len(ordered))-1)]


def process_footprint(pid: int) -> dict[str, int] | None:
    """Collect native Windows process private bytes and working set."""
    if os.name != "nt":
        return None
    class Counters(ctypes.Structure):
        _fields_ = [
            ("cb",ctypes.c_uint32),("page_fault_count",ctypes.c_uint32),
            ("peak_working_set",ctypes.c_size_t),("working_set",ctypes.c_size_t),
            ("quota_peak_paged_pool",ctypes.c_size_t),("quota_paged_pool",ctypes.c_size_t),
            ("quota_peak_nonpaged_pool",ctypes.c_size_t),("quota_nonpaged_pool",ctypes.c_size_t),
            ("pagefile_usage",ctypes.c_size_t),("peak_pagefile_usage",ctypes.c_size_t),
            ("private_usage",ctypes.c_size_t)
        ]
    k32=ctypes.WinDLL("kernel32",use_last_error=True)
    k32.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
    k32.OpenProcess.restype=ctypes.c_void_p
    k32.CloseHandle.argtypes=[ctypes.c_void_p]
    k32.CloseHandle.restype=ctypes.c_int
    handle=k32.OpenProcess(0x0400 | 0x0010,False,pid)
    if not handle:
        return None
    try:
        psapi=ctypes.WinDLL("psapi")
        psapi.GetProcessMemoryInfo.argtypes=[
            ctypes.c_void_p,ctypes.POINTER(Counters),ctypes.c_uint32]
        psapi.GetProcessMemoryInfo.restype=ctypes.c_int
        stats=Counters()
        stats.cb=ctypes.sizeof(stats)
        if not psapi.GetProcessMemoryInfo(handle,ctypes.byref(stats),stats.cb):
            return None
        return {"working_set_bytes":stats.working_set,
                "private_bytes":stats.private_usage}
    finally:
        k32.CloseHandle(handle)


def host_handles() -> int | None:
    if os.name != "nt":
        return None
    k32=ctypes.WinDLL("kernel32")
    k32.GetCurrentProcess.restype=ctypes.c_void_p
    k32.GetProcessHandleCount.argtypes=[
        ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)]
    total=ctypes.c_uint32()
    if not k32.GetProcessHandleCount(k32.GetCurrentProcess(),
                                     ctypes.byref(total)):
        return None
    return total.value


def run(frames: int = 2000, contexts: int = 4) -> dict:
    if not (100 <= frames <= 1000000 and 2 <= contexts <= 8):
        raise ValueError("Expected 100..1000000 frames and 2..8 contexts")
    os.environ["NORTHSTARCPP_V8_D8_EXECUTABLE"] = str((V8/"bin/d8.exe").resolve())
    os.environ["NORTHSTARCPP_V8_SIDECAR_WORKER"] = str(
        (ROOT/"tools/v8_provider_sidecar/v8_worker.js").resolve())
    api = Runtime()
    t0 = time.perf_counter()
    # Lazy initialization and zero data leakage after warm-process recycling.
    ctx = api.create()
    try:
        assert api.pid(ctx) == 0, "create() started a subprocess eagerly"
        assert api.evaluate(ctx, "globalThis.secret=492;secret") == "492"
        warm_pid = api.pid(ctx)
        assert warm_pid != 0
    finally:
        api.destroy(ctx)
    ctx = api.create()
    try:
        assert api.pid(ctx) == 0
        assert api.evaluate(ctx, "typeof secret") == '"undefined"'
        assert api.pid(ctx) == warm_pid, "Warm pool did not reuse worker PID"
        # Multi-megabyte hex protocol, larger than the 64 KiB pipe buffer.
        assert api.evaluate(ctx,
            "globalThis.large='"+("x"*540000)+"';large.length") == "540000"
        assert api.evaluate(ctx, "large.length") == "540000"
    finally:
        api.destroy(ctx)

    # Cancel infinite JS without waiting for the timeout deadline.
    ctx = api.create(timeout=6000)
    try:
        assert api.evaluate(ctx, "1+1") == "2"
        finished = threading.Event()
        failure: dict[str, str] = {}
        def run_loop() -> None:
            try:
                api.evaluate(ctx, "while(true){}")
                failure["unexpected"] = "Infinite loop returned normally"
            except RuntimeError as exc:
                failure["message"] = str(exc)
            finally:
                finished.set()
        started = time.perf_counter()
        t = threading.Thread(target=run_loop, name="v8-cancel-test")
        t.start()
        time.sleep(0.120)
        assert api.cancel(ctx), "Cannot signal V8 cancellation"
        stopped = finished.wait(timeout=2.5)
        if not stopped:
            raise RuntimeError("V8 cancel failed to release blocked IPC")
        t.join()
        cancel_ms = (time.perf_counter()-started)*1000
        assert "cancelled" in failure.get("message", ""), failure
        assert cancel_ms < 1600, f"Cancellation too slow: {cancel_ms:.1f}ms"
        assert api.evaluate(ctx, "2+3") == "5", "Cancelled process did not recover"
    finally:
        api.destroy(ctx)

    # An intentionally stalled child does not read stdin: verify that a
    # pending overlapped WRITE can also be cancelled, not just a READ.
    real_worker=os.environ["NORTHSTARCPP_V8_SIDECAR_WORKER"]
    os.environ["NORTHSTARCPP_V8_SIDECAR_WORKER"]=str(
        (ROOT/"tools/v8_provider_sidecar/fixtures/hung_worker.js").resolve())
    try:
        ctx=api.create(timeout=6000)
        try:
            failure={}
            finished=threading.Event()
            oversized_source="globalThis.data='"+("q"*800000)+"';data.length"
            def blocked_writer():
                try:
                    api.evaluate(ctx,oversized_source)
                    failure["unexpected"]="Blocked write returned normally"
                except RuntimeError as exc:
                    failure["message"]=str(exc)
                finally:
                    finished.set()
            stamp=time.perf_counter()
            thread=threading.Thread(target=blocked_writer,name="v8-stalled-pipe-writer")
            thread.start()
            time.sleep(.160)
            assert api.cancel(ctx),"Cannot signal blocked writer cancellation"
            assert finished.wait(2.5),"Overlapped WRITE cancellation deadlocked"
            thread.join()
            writer_cancel_ms=(time.perf_counter()-stamp)*1000
            assert "cancelled" in failure.get("message",""), failure
        finally:
            api.destroy(ctx)
    finally:
        os.environ["NORTHSTARCPP_V8_SIDECAR_WORKER"]=real_worker

    # Separate timeout policy. The same ABI also recovers from deadline kills.
    ctx = api.create(timeout=250)
    try:
        assert api.evaluate(ctx, "1+1") == "2"
        try:
            api.evaluate(ctx, "while(true){}")
        except RuntimeError as exc:
            assert "timed out" in str(exc), str(exc)
        else:
            raise RuntimeError("V8 infinite script ignored timeout")
        assert api.evaluate(ctx, "4+1") == "5"
    finally:
        api.destroy(ctx)

    # Truly concurrent active script contexts, each with independent d8 pipe.
    gate = threading.Barrier(contexts)
    active_gate = threading.Barrier(contexts)
    exit_gate = threading.Barrier(contexts)
    def context_task(index: int) -> dict:
        instance = api.create(timeout=10000)
        try:
            assert api.pid(instance) == 0
            gate.wait(timeout=10)
            launch = time.perf_counter()
            assert api.evaluate(instance,
                f"globalThis.ctxNumber={index};ctxNumber") == str(index)
            first_ms = (time.perf_counter()-launch)*1000
            pid = api.pid(instance)
            # Do not release an earlier worker back to the idle pool until
            # all contexts have simultaneously entered the ACTIVE state.
            active_gate.wait(timeout=15)
            for _ in range(100):
                assert api.evaluate(instance, "ctxNumber") == str(index)
            exit_gate.wait(timeout=15)
            return {"index": index, "pid": pid, "first_eval_ms": first_ms}
        finally:
            api.destroy(instance)
    with concurrent.futures.ThreadPoolExecutor(max_workers=contexts) as executor:
        independent = list(executor.map(context_task, range(contexts)))
    assert len(set(item["pid"] for item in independent)) == contexts, (
        "Concurrent contexts are not independently hosted")

    # Frame-equivalent callback path: engine.on/update in persistent Realm.
    # This is scripting/IPC CPU dispatch cost, NOT real Vulkan frame time.
    ctx = api.create(timeout=5000)
    latency_ms: list[float] = []
    try:
        setup = ("let frameCounter=0;"
                 "engine.on('update',dt=>{frameCounter++;return dt});true")
        assert api.evaluate(ctx, setup) == "true"
        pid=api.pid(ctx)
        before_memory=process_footprint(pid)
        before_handles=host_handles()
        checkpoints=[]
        for i in range(frames):
            start = time.perf_counter()
            result = api.evaluate(ctx,
                'engine.emit("update",JSON.parse("0.0166667"))',
                origin="<engine-event>")
            latency_ms.append((time.perf_counter()-start)*1000)
            if result != "1":
                raise RuntimeError(f"V8 update callback {i} failed: {result}")
            if (i+1) % max(1,frames//4)==0:
                checkpoints.append({
                    "frames":i+1,
                    "worker_memory":process_footprint(pid),
                    "host_handles":host_handles()})
        assert api.evaluate(ctx, "frameCounter") == str(frames)
        after_memory=process_footprint(pid)
        after_handles=host_handles()
    finally:
        api.destroy(ctx)
    report = {
        "classification": "DEVELOPER_V8_SCRIPT_IPC_CPU_DISPATCH_NOT_GPU_FRAME_TIME",
        "v8_version": "15.8.62",
        "event_dispatch_fast_path": True,
        "frames": frames,
        "parallel_active_contexts": contexts,
        "warm_pool_reused_worker_pid": warm_pid,
        "explicit_cancel_ms": round(cancel_ms,3),
        "blocked_write_cancel_ms":round(writer_cancel_ms,3),
        "active_contexts": independent,
        "ipc_p50_ms": round(percentile(latency_ms,0.50),4),
        "ipc_p95_ms": round(percentile(latency_ms,0.95),4),
        "ipc_p99_ms": round(percentile(latency_ms,0.99),4),
        "ipc_max_ms": round(max(latency_ms),4),
        "ipc_mean_ms": round(statistics.mean(latency_ms),4),
        "wall_seconds": round(time.perf_counter()-t0,3),
        "worker_memory_before":before_memory,
        "worker_memory_after":after_memory,
        "memory_checkpoints":checkpoints,
        "worker_private_growth_bytes":(
            after_memory["private_bytes"]-before_memory["private_bytes"]
            if before_memory and after_memory else None),
        "host_handles_before":before_handles,
        "host_handles_after":after_handles,
        "host_handle_delta":(
            after_handles-before_handles
            if before_handles is not None and after_handles is not None else None),
        "security_approved": False,
        "release_approved": False,
    }
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames", type=int, default=2000)
    parser.add_argument("--contexts", type=int, default=4)
    args = parser.parse_args()
    try:
        info = run(args.frames, args.contexts)
        report_path = V8 / f"script-ipc-stress-{args.frames}-report.json"
        content = json.dumps(info,indent=2)+"\n"
        report_path.write_text(content,encoding="utf-8")
        # Keep canonical report for substantial soak runs only.
        if args.frames >= 300000:
            (V8 / "script-ipc-stress-report.json").write_text(
                content,encoding="utf-8")
        print(json.dumps(info,indent=2))
        print("PASS: cancellation, deadline, pooling, parallel contexts, frame dispatch")
    except Exception as exc:
        print("V8 STRESS FAILED:",repr(exc),file=sys.stderr)
        sys.exit(1)
