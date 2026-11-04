# Slides, report and video

## Slides (due Thu 11/6, 11:59 pm)

Required sections (project description): background, problem statement, novelty, method,
simulation results (**original figures next to ours**), conclusion. Use keywords, not paragraphs.
Explain the details out loud.

| # | Slide | Content | Owner |
|---|---|---|---|
| 1 | Title | paper, authors, our names | – |
| 2 | Background: loop scheduling | an OpenMP `for`, threads, chunks; STATIC vs dynamic picture | A |
| 3 | Background: the portfolio | 12 algorithms in 3 families; paper Fig. 1/2 (chunk sizes) | A |
| 4 | Problem statement | the best algorithm depends on (app, system); Rice's algorithm selection problem; paper Fig. 4 | B |
| 5 | Expert-based selection | RandomSel / ExhaustiveSel / ExpertSel / expChunk, one diagram | B |
| 6 | Novelty | RL inside the OpenMP runtime: state = algorithm, action = next algorithm, reward LT/LIB | C |
| 7 | RL method | Q-Learn vs SARSA update, explore-first 144 steps, rewards | C |
| 8 | Our setup | our runtime vs LB4OMP, 3 apps, 3 laptops (table from PAPER_SUBSET.md) | all |
| 9–11 | Results | paper figure ↔ our figure, side by side (preliminary on 11/6) | all |
| 12 | Conclusion / next steps | what we expect to confirm, what is still to do | all |

For 11/6 the results are **preliminary** (`quick` profile, STATIC/SS/GSS only). Say so on the slides.
Put screenshots of the paper's figures in `figures/paper/` (e.g. `paper_fig5.png`).

## Final report (due 12/1)

Use the course template. Suggested mapping:

1. Introduction & background: A
2. Problem & related work: B
3. Method (expert + RL): B + C
4. Our implementation & setup (ARCHITECTURE.md, PAPER_SUBSET.md): all
5. Results, figure by figure, paper vs ours: owner of each figure
6. **Why our results differ**: decision log + expected differences + measured evidence: all
7. Conclusion
8. **Contributions of each member**: required by the course; reference the ownership table and git history

## Video (due 12/1)

Show, in one screen recording (~5–10 min):

1. `git clone` → `make setup` → `make build` → `make test` (all PASS)
2. `make smoke SYSTEM=demo` (shows the campaign runner)
3. `make check SYSTEM=<a real system>` (shows the collected data)
4. `make plots` → open `figures/ours/fig5_heatmap.png` and one Fig. 6 / Fig. 7
5. Briefly: where each member's code lives

## Final package checklist

- [ ] `paper/Korndorfer2025_OpenMP_Scheduling_Selection.pdf`
- [ ] final report PDF
- [ ] video (or link)
- [ ] repository snapshot (zip of the repo at the final commit, or its link)
