"""Shared plot style. Every figure script imports this so all figures match.

Colors: a validated 8-hue categorical palette (CVD-checked). The portfolio has
12 algorithms, so algorithms 9-12 reuse hues 1-4 WITH a hatch pattern as the
second encoding. An algorithm's color never depends on which subset is plotted.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common import ALGO_DISPLAY, ALGO_ORDER, FIGURES, PROCESSED, RAW  # noqa: E402,F401

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
SEQ_BLUE = ["#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#256abf", "#184f95", "#0d366b"]
NEUTRAL = "#8a8984"


def algo_style(algo: str) -> tuple[str, str]:
    """(color, hatch) for an algorithm CLI token, fixed for the whole project."""
    i = ALGO_ORDER.index(algo) if algo in ALGO_ORDER else len(ALGO_ORDER)
    if i >= len(ALGO_ORDER):
        return NEUTRAL, ""
    return SERIES[i % 8], ("" if i < 8 else "////")


def series_color(i: int) -> str:
    return SERIES[i % len(SERIES)]


def apply() -> None:
    plt.rcParams.update({
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "axes.edgecolor": GRID, "axes.labelcolor": INK2, "axes.titlecolor": INK,
        "axes.titlesize": 11, "axes.labelsize": 9, "xtick.color": INK2, "ytick.color": INK2,
        "xtick.labelsize": 8, "ytick.labelsize": 8, "axes.grid": True, "grid.color": GRID,
        "grid.linewidth": 0.6, "axes.axisbelow": True, "axes.spines.top": False,
        "axes.spines.right": False, "lines.linewidth": 2, "legend.frameon": False,
        "legend.fontsize": 8, "font.size": 9, "hatch.color": SURFACE, "hatch.linewidth": 1.0,
    })


def save(fig, name: str) -> Path:
    FIGURES.mkdir(parents=True, exist_ok=True)
    path = FIGURES / name
    fig.savefig(path, dpi=160, bbox_inches="tight")
    plt.close(fig)
    print("  wrote", os.path.relpath(path))
    return path


def need(path: Path) -> bool:
    if not path.exists():
        print(f"  skip: {os.path.relpath(path)} missing (run `make aggregate` / the campaign)")
        return False
    return True
