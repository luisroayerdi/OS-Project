"""Shared helpers for the campaign, aggregation and plotting scripts."""
from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import tomllib  # Python >= 3.11
except ModuleNotFoundError:  # pragma: no cover
    import tomli as tomllib  # type: ignore

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "build" / "lb_bench"
CAMPAIGN = ROOT / "experiments" / "campaign.toml"
RAW = ROOT / "results" / "raw"
PROCESSED = ROOT / "results" / "processed"
FIGURES = ROOT / "figures" / "ours"

SYSTEM_RE = re.compile(r"^[a-z0-9][a-z0-9-]{1,31}$")

# CLI token -> name used in the paper's figures
ALGO_DISPLAY = {
    "STATIC": "STATIC", "SS": "SS", "GSS": "GSS", "TSS": "TSS",
    "StaticSteal": "Static Steal", "mFAC2": "mFAC2",
    "AWF-B": "AWF-B", "AWF-C": "AWF-C", "AWF-D": "AWF-D", "AWF-E": "AWF-E",
    "mAF": "mAF", "AutoLLVM": "Auto(LLVM)",
}
ALGO_ORDER = list(ALGO_DISPLAY)

METHOD_DISPLAY = {
    "randomsel": "RandomSel", "exhaustivesel": "ExhaustiveSel", "expertsel": "ExpertSel",
    "qlearn": "Q-learn", "sarsa": "SARSA",
}
APP_DISPLAY = {"mandelbrot": "Mandelbrot", "stream": "STREAM Triad", "tc": "TC"}

# Column order of paper Fig. 5
FIG5_METHODS = [
    "Oracle",
    "RandomSel", "RandomSel,expChunk",
    "ExhaustiveSel", "ExhaustiveSel,expChunk",
    "ExpertSel", "ExpertSel,expChunk",
    "Q-learn,LT", "Q-learn,LT,expChunk", "Q-learn,LIB", "Q-learn,LIB,expChunk",
    "SARSA,LT", "SARSA,LT,expChunk", "SARSA,LIB", "SARSA,LIB,expChunk",
]


def load_campaign(profile: str | None = None) -> dict:
    """Return the campaign config with [defaults] overridden by the profile."""
    with open(CAMPAIGN, "rb") as f:
        cfg = tomllib.load(f)
    merged = dict(cfg["defaults"])
    if profile:
        profiles = cfg.get("profiles", {})
        if profile not in profiles:
            sys.exit(f"unknown profile '{profile}', choose from {sorted(profiles)}")
        merged.update(profiles[profile])
    cfg["run"] = merged
    return cfg


def check_system_name(name: str) -> str:
    if not SYSTEM_RE.match(name):
        sys.exit(f"bad system name '{name}': use lowercase letters, digits and '-', "
                 "e.g. 'alice-m2' or 'bob-i7-12700h'")
    return name


def config_label(method: str, method_arg: str, reward: str, chunk_mode: str,
                 chunk_param: int | None = None) -> str:
    """Human label in the paper's style, e.g. 'SS,34', 'Q-learn,LT,expChunk'."""
    exp = chunk_mode == "exp"
    if method == "fixed":
        name = ALGO_DISPLAY.get(method_arg, method_arg)
        if exp:
            return f"{name},{chunk_param}" if chunk_param is not None else f"{name},expChunk"
        return name
    parts = [METHOD_DISPLAY.get(method, method)]
    if reward:
        parts.append(reward)
    if exp:
        parts.append("expChunk")
    return ",".join(parts)
