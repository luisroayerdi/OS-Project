#!/usr/bin/env python3
"""Paper Fig. 6: median loop time of every individual algorithm (default and
expChunk), Oracle, and every selection method, for each (system, app).
OWNER: Member B

Error bars = std over repetitions; dotted line = Oracle.
Input : results/processed/degradation.csv
Output: figures/ours/fig6_<app>_<system>.png
"""
from __future__ import annotations

import argparse

import pandas as pd

import style
from style import ALGO_ORDER, PROCESSED, plt, series_color
from common import APP_DISPLAY, FIG5_METHODS

GROUP_COLOR = {"fixed": style.NEUTRAL, "oracle": style.INK, "expert": series_color(0),
               "rl": series_color(1)}


def plot_one(d: pd.DataFrame, system: str, app: str) -> None:
    fixed = d[d.section == "fixed"].copy()
    fixed["order"] = fixed.method_arg.map(ALGO_ORDER.index) + (fixed.chunk_mode == "exp") * 100
    parts = [fixed.sort_values("order"), d[d.section == "oracle"]]
    sel = d[d.section.isin(["expert", "rl"])].copy()
    sel["order"] = sel.label.map(lambda x: FIG5_METHODS.index(x) if x in FIG5_METHODS else 99)
    parts.append(sel.sort_values("order"))
    b = pd.concat(parts, ignore_index=True)
    if b.empty:
        return
    y = pd.to_numeric(b.median_s)
    err = pd.to_numeric(b.get("std_s", pd.Series(dtype=float)), errors="coerce").fillna(0)
    fig, ax = plt.subplots(figsize=(max(8, 0.3 * len(b)), 4))
    ax.bar(range(len(b)), y, yerr=err, width=0.75, color=[GROUP_COLOR[s] for s in b.section],
           edgecolor=style.SURFACE, linewidth=1, error_kw={"lw": 1, "ecolor": style.INK2})
    oracle = d[d.section == "oracle"]
    if not oracle.empty:
        ax.axhline(float(oracle.median_s.iloc[0]), color=style.INK, ls=":", lw=1.2)
    for i, t in enumerate(b.truncated if "truncated" in b else []):
        if str(t) == "True":
            ax.text(i, y.iloc[i], "cap", ha="center", va="bottom", fontsize=6, color=style.INK2)
    ax.set_xticks(range(len(b)), b.label, rotation=75, ha="right", fontsize=7)
    if y.max() / max(y[y > 0].min(), 1e-12) > 30:
        ax.set_yscale("log")
    ax.set_ylabel("parallel loop time (s), median")
    handles = [plt.Rectangle((0, 0), 1, 1, color=c) for c in GROUP_COLOR.values()]
    ax.legend(handles, ["individual algorithm", "Oracle", "expert-based", "RL-based"], ncol=4,
              loc="upper left")
    ax.set_title(f"{APP_DISPLAY.get(app, app)} | {system} (paper Fig. 6)", loc="left")
    style.save(fig, f"fig6_{app}_{system}.png")


def main(system: str | None = None, app: str | None = None) -> None:
    style.apply()
    path = PROCESSED / "degradation.csv"
    if not style.need(path):
        return
    d = pd.read_csv(path, keep_default_na=False)
    d = d[d.section.isin(["fixed", "oracle", "expert", "rl"])]
    for (s, a), grp in d.groupby(["system", "app"]):
        if (system and s != system) or (app and a != app):
            continue
        plot_one(grp, s, a)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--system")
    ap.add_argument("--app")
    x = ap.parse_args()
    main(x.system, x.app)
