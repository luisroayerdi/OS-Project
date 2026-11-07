# Team plan

Fill in names and machine labels first:

| Role | Name | `SYSTEM` label | Machine (CPU, cores, RAM, OS) |
|---|---|---|---|
| Member A, Portfolio | _____ | _____ | _____ |
| Member B, Expert selection | _____ | _____ | _____ |
| Member C, RL selection | Luis Ayerdi-Morales | luis-m2 | Apple M2, 8 cores, 16 GB, macOS 15 |

Ground rules:

- **Own your files.** Edit only the files listed under your role, plus your own
  `results/raw/<your-system>/`. Changes to shared files (`runtime/include/`, `main.cpp`,
  `campaign.toml`, `aggregate.py`, `common.py`) go through a pull request that the other two
  have seen.
- `make test` must show **0 FAIL** before you push code. TODO is fine.
- Work on a branch (`a/tss`, `b/exhaustive`, `c/qlearn`, …) and merge to `main` via PR.
- Every decision the paper leaves open goes in the **decision log** at the bottom, with a one-line reason.

---

## Everybody

- [ ] `make setup && make build && make test && make smoke SYSTEM=<you>` works on your machine
- [ ] Add your machine to the table above
- [ ] Read paper §1–3 and skim §4–5. Read [ARCHITECTURE.md](ARCHITECTURE.md)
- [ ] Preliminary run for the slides: `make campaign SYSTEM=<you> PROFILE=quick ARGS="--portfolio STATIC,SS,GSS --only fixed"`, then push
- [ ] Final full campaign on your machine (all sections + `make trace`), then push
- [ ] Your slide section and report section (see [PRESENTATION_OUTLINE.md](PRESENTATION_OUTLINE.md))
- [ ] Describe your contribution in the report

## Member A: scheduling-algorithm portfolio

File: `runtime/src/algos_portfolio.cpp`. Examples: `runtime/src/algos_reference.cpp`.

- [ ] **TSS** (Eq. 4)
- [ ] **mFAC2** (FAC2 with an atomic batch counter, p. 4–5)
- [ ] **Static Steal** (LLVM `kmp_sch_static_steal`)
- [ ] **Auto(LLVM)**: find what LLVM maps `schedule(auto)` to and log it below
- [ ] **AWF-B, AWF-C, AWF-D, AWF-E** (p. 5; weights persist across instances)
- [ ] **mAF** (Eqs. 6–7)
- [ ] `make test`: coverage + TC invariance PASS for all 12
- [ ] `make trace SYSTEM=<you>`, then Fig. 1/2. Compare the shapes with the paper (GSS/TSS/mFAC2 decreasing, adaptive ones fluctuating)
- [ ] Fig. 4 (c.o.v.) and its discussion: which apps are sensitive, and is that the same as in the paper?
- [ ] Slides/report: Background (§2) + portfolio

Acceptance: all 12 algorithms PASS in `make test`. Chunk-size traces have the qualitative shape
of paper Figs. 1/2. Each algorithm has a comment naming its source (paper equation or LB4OMP function).

## Member B: expert-based selection

File: `runtime/src/selectors_expert.cpp`. Main source: the Auto4OMP paper (ref. [25]) and its code.

- [ ] **expChunk** (ref. [25] Eq. 1). `make test` prints your value next to the paper's (217, 305, 34, 30). Explain any mismatch
- [ ] **RandomSel**
- [ ] **ExhaustiveSel** with LIB re-trigger. Log the exact re-trigger rule below
- [ ] **ExpertSel** (fuzzy logic). Log any simplification below
- [ ] Check that the Oracle in `scripts/aggregate.py` matches paper §3.3 (you own its correctness)
- [ ] Fig. 5 (heatmap) and Fig. 6 (bars). Test claims 1, 2, 4, 7 of [PAPER_SUBSET.md](PAPER_SUBSET.md)
- [ ] Slides/report: Problem statement + expert methods + Fig. 5/6 discussion

