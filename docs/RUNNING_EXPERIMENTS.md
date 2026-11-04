# Running experiments and submitting data

Every member runs **the same campaign** (`experiments/campaign.toml`) on **their own machine**.
Each machine writes only into its own folder `results/raw/<system>/`, so the three of you never
get git conflicts on data.

## 1. Before a real run

- Laptop **plugged in**, power mode "High performance" / not "Low power".
- Close browsers, IDE indexers, Spotify, Docker, and sync clients (Dropbox / OneDrive / iCloud).
- macOS: run under `caffeinate -i` so the machine does not sleep. Linux: disable suspend.
- Don't use the laptop during the run. Overnight is ideal.
- Don't change the code while the campaign runs (it calls `build/lb_bench` for each job).

## 2. Commands

```bash
make build && make test                                    # 0 FAIL required

# preliminary (slides): only implemented algorithms, shorter runs
make campaign SYSTEM=alice-m2 PROFILE=quick ARGS="--portfolio STATIC,SS,GSS"

# final
caffeinate -i make campaign SYSTEM=alice-m2                # fixed + expert + rl  (~1-3 h)
make trace SYSTEM=alice-m2                                 # chunk traces for Fig. 1/2 (seconds)
python scripts/run_campaign.py --system alice-m2 --only rl_sweep   # Member C asks for it (RQ2)

make check SYSTEM=alice-m2                                 # done / missing / not_implemented
```

Useful options (pass to `run_campaign.py` directly or via `ARGS="..."`):

| Option | Effect |
|---|---|
| `--only fixed,expert` | run some sections only (`fixed, expert, rl, chunks, rl_sweep`) |
| `--apps tc,stream` | run some apps only |
| `--reps 2` | fewer repetitions (e.g. while testing) |
| `--portfolio STATIC,SS,GSS` | only these algorithms. Results go to `results/raw/<system>-p3/`, separate from the final data |
| `--dry-run` | list the jobs without running them |
| `--force` | re-run jobs whose output already exists |
| `--threads 4` | override the thread count (default = physical cores) |

**Resuming:** the campaign skips every job whose output file exists. Stop with Ctrl-C at any
time and run the same command again to continue. Jobs that fail with *not implemented* are
retried automatically on the next run, so you can start the campaign before all code exists and
fill the gaps later.

**Re-running after a code fix:** delete the affected output files (e.g.
`rm results/raw/alice-m2/*/*__fixed-TSS__*`) and run the campaign again. Tell the others; they
must do the same.

**Order:** within each repetition the jobs are shuffled with a fixed seed, so thermal drift
does not always penalise the same algorithm. Repetitions run in order, so a half-finished
campaign still has complete rep 0 data.

**Long runs:** some configurations, such as STREAM with SS, are 100× slower than the rest. A
run stops after `max_seconds` of loop time (90 s by default). It is then extrapolated to T
time-steps and flagged `truncated`; the plots mark it with "cap". Say so in the report.

## 3. Estimated cost (M2, 8 cores, `full` profile)

3 apps × 2 chunk modes × (12 fixed + 3 expert + 4 RL) = 114 runs per repetition × 5 = 570
runs, roughly 1–3 h. Run `--dry-run` to see the exact job count. `make smoke` takes about a minute.

## 4. Submitting your data

```bash
make check SYSTEM=alice-m2          # no CORRUPT lines
git checkout -b data/alice-m2
git add results/raw/alice-m2
git commit -m "data(alice-m2): full campaign"
git push -u origin data/alice-m2    # open a PR, or push to main if the group agrees
```

What gets committed: `results/raw/<system>/**/*.csv.gz`, `system_info.json`, `status.csv`.
What does not (it's in `.gitignore`): `*-smoke/`, `.tmp/`, `results/processed/`, `build/`, `.venv/`.

## 5. Making the figures

```bash
git pull                 # everybody's results/raw/*
make plots               # = make aggregate + all figure scripts -> figures/ours/*.png
```

`make aggregate` merges all systems, computes the Oracle, and writes `results/processed/*.csv`.
The processed files are not committed because anyone can regenerate them. **Commit the final
figures** in `figures/ours/` so the slides and report have stable versions.

Individual figure scripts take options, e.g.
`.venv/bin/python scripts/plots/fig7_8_selections.py --app tc --loop 0 --chunk exp --system bob-i7`.

## 6. Troubleshooting

| Symptom | Fix |
|---|---|
| `build/lb_bench not found` | `make build` |
| `No module named pandas` | `make setup`, and use `.venv/bin/python` (the Makefile does) |
| Everything is `not_implemented` | expected until the owners implement it; check `make test` |
| `error: pass SYSTEM=...` | add `SYSTEM=<your-label>` to the make command |
| Times jump around a lot | thermal throttling or background load; see §1, and re-run the affected reps with `--force` |
| Windows | use WSL2 Ubuntu: `sudo apt install build-essential python3-venv`, then as above |
| Linux `--pin` errors on a VM | run `build/lb_bench` without `--pin`, or set `threads` ≤ the VM's cores |
