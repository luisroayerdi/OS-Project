#!/usr/bin/env python3
"""Paper Fig. 7 / Fig. 8: which algorithm each selection method picked at every
loop instance, bar height = loop time.   OWNER: Member C

One panel per method (Oracle + 7 selection methods). Panel title shows the
most-selected algorithms and their share (the paper's pie charts).
Paper Fig. 7 = STREAM, default chunk; Fig. 8 = SPHYNX with expChunk. Defaults
below mirror that with our apps; change with --app / --chunk / --loop.

Input : results/processed/loop_instances.csv.gz, oracle_choices.csv
Output: figures/ours/fig7_8_<app>_L<loop>_<chunk>_<system>.png
"""
from __future__ import annotations

import argparse

import pandas as pd

import style
from style import ALGO_DISPLAY, ALGO_ORDER, PROCESSED, algo_style, plt
from common import APP_DISPLAY, config_label

PANELS = [("expert", "randomsel", ""), ("expert", "exhaustivesel", ""), ("expert", "expertsel", ""),
          ("rl", "qlearn", "LT"), ("rl", "qlearn", "LIB"), ("rl", "sarsa", "LT"),
          ("rl", "sarsa", "LIB")]


def shares(algos: pd.Series) -> str:
    pct = algos.value_counts(normalize=True) * 100
    top = [f"{ALGO_DISPLAY.get(a, a)} {p:.0f}%" for a, p in pct.items() if p >= 2][:3]
    return ", ".join(top)


def bars(ax, d: pd.DataFrame, title: str) -> None:
    d = d.sort_values("timestep")
    colors = [algo_style(a)[0] for a in d.algo]
    hatches = [algo_style(a)[1] for a in d.algo]
    bs = ax.bar(d.timestep, d.t_par_s, width=1.0, color=colors, linewidth=0)
    for b, h in zip(bs, hatches):
        if h:
            b.set_hatch(h)
    ax.set_yscale("log")
    ax.set_title(f"{title}\n{shares(d.algo)}", loc="left", fontsize=8.5)


def main(system: str | None, app: str, loop: int, chunk: str, rep: int) -> None:
    style.apply()
    li_path, oc_path = PROCESSED / "loop_instances.csv.gz", PROCESSED / "oracle_choices.csv"
    if not style.need(li_path):
        return
    li = pd.read_csv(li_path, keep_default_na=False)
    li = li[(li.app == app) & (li.loop_id == loop)]
    oc = pd.read_csv(oc_path, keep_default_na=False) if oc_path.exists() else pd.DataFrame()
    for s in sorted(li.system.unique()):
        if system and s != system:
            continue
        d = li[li.system == s]
        fig, axes = plt.subplots(2, 4, figsize=(15, 6), sharey=True)
        axes = axes.ravel()
        # Oracle panel: per-timestep median of the Oracle's chosen fixed config
        o = oc[(oc.system == s) & (oc.app == app) & (oc.loop_id == loop)] if not oc.empty else oc
        if not o.empty:
            algo, cm = o.algo.iloc[0], o.chunk_mode.iloc[0]
            f = d[(d.section == "fixed") & (d.method_arg == algo) & (d.chunk_mode == cm)]
            f = f.groupby("timestep").agg(t_par_s=("t_par_s", "median")).reset_index()
            f["algo"] = algo
            bars(axes[0], f, "Oracle")
        else:
            axes[0].set_title("Oracle (needs fixed runs)", loc="left", fontsize=8.5)
        for ax, (sec, m, r) in zip(axes[1:], PANELS):
            p = d[(d.section == sec) & (d.method == m) & (d.reward == r) & (d.chunk_mode == chunk)
                  & (d.rep == rep)]
            label = config_label(m, "", r, chunk)
            if p.empty:
                ax.set_title(f"{label}\n(no data)", loc="left", fontsize=8.5)
                continue
            bars(ax, p, label)
        for ax in axes[4:]:
            ax.set_xlabel("loop instance (time-step)")
        for ax in axes[::4]:
            ax.set_ylabel("loop time (s)")
        used = [a for a in ALGO_ORDER if a in set(d.algo)]
        handles = [plt.Rectangle((0, 0), 1, 1, facecolor=algo_style(a)[0], hatch=algo_style(a)[1])
                   for a in used]
        fig.legend(handles, [ALGO_DISPLAY[a] for a in used], ncol=min(12, len(used)),
                   loc="lower center", bbox_to_anchor=(0.5, -0.04))
        fig.suptitle(f"{APP_DISPLAY.get(app, app)} | L{loop} | chunk={chunk} | {s} "
                     "(paper Fig. 7/8)", x=0.01, ha="left")
        fig.tight_layout()
        style.save(fig, f"fig7_8_{app}_L{loop}_{chunk}_{s}.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--system")
    ap.add_argument("--app", default="stream")
    ap.add_argument("--loop", type=int, default=0)
    ap.add_argument("--chunk", default="default")
    ap.add_argument("--rep", type=int, default=0)
    x = ap.parse_args()
    main(x.system, x.app, x.loop, x.chunk, x.rep)