Acceptance: `make test` B-checks PASS. ExhaustiveSel settles on the synthetic best. Fig. 5 is
complete for all 3 systems.

## Member C: RL-based selection

File: `runtime/src/selectors_rl.cpp`.

- [ ] Reward function Eq. 11 (LT and LIB)
- [ ] Explore-first plan covering all K×K (state, action) pairs (Eulerian circuit; works for any portfolio size K)
- [ ] **Q-Learn** (Eq. 10), **SARSA** (Eq. 9) with α-decay. Log the decay form below
- [ ] `make test`: C-checks PASS. Read the informational lines: does LIB lure the agent toward the low-LIB, slow algorithm as the paper says?
- [ ] Fig. 7/8 (selections per instance). Test claims 3, 5, 6
- [ ] RQ2: `python scripts/run_campaign.py --system <you> --only rl_sweep`, then the α/γ figure
- [ ] Slides/report: Novelty (RL in the OpenMP runtime) + RL method + Fig. 7/8 + RQ2

Acceptance: explore-first coverage PASS for K = 12 and for `--portfolio STATIC,SS,GSS` (K = 3).
Fig. 7/8 shows the 144-step learning phase followed by a stable selection.

---

## Timeline

| When | What | Who |
|---|---|---|
| Tue 11/4 | Setup working on all 3 machines; names and machines filled in above | all |
| Wed 11/5 | `quick` preliminary run (STATIC/SS/GSS), push, `make plots` | all |
| **Thu 11/6** | **Slides submitted.** Method + paper figures + our preliminary Fig. 4/6 | all |
| 11/7 – 11/16 | Implementation (A: algorithms, B: expert selectors, C: RL) | A, B, C |
| 11/17 – 11/20 | Code complete, `make test` all PASS, code freeze on `main` | all |
| 11/20 – 11/23 | Full campaigns on all 3 laptops (overnight), push results | all |
| 11/24 – 11/27 | `make plots`, comparison with paper, "why different" analysis | all |
| 11/28 – 11/30 | Report, video, packaging | all |
| **Mon 12/1** | **Final package submitted** | all |

**Code freeze matters.** After the full campaign has started, changing an algorithm invalidates
everybody's results for it. If you must change something, delete the affected result files on
all machines and re-run them.

---

## Decision log

Record every choice the paper does not pin down. These feed the "differences" section of the report.

| Date | Who | Decision | Reason / source |
|---|---|---|---|
| 2026-11-03 | setup | Oracle = per loop, best fixed (algorithm, chunk mode) by median total time; Oracle total = sum over loops | paper §3.3 + Table 2 ("ideal combination … across application loops") |
| 2026-11-03 | setup | Mandelbrot L1 zooms into (−0.7436, 0.1318) over T steps, L2 is the reverse | paper gives no coordinates |
| 2026-11-03 | setup | STREAM/SS-type runs capped at `max_seconds` and extrapolated | laptop time budget |
| | A | Auto(LLVM) maps to … | |
| | B | expChunk formula: … | |
| | B | ExhaustiveSel re-trigger: … | |
| | B | ExpertSel rules: … | |
| 2026-11-06 | C | α-decay: multiplicative, α ← max(0.01, α·(1−0.05)) once per greedy instance (after explore-first) | subtractive 0.05 would zero α after 10 instances (0.5/0.05) and freeze learning; not yet verified against the RL4OMP code (ref. [28]) |
| 2026-11-06 | C | explore-first order: Eulerian circuit of complete digraph with self-loops (Hierholzer from node 0, out-edges in portfolio order); instances 0..K² play its K²+1 nodes, greedy (argmax Q, ties → lowest index) afterwards. Reward Eq. 11 uses min/max *including* the current instance; first instance has no state, so no update | covers every (s,a) once for any K; K² transitions = 144 for K = 12 |
