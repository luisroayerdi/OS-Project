# Data formats

All tables are CSV. Raw files are gzip-compressed (`.csv.gz`); pandas reads them directly with
`pd.read_csv(path)`.

## Raw: one file per run

`results/raw/<system>/<section>/<app>__<tag>__<chunk_mode>__rep<k>.csv.gz`

- `section`: `fixed` | `expert` | `rl` | `rl_sweep`
- `tag`: `fixed-<ALGO>`, `randomsel`, `exhaustivesel`, `expertsel`, `qlearn-LT`, `sarsa-LIB`, `qlearn-LT-a0.5-g0.9`, …

One row per **loop instance** (time-step × loop):

| Column | Meaning |
|---|---|
| `system` | machine label |
| `app` | `mandelbrot` \| `stream` \| `tc` |
| `loop_id` | 0..L-1 (Mandelbrot has 3 loops: L0 constant, L1 increasing, L2 decreasing) |
| `timestep` | 0..T-1 |
| `T` | number of time-steps requested (fewer rows than T means the run was truncated) |
| `method` | `fixed`, `randomsel`, `exhaustivesel`, `expertsel`, `qlearn`, `sarsa` |
| `method_arg` | algorithm for `fixed`, otherwise empty |
| `reward` | `LT` / `LIB` for RL, otherwise empty |
| `alpha`, `gamma` | RL parameters (0 for non-RL) |
| `chunk_mode` | `default` \| `exp` \| `fixed` (explicit integer) |
| `chunk_param` | chunk parameter value used (0 = default) |
| `rep` | repetition index |
| `threads` | P |
| `N` | iterations of this loop |
| `algo` | algorithm actually used in this instance (what the selector picked) |
| `t_par_s` | parallel loop time, seconds |
| `lib_pct` | percent load imbalance, Eq. 8 |
| `exec_imb_pct` | execution imbalance, Table 2 |
| `n_chunks` | number of chunks (scheduling rounds) |

## Raw: chunk traces (Fig. 1/2)

`results/raw/<system>/chunks/<app>__<ALGO>__<chunk_mode>.csv.gz`, one row per chunk of loop 0 at one time-step:

`system, app, loop_id, timestep, method_arg, chunk_mode, chunk_param, threads, N, chunk_id, tid, start, size, t_assign_s`

## Raw: metadata

- `results/raw/<system>/system_info.json`: CPU, cores, P/E cores, RAM, OS, compiler, git commit
- `results/raw/<system>/status.csv`: log of every job attempt (`ok`, `truncated`, `not_implemented`, `error`, `timeout`)

## Processed (`make aggregate`, not committed)

| File | One row per | Key columns |
|---|---|---|
| `loop_instances.csv.gz` | loop instance (all raw rows + `section`) | as raw |
| `run_totals.csv` | run | `total_s` (sum over loops and steps, extrapolated if truncated), `truncated`, `label` |
| `summary.csv` | configuration | `median_s`, `mean_s`, `std_s`, `n_reps`, `label` |
| `oracle.csv` | (system, app) | `median_s` (per time-step Oracle), `median_s_fixed` (best fixed configuration per loop), `n_configs` |
| `oracle_choices.csv` | (system, app, loop) | best fixed `algo`, `chunk_mode` |
| `oracle_timestep_choices.csv.gz` | (system, app, loop, time-step) | Oracle's `algo`, `chunk_mode`, `t_par_s` |
| `degradation.csv` | configuration + Oracle | `degradation_pct = (median − oracle)/oracle·100`, and `degradation_fixed_pct` against `median_s_fixed` |
| `fig4_cov.csv` | (system, app) | `cov` |

`label` uses the paper's naming: `SS`, `SS,34` (fixed with expChunk = 34), `Q-learn,LT,expChunk`, ….
