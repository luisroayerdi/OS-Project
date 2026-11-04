#!/usr/bin/env python3
"""Paper Fig. 1 + Fig. 2: chunk-size progression per algorithm.   OWNER: Member A

Note: Static Steal steals from range ends, so "ordered by start" is not its
assignment order; use t_assign_s if you want true request order.

Input : results/raw/<system>/chunks/*.csv.gz   (make trace SYSTEM=...)
Output: figures/ours/fig1_2_chunks_<system>_<chunk_mode>.png
Left panel = non-adaptive algorithms (paper Fig. 1), right = adaptive (Fig. 2).
"""
from __future__ import annotations

import argparse

import pandas as pd

import style
from style import ALGO_DISPLAY, RAW, algo_style, plt

NON_ADAPTIVE = ["STATIC", "SS", "GSS", "TSS", "StaticSteal", "mFAC2", "AutoLLVM"]
ADAPTIVE = ["AWF-B", "AWF-C", "AWF-D", "AWF-E", "mAF"]


def main(smoke: bool = False) -> None:
    style.apply()
    for sysdir in sorted(p for p in RAW.iterdir() if p.is_dir()):
        if sysdir.name.endswith("-smoke") != smoke or not (sysdir / "chunks").exists():
            continue
        files = sorted((sysdir / "chunks").glob("*.csv.gz"))
        if not files:
            continue
        df = pd.concat([pd.read_csv(f, keep_default_na=False) for f in files], ignore_index=True)
        for chunk_mode, d in df.groupby("chunk_mode"):
            fig, axes = plt.subplots(1, 2, figsize=(11, 3.8), sharey=True)
            for ax, group, title in [(axes[0], NON_ADAPTIVE, "Non-adaptive (paper Fig. 1)"),
                                     (axes[1], ADAPTIVE, "Adaptive (paper Fig. 2)")]:
                for algo in group:
                    # order by start offset = order of the central work queue
                    # (logged chunk_id can interleave between threads)
                    a = d[d.method_arg == algo].sort_values("start")
                    if a.empty:
                        continue
                    x = range(1, len(a) + 1)
                    color, hatch = algo_style(algo)
                    ax.plot(x, a["size"], color=color, lw=2,
                            ls="--" if hatch else "-", label=ALGO_DISPLAY[algo])
                ax.set_yscale("log")
                ax.set_xscale("log")
                ax.set_xlabel("chunk id (ordered by start iteration)")
                ax.set_title(title)
                if ax.has_data():
                    ax.legend(ncol=2)
                else:
                    ax.text(0.5, 0.5, "no traces yet", transform=ax.transAxes, ha="center",
                            color=style.INK2)
            axes[0].set_ylabel("chunk size (iterations)")
            N, P = d.N.iloc[0], d.threads.iloc[0]
            fig.suptitle(f"{sysdir.name} | Mandelbrot L0, N={N}, P={P}, chunk={chunk_mode}",
                         x=0.01, ha="left", fontsize=11)
            style.save(fig, f"fig1_2_chunks_{sysdir.name}_{chunk_mode}.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--smoke", action="store_true")
    main(ap.parse_args().smoke)
