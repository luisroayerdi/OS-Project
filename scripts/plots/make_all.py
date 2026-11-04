#!/usr/bin/env python3
"""Regenerate every figure into figures/ours/. Missing data -> that figure is skipped.

Usage: python scripts/plots/make_all.py [--smoke]
(`make plots` runs `make aggregate` first.)
"""
from __future__ import annotations

import argparse

import fig4_cov
import fig5_heatmap
import fig6_bars
import fig7_8_selections
import fig_chunks
import rq2_rl_params

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--smoke", action="store_true", help="use *-smoke systems for chunk traces")
    a = ap.parse_args()
    print("Fig. 1/2 chunk progression");  fig_chunks.main(a.smoke)
    print("Fig. 4 c.o.v.");               fig4_cov.main()
    print("Fig. 5 heatmap");              fig5_heatmap.main()
    print("Fig. 6 bars");                 fig6_bars.main()
    print("Fig. 7 selections (STREAM, default chunk)")
    fig7_8_selections.main(None, "stream", 0, "default", 0)
    print("Fig. 8 selections (Mandelbrot L1, expChunk)")
    fig7_8_selections.main(None, "mandelbrot", 1, "exp", 0)
    print("RQ2 alpha/gamma");             rq2_rl_params.main()
