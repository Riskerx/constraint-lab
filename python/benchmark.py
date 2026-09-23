#!/usr/bin/env python3
"""Compare full 8-Queens enumeration with and without AC-3."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, default=ROOT / "build" / "solver")
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "benchmark.json")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    rows, expected = [], None
    for ac3 in (True, False):
        command = [str(args.solver.resolve()), "nqueens", "8", "--limit", "0", "--timeout-ms", "0", "--trace-limit", "0"]
        if not ac3:
            command.append("--no-ac3")
        results = [json.loads(subprocess.check_output(command, text=True)) for _ in range(args.repeats)]
        for result in results:
            found = {tuple(s) for s in result["solutions"]}
            if not result["complete"] or len(found) != 92 or (expected is not None and found != expected):
                raise RuntimeError("benchmark requires complete, matching solution sets")
            expected = found
        stats = results[0]["stats"]
        rows.append({"ac3": ac3, "solutions": 92, "nodes": stats["nodes"], "decisions": stats["decisions"], "constraint_checks": stats["constraint_checks"], "median_ms": statistics.median(r["stats"]["elapsed_ms"] for r in results)})
    report = {"puzzle": "8-Queens", "repeats": args.repeats, "note": "Timing is hardware-dependent; nodes and checks are deterministic. AC-3 has queue overhead and is not always faster.", "results": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Mode              Solutions   Nodes    Checks    Median ms")
    for row in rows:
        print(f"{'AC-3' if row['ac3'] else 'Backtracking':<18}{row['solutions']:>9}{row['nodes']:>8}{row['constraint_checks']:>10}{row['median_ms']:>13.3f}")
    print(report["note"])


if __name__ == "__main__":
    main()
