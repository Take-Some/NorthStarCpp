#!/usr/bin/env python3
"""Keep public engine.* capability IDs synchronized with the host inspector."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
public=ROOT/"engine/northstarcpp/modules/api/include/northstarcpp/module.h"
inspector=ROOT/"engine/northstarcpp/modules/host/provider_inspector.cpp"
definitions=re.findall(
    r'^#define\s+(NORTHSTARCPP_CAPABILITY_[A-Z_]+)\s+"(engine\.[a-z._]+)"',
    public.read_text(encoding="utf-8"),re.M)
assert definitions, "No public engine.* capabilities found"
source=inspector.read_text(encoding="utf-8")
missing=[f"{macro} ({capability})" for macro,capability in definitions
         if not re.search(r"\b"+re.escape(macro)+r"\b",source)]
assert not missing, (
    "Host/Editor provider inspector is missing new engine.* ABI capabilities: "+
    ", ".join(missing))
assert len(definitions)==len(set(capability for _,capability in definitions)), (
    "Duplicate capability names in public ABI contract")
print(f"Provider catalog / public ABI synchronized: {len(definitions)} engine.* capabilities")
