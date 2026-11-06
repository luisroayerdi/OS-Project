#!/usr/bin/env python3
"""Merge everybody's raw results into analysis-ready CSVs.

Reads   results/raw/<system>/{fixed,expert,rl,rl_sweep}/*.csv.gz
Writes  results/processed/
          loop_instances.csv.gz  every loop instance of every run (big, for Fig. 7/8)
          run_totals.csv         one row per run: total loop time (sum over loops+steps)
          summary.csv            one row per configuration: median/std over repetitions
          oracle.csv             Oracle per (system, app), per time-step and best-fixed totals
          oracle_choices.csv     best fixed (algorithm, chunk mode) per loop
          oracle_timestep_choices.csv.gz  Oracle's (algorithm, chunk mode) per time-step
          degradation.csv        summary + Oracle + % degradation vs Oracle (Fig. 5)

Oracle (paper Sect. 3.3, Table 2): the best choice "across all combinations of
application, loop, time-step, and system". From the `fixed` runs, take each
configuration's median time per time-step over the repetitions, then the fastest
configuration at each time-step of each loop; Oracle total = sum over loops and
time-steps. This is `median_s`, the baseline for Fig. 5 and Fig. 6.

The best single (algorithm, chunk mode) per loop is kept as `median_s_fixed`, with
`degradation_fixed_pct` next to `degradation_pct`. Picking a minimum per time-step
also picks up timing noise, so the gap between the two totals is partly noise.

Runs cut short by --max-seconds are extrapolated linearly to T time-steps and
flagged `truncated`. The per time-step Oracle uses the configurations that reached
each time-step, and extrapolates only if no configuration reached it.

Usage: python scripts/aggregate.py [--smoke] [--systems a,b]
"""
from __future__ import annotations

import argparse
import os
import sys

import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ALGO_DISPLAY, PROCESSED, RAW, config_label  # noqa: E402

SECTIONS = ["fixed", "expert", "rl", "rl_sweep"]
ORACLE_CONFIGS = len(ALGO_DISPLAY) * 2  # every algorithm, default and expChunk
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


def timestep_oracle(fixed: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Paper Sect. 3.3: fastest configuration at each time-step of each loop.

    `fixed` holds loop instances of the fixed runs. Returns (choices, per-loop totals).
    """
    loop = ["system", "app", "loop_id"]
    med = (fixed.groupby(loop + ["timestep", "method_arg", "chunk_mode"])
           .agg(t_par_s=("t_par_s", "median"), chunk_param=("chunk_param", "first"),
                T=("T", "first"))
           .reset_index())
    choices = med.loc[med.groupby(loop + ["timestep"])["t_par_s"].idxmin()]
    totals = (choices.groupby(loop)
              .agg(loop_s=("t_par_s", "sum"), steps=("timestep", "nunique"), T=("T", "first"))
              .reset_index())
    totals["loop_s"] = totals["loop_s"] * totals["T"] / totals["steps"]
    return choices.rename(columns={"method_arg": "algo"}), totals


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
        step_choices, step_totals = timestep_oracle(li[li.section == "fixed"])
        step_choices.to_csv(PROCESSED / "oracle_timestep_choices.csv.gz", index=False)
        for (system, app), grp in best.groupby(["system", "app"]):
            configs = med[(med.system == system) & (med.app == app)]
            n_configs = len(configs[["method_arg", "chunk_mode"]].drop_duplicates())
            if n_configs < ORACLE_CONFIGS:
                print(f"  warning: Oracle for {system}/{app} uses {n_configs} of "
                      f"{ORACLE_CONFIGS} configurations")
            step = step_totals[(step_totals.system == system) & (step_totals.app == app)]
            oracle_rows.append({"system": system, "app": app, "label": "Oracle",
                                "section": "oracle", "median_s": step["loop_s"].sum(),
                                "median_s_fixed": grp["median_s"].sum(),
                                "n_loops": len(grp), "n_configs": n_configs})
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
        indexed = oracle.set_index(["system", "app"])
        for column, ref_col, pct_col in (("median_s", "oracle_s", "degradation_pct"),
                                         ("median_s_fixed", "oracle_fixed_s",
                                          "degradation_fixed_pct")):
            ref = indexed[column]
            deg[ref_col] = [ref.get((s, ap_), float("nan")) for s, ap_ in zip(deg.system, deg.app)]
            deg[pct_col] = (deg["median_s"] - deg[ref_col]) / deg[ref_col] * 100
    deg.to_csv(PROCESSED / "degradation.csv", index=False)

    n_sys = li["system"].nunique()
    print(f"aggregated {len(runs)} runs from {n_sys} system(s) -> {PROCESSED}")
    if runs["truncated"].any():
        print(f"  note: {int(runs['truncated'].sum())} runs hit max_seconds and were extrapolated")
    if oracle.empty:
        print("  note: no fixed runs -> no Oracle yet")


if __name__ == "__main__":
    main()
