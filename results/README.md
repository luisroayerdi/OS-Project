# results/

- `raw/<system>/`: one folder per machine, **written only by that machine's owner**. Commit it.
  - `fixed/ expert/ rl/ rl_sweep/`: one `.csv.gz` per run (see docs/DATA_FORMAT.md)
  - `chunks/`: chunk traces for Fig. 1/2
  - `system_info.json`, `status.csv`
- `raw/<system>-p3/`: preliminary runs with a reduced portfolio (fine to commit; they are kept separate from the final data)
- `raw/*-smoke/`: smoke tests, git-ignored
- `processed/`: produced by `make aggregate`, git-ignored (regenerate anytime)
