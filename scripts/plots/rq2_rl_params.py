#!/usr/bin/env python3
"""RQ2 (extra, not a paper figure): sensitivity of Q-Learn / SARSA to alpha and
gamma.   OWNER: Member C

Input : results/processed/summary.csv rows with section == rl_sweep
        (run: python scripts/run_campaign.py --system <you> --only rl_sweep)
Output: figures/ours/rq2_alpha_gamma_<system>.png
"""
from __future__ import annotations

import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap

import style
from style import PROCESSED, plt


def main() -> None:
    style.apply()
    path = PROCESSED / "summary.csv"
    if not style.need(path):
        return
    s = pd.read_csv(path, keep_default_na=False)
    s = s[s.section == "rl_sweep"]
    if s.empty:
        print("  skip rq2: no rl_sweep runs yet")
        return
    cmap = LinearSegmentedColormap.from_list("seq", style.SEQ_BLUE)
    for system, d in s.groupby("system"):
        groups = list(d.groupby(["app", "method", "reward", "chunk_mode"]))
        fig, axes = plt.subplots(1, len(groups), figsize=(4 * len(groups), 3.6), squeeze=False)
        for ax, ((app, m, r, cm), g) in zip(axes[0], groups):
            al = sorted(pd.to_numeric(g.alpha).unique())
            ga = sorted(pd.to_numeric(g.gamma).unique())
            z = np.full((len(al), len(ga)), np.nan)
            for row in g.itertuples():
                z[al.index(float(row.alpha)), ga.index(float(row.gamma))] = float(row.median_s)
            ax.imshow(z, cmap=cmap, aspect="auto", origin="lower")
            for i in range(len(al)):
                for j in range(len(ga)):
                    if not np.isnan(z[i, j]):
                        dark = (z[i, j] - np.nanmin(z)) / max(np.nanmax(z) - np.nanmin(z), 1e-12) > .55
                        ax.text(j, i, f"{z[i, j]:.2f}s", ha="center", va="center", fontsize=8,
                                color="#ffffff" if dark else style.INK)
            ax.set_xticks(range(len(ga)), ga)
            ax.set_yticks(range(len(al)), al)
            ax.set_xlabel("gamma")
            ax.set_ylabel("alpha")
            ax.grid(False)
            ax.set_title(f"{app} | {m},{r},{cm}", loc="left")
        fig.suptitle(f"RL parameter sensitivity (median total loop time) | {system}", x=0.01,
                     ha="left")
        fig.tight_layout()
        style.save(fig, f"rq2_alpha_gamma_{system}.png")


if __name__ == "__main__":
    main()
