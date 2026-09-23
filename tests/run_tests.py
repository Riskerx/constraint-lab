#!/usr/bin/env python3
"""Seeded differential tests against independent, exhaustive Python baselines."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def queens_baseline(n, fixed=(), blocked=()):
    # Row permutations, then diagonal checks. No AC-3 or C++ model is reused.
    return {
        rows for rows in itertools.permutations(range(n))
        if all(rows[c] == r for c, r in fixed)
        and all(rows[c] != r for c, r in blocked)
        and len({c + r for c, r in enumerate(rows)}) == n
        and len({c - r for c, r in enumerate(rows)}) == n
    }


def crypto_baseline(expression):
    words = expression.replace("+", "=").split("=")
    letters = sorted(set("".join(words)))
    leading = {w[0] for w in words if len(w) > 1}
    answers = set()
    for values in itertools.permutations(range(10), len(letters)):
        mapping = dict(zip(letters, values))
        if any(mapping[l] == 0 for l in leading):
            continue
        a, b, c = (int("".join(str(mapping[l]) for l in word)) for word in words)
        if a + b == c:
            answers.add(values)
    return answers


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, default=ROOT / "build" / "solver")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--cases", type=int, default=60, help="random cases per puzzle family")
    parser.add_argument("--report", type=Path, default=ROOT / "build" / "test-report.json")
    args = parser.parse_args()
    if args.cases < 0:
        parser.error("--cases must be nonnegative")
    rng, started, cases = random.Random(args.seed), time.perf_counter(), []
    current_command = []

    def invoke(command, expect_error=False):
        nonlocal current_command
        current_command = [str(args.solver.resolve()), *command]
        result = subprocess.run(current_command, text=True, capture_output=True, timeout=30)
        if expect_error:
            require(result.returncode == 2 and not result.stdout and "error:" in result.stderr, f"bad input was accepted: {command}")
            return None
        require(result.returncode == 0, result.stderr)
        return json.loads(result.stdout)

    def exact(command, expected, no_ac3=False):
        run = [*command, "--limit", "0", "--timeout-ms", "0", "--trace-limit", "0"]
        if no_ac3:
            run.append("--no-ac3")
        data = invoke(run)
        actual = {tuple(values) for values in data["solutions"]}
        require(data["complete"], f"search incomplete: {command}")
        require(actual == expected, f"baseline mismatch: {command}; missing={len(expected-actual)}, extra={len(actual-expected)}")
        require(len(actual) == len(data["solutions"]), f"duplicate solutions: {command}")
        require(data["status"] == ("solved" if expected else "unsatisfiable"), "incorrect completion status")
        fingerprint = hashlib.sha256(json.dumps(sorted(actual), separators=(",", ":")).encode()).hexdigest()
        cases.append({"command": run, "solution_count": len(actual), "solutions_sha256": fingerprint, "nodes": data["stats"]["nodes"], "constraint_checks": data["stats"]["constraint_checks"]})
        return data

    try:
        counts = [1, 0, 0, 2, 10, 4, 40, 92]
        for n, count in enumerate(counts, 1):
            baseline = queens_baseline(n)
            require(len(baseline) == count, "known N-Queens count disagrees with baseline")
            exact(["nqueens", str(n)], baseline)
            if n <= 6:
                exact(["nqueens", str(n)], baseline, no_ac3=True)
        for _ in range(args.cases):
            n = rng.randint(1, 7)
            fixed = [(rng.randrange(n), rng.randrange(n)) for _ in range(rng.randint(0, 2))]
            blocked = [(c, r) for c in range(n) for r in range(n) if rng.random() < .18]
            command = ["nqueens", str(n)]
            for name, cells in [("--fix", fixed), ("--block", blocked)]:
                for c, r in cells:
                    command.extend([name, f"{c}:{r}"])
            exact(command, queens_baseline(n, fixed, blocked))
        edge_puzzles = ["A+A=A", "A+B=C", "A+A=B", "A+B=BA", "AB+C=BA", "A+B=CC", "AA+BB=CC", "AA+AA=BB", "AB+AB=CD", "AB+CD=A", "A+A=BBB"]
        for expression in edge_puzzles:
            baseline = crypto_baseline(expression)
            exact(["cryptarithm", expression], baseline)
            if expression in {"A+A=A", "A+B=C", "AA+AA=BB"}:
                exact(["cryptarithm", expression], baseline, no_ac3=True)
        for i in range(args.cases):
            if i % 2 == 0:
                alphabet = "ABCD"[:rng.randint(2, 4)]
                words = ["".join(rng.choice(alphabet) for _ in range(rng.randint(1, 3))) for _ in range(3)]
            else:
                # Construct a satisfiable puzzle from an addition using <=4 digits.
                while True:
                    a, b = rng.randint(0, 99), rng.randint(0, 99)
                    digits = sorted(set(str(a) + str(b) + str(a + b)))
                    if len(digits) <= 4:
                        break
                names = dict(zip(digits, "ABCD"))
                words = ["".join(names[d] for d in str(value)) for value in (a, b, a + b)]
            expression = f"{words[0]}+{words[1]}={words[2]}"
            exact(["cryptarithm", expression], crypto_baseline(expression))
        exact(["cryptarithm", "SEND+MORE=MONEY"], {(7, 5, 1, 6, 0, 8, 9, 2)})
        exact(["nqueens", "4", "--fix", "0:1", "--fix", "0:2"], set())
        exact(["nqueens", "4", "--fix", "0:1", "--block", "0:1"], set())

        traced = invoke(["nqueens", "8", "--limit", "1", "--timeout-ms", "0", "--trace-limit", "20000"])
        require(traced["status"] == "solution_limit" and not traced["complete"] and len(traced["solutions"]) == 1, "solution limit contract")
        require(traced["trace"][0]["domains"] == traced["initial_domains"], "trace must start at initial domains")
        require(any(e["type"] == "solution" for e in traced["trace"]), "missing solution event")
        for event in traced["trace"]:
            require(len(event["domains"]) == 8, "incorrect trace shape")
            require(all(set(d) <= set(initial) for d, initial in zip(event["domains"], traced["initial_domains"])), "trace contains an invalid domain")
        require(tuple(traced["solutions"][0]) in queens_baseline(8), "invalid traced solution")
        limited = invoke(["nqueens", "8", "--max-nodes", "1", "--timeout-ms", "0"])
        require(limited["status"] == "node_limit" and limited["stats"]["nodes"] == 1 and not limited["complete"], "node limit contract")
        capped = invoke(["nqueens", "8", "--trace-limit", "2", "--timeout-ms", "0"])
        require(len(capped["trace"]) == 2 and capped["trace_truncated"], "trace cap contract")
        timeout = invoke(["nqueens", "32", "--limit", "0", "--timeout-ms", "1", "--trace-limit", "0"])
        require(timeout["status"] == "timeout" and not timeout["complete"], "deadline must be distinguished from unsatisfiability")
        repeat = invoke(["nqueens", "8", "--limit", "1", "--timeout-ms", "0", "--trace-limit", "20000"])
        traced["stats"].pop("elapsed_ms")
        repeat["stats"].pop("elapsed_ms")
        require(traced == repeat, "same input must have the same trace and counters")
        for command in [["nqueens", "0"], ["nqueens", "33"], ["nqueens", "4x"], ["nqueens", "4", "--fix", "4:0"], ["nqueens", "4", "--limit", "-1"], ["nqueens", "4", "--max-nodes"], ["nqueens", "4", "--mystery", "2"], ["cryptarithm", "A++B=C"], ["cryptarithm", "ABCDEFGHIJ+K=L"], ["cryptarithm", "A+B=C", "--fix", "0:0"]]:
            invoke(command, expect_error=True)
        # Verify the Python renderer with both puzzle families and disabled/capped traces.
        sys.path.insert(0, str(ROOT / "python"))
        from visualize import render
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            for i, payload in enumerate([capped, timeout, invoke(["cryptarithm", "SEND+MORE=MONEY"]) ]):
                output = Path(temp) / f"replay-{i}.html"
                render(payload, output)
                html = output.read_text(encoding="utf-8")
                embedded = html.split('<script id="solver-data" type="application/json">', 1)[1].split('</script>', 1)[0]
                require(json.loads(embedded) == payload and "__SOLVER_DATA__" not in html, "HTML JSON round trip")
            payload["puzzle"]["title"] = "</script><script>unexpected()</script>"
            render(payload, output)
            require(payload["puzzle"]["title"] not in output.read_text(encoding="utf-8"), "JSON must not escape its script element")
        report = {"passed": True, "seed": args.seed, "random_cases_per_family": args.cases, "differential_cases": len(cases), "regressions": "engine CTest plus limits, trace, replay, input errors, determinism", "cases": cases}
        report["reproducibility_sha256"] = hashlib.sha256(json.dumps(cases, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: {len(cases)} exact solution-set comparisons + CLI/replay regressions (seed {args.seed})")
        print(f"Fingerprint: {report['reproducibility_sha256']}")
        print(f"Elapsed: {time.perf_counter()-started:.2f}s | report: {args.report}")
    except (AssertionError, subprocess.TimeoutExpired, OSError, ValueError) as error:
        failure = {"passed": False, "seed": args.seed, "command": current_command, "error": str(error), "completed_cases": cases}
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(failure, indent=2) + "\n", encoding="utf-8")
        print(f"FAIL (seed {args.seed}): {error}\nReplay command: {current_command}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
