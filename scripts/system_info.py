#!/usr/bin/env python3
"""Record the hardware/software of this machine (the paper's "Computing nodes" row).

Usage: python scripts/system_info.py --system alice-m2
Writes results/raw/<system>/system_info.json. run_campaign.py calls this automatically.
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import RAW, ROOT, check_system_name  # noqa: E402


def sh(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10).stdout.strip()
    except Exception:
        return ""


def physical_cores() -> int:
    if sys.platform == "darwin":
        v = sh(["sysctl", "-n", "hw.physicalcpu"])
        if v.isdigit():
            return int(v)
    if sys.platform.startswith("linux"):
        out = sh(["lscpu", "-p=core,socket"])
        cores = {line for line in out.splitlines() if line and not line.startswith("#")}
        if cores:
            return len(cores)
    return os.cpu_count() or 1


def collect() -> dict:
    info: dict = {
        "collected_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "os": platform.platform(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "logical_cpus": os.cpu_count(),
        "physical_cores": physical_cores(),
        "compiler": sh(["c++", "--version"]).splitlines()[0] if sh(["c++", "--version"]) else "",
        "git_commit": sh(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"]),
    }
    if sys.platform == "darwin":
        info["cpu"] = sh(["sysctl", "-n", "machdep.cpu.brand_string"])
        info["memory_gb"] = round(int(sh(["sysctl", "-n", "hw.memsize"]) or 0) / 2**30, 1)
        # Apple silicon: performance vs efficiency cores (heterogeneous!)
        p = sh(["sysctl", "-n", "hw.perflevel0.physicalcpu"])
        e = sh(["sysctl", "-n", "hw.perflevel1.physicalcpu"])
        if p:
            info["performance_cores"] = int(p)
        if e:
            info["efficiency_cores"] = int(e)
        info["thread_pinning"] = "not available on macOS"
    else:
        model = [l for l in sh(["lscpu"]).splitlines() if l.startswith("Model name")]
        info["cpu"] = model[0].split(":", 1)[1].strip() if model else platform.processor()
        mem = [l for l in open("/proc/meminfo").read().splitlines() if l.startswith("MemTotal")] \
            if os.path.exists("/proc/meminfo") else []
        if mem:
            info["memory_gb"] = round(int(mem[0].split()[1]) / 2**20, 1)
        info["thread_pinning"] = "cores (pthread_setaffinity_np)"
    return info


def write(system: str) -> dict:
    out = RAW / system / "system_info.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    info = {"system": system, **collect()}
    out.write_text(json.dumps(info, indent=2) + "\n")
    return info


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--system", required=True)
    a = ap.parse_args()
    print(json.dumps(write(check_system_name(a.system)), indent=2))
