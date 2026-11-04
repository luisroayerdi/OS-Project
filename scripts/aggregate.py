#!/usr/bin/env python3
"""Merge everybody's raw results into analysis-ready CSVs.

Reads   results/raw/<system>/{fixed,expert,rl,rl_sweep}/*.csv.gz
Writes  results/processed/
          loop_instances.csv.gz  every loop instance of every run (big, for Fig. 7/8)
          run_totals.csv         one row per run: total loop time (sum over loops+steps)
          summary.csv            one row per configuration: median/std over repetitions
          oracle.csv             Oracle per (system, app) + which algorithm per loop
          degradation.csv        summary + Oracle + % degradation vs Oracle (Fig. 5)

Oracle (paper Sect. 3.3): for every loop, the single (algorithm, chunk mode)
from the `fixed` runs with the lowest median loop time; Oracle total = sum of
those per-loop minima. Runs cut short by --max-seconds are extrapolated
linearly to T time-steps and flagged `truncated`.

Usage: python scripts/aggregate.py [--smoke] [--systems a,b]
"""
from __future__ import annotations

import argparse
import os
import sys

import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import PROCESSED, RAW, config_label  # noqa: E402

SECTIONS = ["fixed", "expert", "rl", "rl_sweep"]
RUN_KEYS = ["system", "app", "section", "method", "method_arg", "reward", "alpha", "gamma",
            "chunk_mode", "rep"]
CFG_KEYS = [k for k in RUN_KEYS if k != "rep"] + ["label"]


def load_raw(systems: list[str] | None, smoke: bool) -> pd.DataFrame:
    frames = []
    for sysdir in sorted(p for p in RAW.iterdir() if p.is_dir()):
        is_smoke = sysdir.name.endswith("-smoke")
        if is_smoke != smoke or (systems and sysdir.name not in systems):
            continue
        for section in SECTIONS:
            for f in sorted((sysdir / section).glob("*.csv.gz")):
                df = pd.read_csv(f, keep_default_na=False, na_values=[])
                df["section"] = section
                frames.append(df)
    if not frames:
        sys.exit("no raw results found (did you run a campaign? use --smoke for smoke runs)")
    df = pd.concat(frames, ignore_index=True)
    df["reward"] = df["reward"].astype(str)
    df["method_arg"] = df["method_arg"].astype(str)
    return df


def per_run_totals(li: pd.DataFrame) -> pd.DataFrame:
    g = li.groupby(RUN_KEYS + ["loop_id"], dropna=False)
    per_loop = g.agg(loop_s=("t_par_s", "sum"), steps=("timestep", "nunique"), T=("T", "first"),
                     chunk_param=("chunk_param", "first"), threads=("threads", "first")).reset_index()
    per_loop["truncated"] = per_loop["steps"] < per_loop["T"]
    per_loop["loop_s"] = per_loop["loop_s"] * per_loop["T"] / per_loop["steps"]
    return per_loop


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--smoke", action="store_true", help="aggregate *-smoke systems instead")
    ap.add_argument("--systems", help="comma list of systems to include (default all)")
    a = ap.parse_args()

    li = load_raw(a.systems.split(",") if a.systems else None, a.smoke)
    PROCESSED.mkdir(parents=True, exist_ok=True)
    li.to_csv(PROCESSED / "loop_instances.csv.gz", index=False)

    per_loop = per_run_totals(li)
    runs = (per_loop.groupby(RUN_KEYS, dropna=False)
            .agg(total_s=("loop_s", "sum"), truncated=("truncated", "any"),
                 chunk_param=("chunk_param", "first"), threads=("threads", "first"))
            .reset_index())
    runs["label"] = [config_label(r.method, r.method_arg, r.reward, r.chunk_mode, r.chunk_param)
                     for r in runs.itertuples()]
    runs.to_csv(PROCESSED / "run_totals.csv", index=False)

    summary = (runs.groupby(CFG_KEYS, dropna=False)
               .agg(median_s=("total_s", "median"), mean_s=("total_s", "mean"),
                    std_s=("total_s", "std"), n_reps=("rep", "nunique"),
                    truncated=("truncated", "any"), threads=("threads", "first"))
               .reset_index())
    summary.to_csv(PROCESSED / "summary.csv", index=False)

    # ---------------- Oracle
    fixed = per_loop[per_loop.section == "fixed"]
    oracle_rows, choice_rows = [], []
    if not fixed.empty:
        med = (fixed.groupby(["system", "app", "loop_id", "method_arg", "chunk_mode"])
               .agg(median_s=("loop_s", "median"), chunk_param=("chunk_param", "first"))
               .reset_index())
        best = med.loc[med.groupby(["system", "app", "loop_id"])["median_s"].idxmin()]
        for (system, app), grp in best.groupby(["system", "app"]):
            oracle_rows.append({"system": system, "app": app, "label": "Oracle",
                                "section": "oracle", "median_s": grp["median_s"].sum(),
                                "n_loops": len(grp)})
            for r in grp.itertuples():
                choice_rows.append({"system": system, "app": app, "loop_id": r.loop_id,
                                    "algo": r.method_arg, "chunk_mode": r.chunk_mode,
                                    "chunk_param": r.chunk_param, "median_s": r.median_s})
    oracle = pd.DataFrame(oracle_rows)
    pd.DataFrame(choice_rows).to_csv(PROCESSED / "oracle_choices.csv", index=False)
    oracle.to_csv(PROCESSED / "oracle.csv", index=False)

    # ---------------- degradation vs Oracle
    deg = pd.concat([summary, oracle], ignore_index=True)
    if not oracle.empty:
        ref = oracle.set_index(["system", "app"])["median_s"]
        deg["oracle_s"] = [ref.get((s, ap_), float("nan")) for s, ap_ in zip(deg.system, deg.app)]
        deg["degradation_pct"] = (deg["median_s"] - deg["oracle_s"]) / deg["oracle_s"] * 100
    deg.to_csv(PROCESSED / "degradation.csv", index=False)

    n_sys = li["system"].nunique()
    print(f"aggregated {len(runs)} runs from {n_sys} system(s) -> {PROCESSED}")
    if runs["truncated"].any():
        print(f"  note: {int(runs['truncated'].sum())} runs hit max_seconds and were extrapolated")
    if oracle.empty:
        print("  note: no fixed runs -> no Oracle yet")


if __name__ == "__main__":
    main()
