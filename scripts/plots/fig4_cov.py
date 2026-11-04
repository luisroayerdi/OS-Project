#!/usr/bin/env python3
"""Paper Fig. 4: coefficient of variation of loop time across ALL fixed
(algorithm, chunk) configurations, per application and system.   OWNER: Member A

c.o.v. = std / mean of the per-configuration median loop times.
High c.o.v. = the choice of scheduling algorithm matters a lot.
Input : results/processed/summary.csv     Output: figures/ours/fig4_cov.png
"""
from __future__ import annotations

import pandas as pd

import style
from style import PROCESSED, plt, series_color
from common import APP_DISPLAY

MARKERS = ["o", "s", "D", "^", "v"]


def main() -> None:
    style.apply()
    path = PROCESSED / "summary.csv"
    if not style.need(path):
        return
    s = pd.read_csv(path, keep_default_na=False)
    f = s[s.section == "fixed"]
    if f.empty:
        print("  skip fig4: no fixed runs")
        return
    cov = (f.groupby(["system", "app"])["median_s"].agg(lambda x: x.std() / x.mean())
           .reset_index(name="cov"))
    cov.to_csv(PROCESSED / "fig4_cov.csv", index=False)
    apps = [a for a in APP_DISPLAY if a in set(cov.app)]
    systems = sorted(cov.system.unique())
    fig, ax = plt.subplots(figsize=(6.5, 0.6 + 0.7 * len(apps)))
    for i, system in enumerate(systems):
        d = cov[cov.system == system].set_index("app").reindex(apps)
        ax.scatter(d["cov"], [APP_DISPLAY[a] for a in apps], s=64, color=series_color(i),
                   marker=MARKERS[i % len(MARKERS)], edgecolor=style.SURFACE, linewidth=2,
                   label=system, zorder=3)
    ax.set_xlabel("c.o.v. of loop time across scheduling algorithms")
    ax.set_xlim(left=0)
    ax.invert_yaxis()
    ax.legend(title="System", loc="lower right")
    ax.set_title("Sensitivity to the scheduling algorithm (paper Fig. 4)", loc="left")
    style.save(fig, "fig4_cov.png")


if __name__ == "__main__":
    main()
