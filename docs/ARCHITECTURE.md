# Architecture

## Mapping to the paper

In LB4OMP (paper Fig. 3), `kmp_dispatch.cpp` has three hooks: **init**, called once before
every loop instance, **next**, called by a thread asking for a chunk, and **finish**. The
selection methods run inside *init*. Ours has the same shape:

```
            ┌───────────────── for t in 0..T-1 (time-steps) ─────────────────┐
            │  for each loop l of the app:                                    │
 Selector   │    algo = selector[l].select(t)        <- "init": pick algorithm │
 (B or C)   │                                                                 │
            │    parallel_for(team, algorithm[l][algo], N, P, chunk):         │
 SchedAlgo  │      algo.begin(params)                <- init                  │
 (A)        │      every thread: while algo.next_chunk(tid,&s,&n):  <- next   │
            │                      body(s, s+n); algo.chunk_done(...)         │
            │      algo.end(finish_times)            <- finish                │
            │                                                                 │
            │    selector[l].observe(t, algo, T_par, LIB)                     │
            │    write one CSV row                                            │
            └─────────────────────────────────────────────────────────────────┘
```

## Interfaces you implement

### `SchedAlgorithm` (`runtime/include/lb/algorithm.h`), Member A

```cpp
void begin(const LoopParams& p);                              // master, before each instance
bool next_chunk(int tid, int64_t* start, int64_t* size);      // CONCURRENT, all threads
bool needs_chunk_timing() const;                              // true -> chunk_done() is called
void chunk_done(int tid, int64_t size, double seconds);       // after each chunk (adaptive)
void end(const std::vector<double>& finish_s);                // master, after each instance
```

- `LoopParams{N, P, chunk}`: `chunk == 0` means *default*. `p.min_chunk()` gives the lower bound.
- One object per (loop, algorithm) lives for the whole run, so state that must persist
  (AWF weights, mAF μ/σ) belongs in members that `begin()` does not reset.
- Synchronisation patterns, all shown in `algos_reference.cpp`: per-thread padded state
  (STATIC), `fetch_add` (SS), compare-exchange loop (GSS). A `std::mutex` is fine where it
  makes things simpler. LB4OMP uses mutexes in several algorithms too, and mutex vs. atomic
  overhead is itself something the paper discusses (FAC2 vs mFAC2).

### `Selector` (`runtime/include/lb/selector.h`), Members B and C

```cpp
Algo select(int timestep);              // which algorithm for this instance
void observe(const Observation& o);     // {timestep, algo, t_par, lib_pct} of that instance
```

- Called only by the master thread, so no locking is needed.
- `SelectorConfig` has the portfolio (12 algorithms, or fewer with `--portfolio`), a seed, and
  the RL parameters.
- Never hard-code 12: use `cfg.portfolio.size()`.

### `exp_chunk(N, P)`, Member B

Called once per loop when the benchmark runs with `--chunk exp`.

## Files

| File | Status | Owner |
|---|---|---|
| `include/lb/common.h` | done | shared |
| `include/lb/thread_team.h`, `src/thread_team.cpp` | done: persistent pool, master = tid 0, Linux pinning | shared |
| `include/lb/parallel_for.h` | done: runs one instance, measures T_par / LIB / imbalance, optional chunk trace | shared |
| `include/lb/algorithm.h`, `src/algorithm_registry.cpp` | done: names, parsing, factory | shared |
| `src/algos_reference.cpp` | done: STATIC, SS, GSS | shared |
| `src/algos_portfolio.cpp` | **TODO** | A |
| `include/lb/selector.h`, `src/selectors_basic.cpp` | done: registry + `fixed:<ALGO>` | shared |
| `src/selectors_expert.cpp` | **TODO** | B |
| `src/selectors_rl.cpp` | **TODO** | C |
| `include/lb/app.h`, `src/apps/apps.cpp` | done: mandelbrot (3 loops), stream, tc | shared |
| `src/main.cpp` | done: `lb_bench` CLI → CSV | shared |
| `tests/test_main.cpp` | done (add your own tests freely) | shared |

## Measurement details

- `T_par` = wall time from just before waking the team to the last thread finishing
  (`steady_clock`). Thread wake-up latency is included, matching the paper's point about
  threads arriving asynchronously.
- `finish[tid]` = when thread `tid` got `false` from `next_chunk`. LIB = (1 − mean/max)·100 (Eq. 8).
- Chunk timing is measured only when the algorithm asks for it, so non-adaptive algorithms pay no timer overhead.
- STREAM arrays are first-touched with a static partition before timing.
- Each app's `checksum()` must not depend on the schedule. `make test` verifies this for TC.

## Useful commands

```bash
build/lb_bench --help
build/lb_bench --list
build/lb_bench --app tc --method fixed:GSS --timesteps 50 --out /tmp/x.csv
build/lb_bench --app mandelbrot --method qlearn --reward LT --portfolio STATIC,SS,GSS --timesteps 200
build/lb_bench --app mandelbrot --method fixed:TSS --timesteps 3 --trace-chunks /tmp/tss.csv
```
