# Reproducing "A Comparative Study of OpenMP Scheduling Algorithm Selection Strategies"

Course project (Operating Systems): reproduction of

> J. H. Müller Korndörfer, A. Mohammed, A. Eleliemy, Q. Guilloteau, R. Krummenacher, F. M. Ciorba.
> *A Comparative Study of OpenMP Scheduling Algorithm Selection Strategies.* arXiv:2507.20312, 2025.
> → [`paper/Korndorfer2025_OpenMP_Scheduling_Selection.pdf`](paper/Korndorfer2025_OpenMP_Scheduling_Selection.pdf)

**The paper in one sentence:** an OpenMP loop can be scheduled by any of 12 algorithms
(STATIC, SS, GSS, …, AWF, mAF), and the best one depends on the application and the
machine. So the runtime should *learn* which one to use while the program runs, either from
expert rules (RandomSel, ExhaustiveSel, ExpertSel) or with reinforcement learning (Q-Learn,
SARSA). The paper compares those approaches against an *Oracle*.

**What we do:** we rebuild that system at laptop scale and regenerate the paper's Figures 1, 2,
4, 5, 6, 7 and 8. See [docs/PAPER_SUBSET.md](docs/PAPER_SUBSET.md) for what is in scope and
what we scaled down.

---

## Quick start (everyone, ~10 minutes)

Requirements: a C++17 compiler (Apple clang / g++ / clang++), `make`, Python ≥ 3.9.
macOS and Linux work natively; on Windows use **WSL2 (Ubuntu)**.

```bash
git clone <repo-url> && cd <repo>
make setup                      # Python venv + numpy/pandas/matplotlib
make build                      # compiles build/lb_bench and build/lb_tests
make test                       # PASS / TODO / FAIL per component (TODOs are expected at first)
make smoke SYSTEM=alice-m2      # 1-minute end-to-end run; results go to results/raw/alice-m2-smoke/ (not committed)
```

`SYSTEM` is the **label of your machine**: lowercase letters, digits and `-`, for example `alice-m2`,
`bob-i7-1260p` or `carol-r7-5800h`. Use the same label for the whole project.

## Who does what

The paper has three building blocks, and each one maps to one person. Every person also
**runs the full experiment campaign on their own laptop**, so our three laptops play the role
of the paper's three HPC systems (Broadwell / Cascade-Lake / EPYC).

| | Member A: *Portfolio* | Member B: *Expert selection* | Member C: *RL selection* |
|---|---|---|---|
| Paper sections | §2, §3.1 | §3.2, §3.3 | §3.4, §3.5, RQ2 |
| Implements | 9 scheduling algorithms: TSS, Static Steal, mFAC2, AWF-B/C/D/E, mAF, Auto(LLVM) | expChunk, RandomSel, ExhaustiveSel, ExpertSel | Q-Learn, SARSA, LT/LIB rewards, explore-first, α-decay |
| Code | `runtime/src/algos_portfolio.cpp` | `runtime/src/selectors_expert.cpp` | `runtime/src/selectors_rl.cpp` |
| Figures | Fig. 1, 2 (chunk sizes), Fig. 4 (c.o.v.) | Fig. 5 (heatmap), Fig. 6 (bars) | Fig. 7/8 (selections), RQ2 α/γ sweep |
| Plot scripts | `fig_chunks.py`, `fig4_cov.py` | `fig5_heatmap.py`, `fig6_bars.py` | `fig7_8_selections.py`, `rq2_rl_params.py` |

The detailed task list, acceptance criteria and decision log are in **[docs/TEAM_PLAN.md](docs/TEAM_PLAN.md)**.
Members B and C do **not** have to wait for A: `make test` checks selectors in a synthetic
environment, and preliminary campaigns can use `--portfolio STATIC,SS,GSS` (already implemented).

## Repository map

```
paper/                       original paper + course project description
docs/
  PAPER_SUBSET.md            scope: what we reproduce, what we scale down, expected differences
  TEAM_PLAN.md               roles, per-member checklists, timeline, decision log  <- start here
  ARCHITECTURE.md            how the runtime works (interfaces you implement)
  RUNNING_EXPERIMENTS.md     running the campaign on your laptop + submitting data
  DATA_FORMAT.md             every CSV column
  PRESENTATION_OUTLINE.md    slides (due 11/6), report, video checklist
runtime/
  include/lb/*.h             interfaces: algorithm, selector, parallel_for, thread team, app
  src/algos_reference.cpp    STATIC, SS, GSS: done, use as examples
  src/algos_portfolio.cpp    Member A
  src/selectors_expert.cpp   Member B
  src/selectors_rl.cpp       Member C
  src/apps/apps.cpp          Mandelbrot, STREAM Triad, Triangle Counting (done)
  src/main.cpp               lb_bench command line (done)
  tests/test_main.cpp        `make test`
experiments/campaign.toml    THE experiment matrix (shared; change only by group agreement)
scripts/
  run_campaign.py            runs all jobs on your machine (resumable)
  check_results.py           completeness report before you push
  aggregate.py               merges all systems -> results/processed/*.csv (+ Oracle)
  plots/*.py                 one script per paper figure -> figures/ours/
results/raw/<system>/        YOUR data (commit it). One folder per machine, so no merge conflicts
figures/ours/                our figures (commit the final ones)
figures/paper/               screenshots of the paper's figures for side-by-side slides
```

## The day-to-day loop

```bash
# implement something, then
make build && make test                      # must show no FAIL
make campaign SYSTEM=alice-m2                # runs every job that is not done yet (Ctrl-C safe)
make check SYSTEM=alice-m2                   # what is done / missing / not implemented
git add results/raw/alice-m2 && git commit -m "alice-m2: fixed + expert runs" && git push
make plots                                   # after pulling everyone's data: figures/ours/*.png
```

The full workflow and the rules for clean measurements on a laptop are in
[docs/RUNNING_EXPERIMENTS.md](docs/RUNNING_EXPERIMENTS.md).

## Deadlines (from the project description)

| Date | Deliverable |
|---|---|
| **Thu 11/6, 11:59 pm** | Slides: background, problem, novelty, method, results (paper figures next to ours), conclusion |
| last two weeks of semester | In-class presentation |
| **Mon 12/1, 11:59 pm** | Final package: original paper + final report + video of running the code and generating the results |

The report must say clearly **what each member contributed**. The ownership table above and
the git history are the evidence.
