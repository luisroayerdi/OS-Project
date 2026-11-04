#!/usr/bin/env python3
"""Paper Fig. 5: % degradation vs Oracle for every selection method (columns)
and application-system pair (rows).   OWNER: Member B

Cell text = degradation % and total loop time (s); color clipped to 0-100%
like the paper. Input: results/processed/degradation.csv
Output: figures/ours/fig5_heatmap.png
"""
from __future__ import annotations

import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap

import style
from style import PROCESSED, plt
from common import APP_DISPLAY, FIG5_METHODS


def main() -> None:
    style.apply()
    path = PROCESSED / "degradation.csv"
    if not style.need(path):
        return
    d = pd.read_csv(path, keep_default_na=False)
    d = d[d.section.isin(["oracle", "expert", "rl"])]
    if "degradation_pct" not in d or d.empty:
        print("  skip fig5: need Oracle (fixed runs) and selection-method runs")
        return
    rows = [(s, a) for a in APP_DISPLAY for s in sorted(d.system.unique())
            if not d[(d.system == s) & (d.app == a)].empty]
    cols = FIG5_METHODS
    deg = np.full((len(rows), len(cols)), np.nan)
    sec = np.full_like(deg, np.nan)
    for i, (s, a) in enumerate(rows):
        for j, label in enumerate(cols):
            m = d[(d.system == s) & (d.app == a) & (d.label == label)]
            if not m.empty:
                deg[i, j] = pd.to_numeric(m.degradation_pct).iloc[0]
                sec[i, j] = pd.to_numeric(m.median_s).iloc[0]

    cmap = LinearSegmentedColormap.from_list("seq", ["#f0efec"] + style.SEQ_BLUE)
    cmap.set_bad("#ffffff")
    fig, ax = plt.subplots(figsize=(1.0 * len(cols) + 2, 0.55 * len(rows) + 1.8))
    ax.imshow(np.clip(deg, 0, 100), cmap=cmap, vmin=0, vmax=100, aspect="auto")
    for i in range(len(rows)):
        for j in range(len(cols)):
            if np.isnan(deg[i, j]):
                ax.text(j, i, "n/a", ha="center", va="center", fontsize=6.5, color=style.INK2)
                continue
            dark = np.clip(deg[i, j], 0, 100) > 55
            ax.text(j, i, f"{deg[i, j]:.1f}%\n{sec[i, j]:.3g}s", ha="center", va="center",
                    fontsize=6.5, color="#ffffff" if dark else style.INK)
    ax.set_xticks(range(len(cols)), cols, rotation=60, ha="right")
    ax.set_yticks(range(len(rows)), [f"{APP_DISPLAY[a]} | {s}" for s, a in rows])
    ax.grid(False)
    ax.set_title("Degradation vs Oracle, % (color clipped at 100%) - paper Fig. 5", loc="left")
    style.save(fig, "fig5_heatmap.png")


if __name__ == "__main__":
    main()
