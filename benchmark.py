#!/usr/bin/env python3
"""Generate results.csv and a Compression Ratio chart for the BZip2 project.

Default behaviour (no arguments):
  1. Builds the C executable if it is missing.
  2. Runs the C program with no arguments — that walks every file under the
     `input_directory` from config.ini (default ./benchmarks/) through the
     full pipeline and writes ./results/results.csv.
  3. Reads results.csv, computes the bzip2 reference ratio for each file
     (using the system `bzip2` if available), and saves a bar chart at
     ./results/compression_ratio.png comparing "Our impl" vs "bzip2".

Compression ratio is shown in the chart as compressed_size / original_size
(lower bars = better compression). The CSV column CompressionRatio stores
the percentage saved: 100 × (1 − compressed/original); the script converts.

Usage:
  python benchmark.py            # full run + chart
  python benchmark.py --no-run   # use existing results.csv only
  python benchmark.py --no-bzip2 # skip the bzip2 reference line
"""
from __future__ import annotations

import argparse
import csv
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

ROOT = Path(__file__).resolve().parent
EXE_NAME = "bzip2_impl.exe" if os.name == "nt" else "bzip2_impl"
EXE = ROOT / EXE_NAME
RESULTS_DIR = ROOT / "results"
CSV_PATH = RESULTS_DIR / "results.csv"
CHART_PATH = RESULTS_DIR / "compression_ratio.png"
BENCH_DIR = ROOT / "benchmarks"


def build_if_needed() -> None:
    if EXE.exists():
        return
    print("Building project (make) …", flush=True)
    make_cmd = "mingw32-make" if os.name == "nt" and shutil.which("mingw32-make") else "make"
    subprocess.run([make_cmd], cwd=ROOT, check=True)


def run_pipeline_suite() -> None:
    print("Running full pipeline on benchmarks/ …", flush=True)
    subprocess.run([str(EXE)], cwd=ROOT, check=True)


def read_results_csv() -> List[Dict[str, str]]:
    if not CSV_PATH.exists():
        print(f"results.csv not found at {CSV_PATH}", file=sys.stderr)
        sys.exit(1)
    with CSV_PATH.open(newline="") as f:
        return list(csv.DictReader(f))


def bzip2_ratio_for(path: Path) -> Optional[float]:
    """Return compressed_size / original_size using system bzip2, or None."""
    if shutil.which("bzip2") is None:
        return None
    try:
        original = path.stat().st_size
        if original == 0:
            return None
        result = subprocess.run(
            ["bzip2", "-c", "-9", "-k", str(path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=True,
        )
        return len(result.stdout) / float(original)
    except Exception:
        return None


def make_chart(rows: List[Dict[str, str]], use_bzip2: bool) -> None:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print(
            "matplotlib is not installed; skipping chart.\n"
            "Install with:  pip install matplotlib",
            file=sys.stderr,
        )
        return

    labels: List[str] = []
    ours: List[float] = []
    refs: List[Optional[float]] = []

    bench_dir = BENCH_DIR
    for row in rows:
        name = Path(row["File"]).name
        labels.append(name)
        # CSV column CompressionRatio is percent saved = 100 × (1 − compressed/original).
        # The chart shows compressed/original (lower = better).
        try:
            saved_pct = float(row["CompressionRatio"])
        except (KeyError, ValueError):
            saved_pct = 0.0
        ours.append(max(0.0, 1.0 - saved_pct / 100.0))
        if use_bzip2:
            candidate = bench_dir / name
            refs.append(bzip2_ratio_for(candidate) if candidate.exists() else None)
        else:
            refs.append(None)

    fig, ax = plt.subplots(figsize=(max(8, len(labels) * 0.7), 5))
    x = list(range(len(labels)))
    ax.bar(x, ours, label="Our impl", color="#1f77b4")

    if use_bzip2 and any(r is not None for r in refs):
        bz_x = [i for i, r in enumerate(refs) if r is not None]
        bz_y = [r for r in refs if r is not None]
        ax.plot(bz_x, bz_y, marker="o", color="#ff7f0e", label="bzip2")

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=40, ha="right")
    ax.set_ylabel("Compression Ratio")
    ax.set_title("Compression Ratio per File")
    ax.set_ylim(0.0, 1.05)
    ax.axhline(1.0, linestyle="--", linewidth=0.8, color="gray")
    ax.legend(loc="best")
    fig.tight_layout()
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(CHART_PATH, dpi=150)
    print(f"Chart saved: {CHART_PATH}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run BZip2 pipeline on benchmarks/ and plot the compression ratio chart.",
    )
    parser.add_argument(
        "--no-run",
        action="store_true",
        help="Skip running the C executable; just read results.csv.",
    )
    parser.add_argument(
        "--no-bzip2",
        action="store_true",
        help="Skip the bzip2 reference line on the chart.",
    )
    args = parser.parse_args()

    if not args.no_run:
        build_if_needed()
        run_pipeline_suite()

    rows = read_results_csv()
    if not rows:
        print("results.csv is empty.", file=sys.stderr)
        return 1
    make_chart(rows, use_bzip2=not args.no_bzip2)
    print(f"CSV: {CSV_PATH} ({len(rows)} row(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
