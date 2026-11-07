# Scope: which part of the paper we reproduce

The paper ran **3,600 executions**: 6 applications × 3 HPC systems × 15 selection
configurations × 5 repetitions, compiled with the Intel compiler against **LB4OMP**, a modified
LLVM OpenMP runtime. That is not reproducible on student laptops. This file states exactly what
we keep, what we change, and why. Reuse it for the "Method" and "Differences" parts of the slides
and the report.

## What we keep (faithful to the paper)

| Paper element | Ours |
|---|---|
| 12-algorithm portfolio (§3.1): STATIC, SS, GSS, TSS, Static Steal, mFAC2, AWF-B/C/D/E, mAF, Auto(LLVM) | same 12 |
| Chunk parameter semantics (§2): `max(chunk_alg, chunk_param)`; static/dynamic take it literally | same |
| Two chunk settings: default and **expChunk** | same |
| Expert selection: RandomSel (Pj = LIB/10), ExhaustiveSel, ExpertSel (§3.2) | same |
| RL: Q-Learn and SARSA, states = actions = algorithms (12 × 12 = 144 Q entries) | same |
| Rewards LT and LIB, r+ = 0.01, r0 = −2, r− = −4 (Eq. 11) | same |
| α = γ = 0.5, α-decay 0.05, explore-first (144 instances) | same |
| T = 500 time-steps, so learning takes 144/500 = 28.8 % | same |
| Metrics: T_par, LIB (Eq. 8), execution imbalance (Table 2) | same |
| Median of 5 repetitions | same (`reps = 5`) |
| Separate selector state per loop | same |

## What we cut or scale down

| Paper | Ours | Why |
|---|---|---|
| 6 apps | **3 apps**: Mandelbrot (compute-bound, evolving imbalance), STREAM Triad (memory-bound, regular), Triangle Counting (irregular graph) | These three cover the paper's three behaviour classes. HACCKernels (c.o.v. ≈ 0), LULESH and SPHYNX are large external codes. |
| 3 HPC nodes (20 / 56 / 128 cores) | **3 laptops**, one per member (typically 8–16 cores) | What we have. Same role in the analysis: "does the best choice depend on the system?" |
| LB4OMP inside the LLVM OpenMP runtime (`kmp_dispatch.cpp`), selected with `OMP_SCHEDULE=auto,8` | **Our own self-scheduling runtime** (`runtime/`): persistent thread team, central work queue, same `init / next / finish` structure | Building LB4OMP needs a patched LLVM toolchain and does not work with Apple clang. Our runtime keeps the mechanism being studied (who gets which chunk, when) while staying portable. |
| Mandelbrot N = 262,144 per loop | 65,536 (`small`), 262,144 (`medium`) | runtime |
| STREAM N = 2·10⁹ (48 GB) | 4·10⁶ (96 MB, still far larger than any laptop cache) | memory |
| TC Kronecker scale 20 (GAP `-g 20`) | scale 14 (`small`) | runtime |
| Thread pinning `OMP_PROC_BIND=close` | Linux: pinned; **macOS: not possible** (no affinity API) | OS limitation; write it up as a source of difference |
| SPHYNX in Fig. 1/2/6/8 | Mandelbrot L0/L1 | SPHYNX not included |
| Extreme runs (STREAM+SS) take hours | runs are capped at `max_seconds` and extrapolated linearly, flagged `truncated` | otherwise one configuration would take most of a night |
| Oracle across application, loop, time-step and system (§3.3, Table 2) | best fixed (algorithm, chunk mode) per loop, computed offline in `aggregate.py`. It does not change by time-step | keeps the existing baseline in the shared `aggregate.py`. On `hemal-u9-285h`, a per time-step Oracle is 0.8–2.2 % lower |

## Figure mapping

| Paper | Our file | Content |
|---|---|---|
| Fig. 1 / Fig. 2 | `figures/ours/fig1_2_chunks_<system>_<chunk>.png` | chunk size per work request, non-adaptive / adaptive |
| Fig. 4 | `fig4_cov.png` | c.o.v. of loop time across all fixed configurations, per app × system |
| Fig. 5 | `fig5_heatmap.png` | % degradation vs Oracle, every method × (app, system) |
| Fig. 6 | `fig6_<app>_<system>.png` | median time of every algorithm, Oracle, every method |
| Fig. 7 | `fig7_8_stream_L0_default_<system>.png` | per-instance selections, STREAM, default chunk |
| Fig. 8 | `fig7_8_mandelbrot_L1_exp_<system>.png` | per-instance selections, Mandelbrot L1 (evolving loop, stands in for SPHYNX), expChunk |
| (RQ2) | `rq2_alpha_gamma_<system>.png` | extra: α/γ sensitivity of the RL agents |

## Claims we test (state each as confirmed, partially confirmed, or contradicted)

1. STREAM Triad and other memory-bound loops are extremely sensitive to the algorithm: dynamic
   algorithms with chunk 1 (SS) are orders of magnitude slower. *(Fig. 4, 6)*
2. expChunk improves almost every method, and it improves the RL methods a lot. *(Fig. 5)*
3. The LIB reward is a poor choice: it steers RL toward low-imbalance, high-overhead algorithms
   such as SS. *(Fig. 5, 7)*
4. ExhaustiveSel + expChunk comes closest to Oracle. *(Fig. 5)*
5. RL finds the best algorithm after learning, but the 144-instance explore-first phase is
   expensive. *(Fig. 7)*
6. SARSA fluctuates more than Q-Learn on loops whose behaviour evolves. *(Fig. 8)*
7. The standard OpenMP schedules (STATIC, SS, GSS) are often ≥ 30 % slower than Oracle. *(Fig. 6)*

## Expected differences (start the "why our results differ" discussion here)

- **Few cores.** With P = 8, both load imbalance and scheduling contention are smaller than
  with P = 128, so differences between algorithms shrink, except on STREAM.
- **Apple silicon is heterogeneous.** It has performance and efficiency cores, which the paper's
  nodes do not. Adaptive algorithms (AWF, mAF) may do *better* here than in the paper.
- **No pinning on macOS**, laptop thermal throttling, and background processes all add noise,
  which hurts ExhaustiveSel and the RL rewards because they compare times across time-steps.
- **Smaller problems** mean shorter loops, so fixed per-instance overhead (thread wake-up)
  weighs more.
- **Our runtime is not LB4OMP.** Implementation details (mutex vs. atomics, AWF weight
  formulas) change absolute overheads.
- **Under-specified details** we had to decide ourselves: ExpertSel fuzzy rules, ExhaustiveSel
  re-trigger, α-decay form, Auto(LLVM) mapping. Each decision is logged in TEAM_PLAN.md.
