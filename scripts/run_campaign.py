#!/usr/bin/env python3
"""Run the experiment campaign on THIS machine and store results under
results/raw/<system>/.

Resumable: a job whose output file already exists is skipped, so you can
stop with Ctrl-C at any time and re-run the same command to continue. Jobs
that are not implemented yet (exit code 3) are recorded and simply retried the
next time you run the campaign.

Examples
  python scripts/run_campaign.py --system alice-m2 --profile smoke
  python scripts/run_campaign.py --system alice-m2                     # full
  python scripts/run_campaign.py --system alice-m2 --only fixed --apps tc
  python scripts/run_campaign.py --system alice-m2 --only rl_sweep
  python scripts/run_campaign.py --system alice-m2 --dry-run
"""
from __future__ import annotations

import argparse
import csv
import gzip
import os
import random
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import system_info  # noqa: E402
from common import BENCH, RAW, check_system_name, load_campaign  # noqa: E402

ALL_SECTIONS = ["fixed", "expert", "rl", "chunks", "rl_sweep"]


@dataclass
class Job:
    section: str
    app: str
    method: str          # lb_bench --method
    chunk: str           # default | exp
    rep: int
    out: Path
    extra: list[str] = field(default_factory=list)
    trace: Path | None = None

    @property
    def name(self) -> str:
        return str(self.out.relative_to(RAW))


def build_jobs(cfg: dict, system: str, sections: list[str], apps: list[str] | None,
               reps: int | None) -> list[Job]:
    run = cfg["run"]
    base = RAW / system
    nreps = reps if reps is not None else run["reps"]
    app_list = apps or run["apps"]
    jobs: list[Job] = []

    def add(section, app, method, tag, chunk, rep, extra=()):
        out = base / section / f"{app}__{tag}__{chunk}__rep{rep}.csv.gz"
        jobs.append(Job(section, app, method, chunk, rep, out, list(extra)))

    for rep in range(nreps):
        for app in app_list:
            for chunk in run["chunk_modes"]:
                if "fixed" in sections:
                    for algo in cfg["fixed"]["algorithms"]:
                        add("fixed", app, f"fixed:{algo}", f"fixed-{algo}", chunk, rep)
                if "expert" in sections:
                    for m in cfg["expert"]["methods"]:
                        add("expert", app, m, m, chunk, rep)
                if "rl" in sections:
                    for m in cfg["rl"]["methods"]:
                        for r in cfg["rl"]["rewards"]:
                            add("rl", app, m, f"{m}-{r}", chunk, rep, ["--reward", r])

    if "rl_sweep" in sections:
        sw = cfg["rl_sweep"]
        for rep in range(reps if reps is not None else sw["reps"]):
            for app in apps or sw["apps"]:
                for chunk in sw["chunk_modes"]:
                    for m in sw["methods"]:
                        for r in sw["rewards"]:
                            for al in sw["alphas"]:
                                for ga in sw["gammas"]:
                                    add("rl_sweep", app, m, f"{m}-{r}-a{al}-g{ga}", chunk, rep,
                                        ["--reward", r, "--alpha", str(al), "--gamma", str(ga)])

    if "chunks" in sections:
        ch = cfg["chunks"]
        for algo in cfg["fixed"]["algorithms"]:
            for chunk in run["chunk_modes"]:
                trace = base / "chunks" / f"{ch['app']}__{algo}__{chunk}.csv.gz"
                j = Job("chunks", ch["app"], f"fixed:{algo}", chunk, 0, trace,
                        ["--timesteps", str(ch["timesteps"]),
                         "--trace-timestep", str(ch["trace_timestep"])])
                j.trace = trace
                jobs.append(j)
    return jobs


def gzip_to(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".part")
    with open(src, "rb") as fi, gzip.open(tmp, "wb", compresslevel=6) as fo:
        shutil.copyfileobj(fi, fo)
    tmp.replace(dst)


