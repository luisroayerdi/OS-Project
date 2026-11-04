#!/usr/bin/env python3
"""Check that a member's results are complete and well-formed before pushing.

Usage: python scripts/check_results.py --system alice-m2 [--profile full]

Prints, per section, how many runs are done / missing / not implemented yet,
and validates the CSV schema of every file. Exit code 1 if a file is corrupt.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import RAW, check_system_name, load_campaign  # noqa: E402
from run_campaign import ALL_SECTIONS, build_jobs  # noqa: E402

RUN_COLS = ["system", "app", "loop_id", "timestep", "T", "method", "method_arg", "reward", "alpha",
            "gamma", "chunk_mode", "chunk_param", "rep", "threads", "N", "algo", "t_par_s",
            "lib_pct", "exec_imb_pct", "n_chunks"]
TRACE_COLS = ["system", "app", "loop_id", "timestep", "method_arg", "chunk_mode", "chunk_param",
              "threads", "N", "chunk_id", "tid", "start", "size", "t_assign_s"]


def last_status(system: str) -> dict[str, str]:
    path = RAW / system / "status.csv"
    if not path.exists():
        return {}
    with open(path) as f:
        return {row["job"]: row["status"] for row in csv.DictReader(f)}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--system", required=True)
    ap.add_argument("--profile", default="full")
    ap.add_argument("--portfolio", help="same value you passed to run_campaign.py, if any")
    a = ap.parse_args()
    system = check_system_name(a.system)
    cfg0 = load_campaign(a.profile)
    if a.portfolio:
        keep = a.portfolio.split(",")
        cfg0["fixed"]["algorithms"] = [x for x in cfg0["fixed"]["algorithms"] if x in keep]
        system += f"-p{len(keep)}"
    if a.profile == "smoke" and not system.endswith("-smoke"):
        system += "-smoke"
    cfg = cfg0
    status = last_status(system)

    if not (RAW / system / "system_info.json").exists():
        print(f"missing results/raw/{system}/system_info.json (run the campaign once)")

    bad_files = 0
    for section in ALL_SECTIONS:
        jobs = build_jobs(cfg, system, [section], None, None)
        c: Counter = Counter()
        for j in jobs:
            if j.out.exists():
                try:
                    with gzip.open(j.out, "rt") as f:
                        header = next(csv.reader(f))
                    want = TRACE_COLS if section == "chunks" else RUN_COLS
                    if header != want:
                        raise ValueError(f"unexpected header {header[:4]}...")
                    c["done"] += 1
                except Exception as e:  # noqa: BLE001
                    print(f"  CORRUPT {j.name}: {e}")
                    bad_files += 1
            else:
                c[status.get(j.name, "missing")] += 1
        line = ", ".join(f"{k}={v}" for k, v in sorted(c.items()))
        print(f"{section:9s} {len(jobs):4d} jobs: {line}")
    print("\n'not_implemented' jobs run automatically once the code exists; "
          "'missing' = never run (start/continue the campaign).")
    sys.exit(1 if bad_files else 0)


if __name__ == "__main__":
    main()