def log_status(system: str, job: Job, status: str, code: int, wall: float, msg: str) -> None:
    path = RAW / system / "status.csv"
    new = not path.exists()
    with open(path, "a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["time", "job", "status", "exit_code", "wall_s", "message"])
        w.writerow([datetime.now().isoformat(timespec="seconds"), job.name, status, code,
                    f"{wall:.1f}", msg])


def run_job(job: Job, system: str, run: dict, threads: int, tmpdir: Path,
            portfolio: str = "") -> tuple[str, int, str]:
    csv_tmp = tmpdir / "run.csv"
    trace_tmp = tmpdir / "trace.csv"
    cmd = [str(BENCH), "--app", job.app, "--method", job.method, "--chunk", job.chunk,
           "--rep", str(job.rep), "--system", system, "--size", run["size"],
           "--threads", str(threads), "--max-seconds", str(run["max_seconds"]),
           "--out", str(csv_tmp)]
    if job.trace is None:
        cmd += ["--timesteps", str(run["timesteps"])]
    else:
        cmd += ["--trace-chunks", str(trace_tmp)]
    cmd += job.extra
    if portfolio:
        cmd += ["--portfolio", portfolio]
    if sys.platform.startswith("linux"):
        cmd.append("--pin")
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=run["timeout"])
    except subprocess.TimeoutExpired:
        return "timeout", -1, f"killed after {run['timeout']}s"
    msg = (p.stderr.strip().splitlines() or [""])[-1]
    if p.returncode == 3:
        return "not_implemented", 3, msg
    if p.returncode != 0:
        return "error", p.returncode, msg
    gzip_to(trace_tmp if job.trace else csv_tmp, job.out)
    return ("truncated" if "TRUNCATED" in msg else "ok"), 0, msg


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--system", required=True, help="label of this machine, e.g. alice-m2")
    ap.add_argument("--profile", default="full", help="smoke | quick | full (see campaign.toml)")
    ap.add_argument("--only", help=f"comma list of sections: {','.join(ALL_SECTIONS)}")
    ap.add_argument("--apps", help="comma list, e.g. mandelbrot,tc")
    ap.add_argument("--reps", type=int, help="override number of repetitions")
    ap.add_argument("--threads", type=int, help="override thread count")
    ap.add_argument("--portfolio", help="restrict to these algorithms, e.g. STATIC,SS,GSS "
                    "(results go to results/raw/<system>-p<k>/)")
    ap.add_argument("--force", action="store_true", help="re-run jobs whose output exists")
    ap.add_argument("--dry-run", action="store_true", help="only list what would run")
    a = ap.parse_args()

    system = check_system_name(a.system)
    cfg = load_campaign(a.profile)
    run = cfg["run"]
    sections = a.only.split(",") if a.only else list(run["sections"])
    bad = set(sections) - set(ALL_SECTIONS)
    if bad:
        sys.exit(f"unknown section(s): {bad}")
    portfolio = a.portfolio or ",".join(run.get("portfolio", []))
    if portfolio:
        # reduced portfolio (e.g. before all 12 algorithms exist): separate folder
        cfg["fixed"]["algorithms"] = [x for x in cfg["fixed"]["algorithms"]
                                      if x in portfolio.split(",")]
        system = f"{system}-p{len(portfolio.split(','))}"
    if a.profile == "smoke":
        # smoke results must never be mixed with real ones
        system = f"{system}-smoke" if not system.endswith("-smoke") else system

    if not BENCH.exists():
        sys.exit("build/lb_bench not found: run `make build` first")

    info = system_info.write(system)
    threads = a.threads or run["threads"] or info["physical_cores"]

    jobs = build_jobs(cfg, system, sections, a.apps.split(",") if a.apps else None, a.reps)
    todo = [j for j in jobs if a.force or not j.out.exists()]
    if run.get("shuffle", True):
        # shuffle inside each repetition, keep repetitions in order
        rng = random.Random(12345)
        by_rep: dict[int, list[Job]] = {}
        for j in todo:
            by_rep.setdefault(j.rep, []).append(j)
        todo = []
        for rep in sorted(by_rep):
            rng.shuffle(by_rep[rep])
            todo += by_rep[rep]

    print(f"system={system} profile={a.profile} threads={threads} sections={sections}")
    print(f"{len(jobs)} jobs in campaign, {len(jobs) - len(todo)} already done, {len(todo)} to run")
    if a.dry_run:
        for j in todo:
            print("  ", j.name, j.method, *j.extra)
        return
    if sys.platform == "darwin":
        print("tip: keep the laptop plugged in; run under `caffeinate -i` to prevent sleep")

    tmpdir = RAW / system / ".tmp"
    tmpdir.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    t_start = time.time()
    try:
        for i, job in enumerate(todo, 1):
            t0 = time.time()
            status, code, msg = run_job(job, system, run, threads, tmpdir, portfolio)
            wall = time.time() - t0
            counts[status] = counts.get(status, 0) + 1
            log_status(system, job, status, code, wall, msg)
            elapsed = time.time() - t_start
            eta = elapsed / i * (len(todo) - i)
            print(f"[{i}/{len(todo)}] {status:15s} {wall:6.1f}s  {job.name}   "
                  f"(ETA {eta / 60:.0f} min)", flush=True)
            if status == "error":
                print(f"    {msg}")
    except KeyboardInterrupt:
        print("\ninterrupted: re-run the same command to continue where you stopped")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("summary:", counts)
    if counts.get("not_implemented"):
        print("jobs marked not_implemented will run automatically next time once the code exists")


if __name__ == "__main__":
    main()
