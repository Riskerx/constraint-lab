#!/usr/bin/env python3
"""Run the C++ solver or replay saved JSON as a self-contained HTML file."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import webbrowser

ROOT = Path(__file__).resolve().parents[1]


def render(data, output):
    if data.get("schema_version") != 1 or data.get("puzzle", {}).get("kind") not in {"nqueens", "cryptarithm"}:
        raise ValueError("expected Constraint Lab schema version 1 JSON")
    # Never let JSON terminate the script element, even for a hand-edited input.
    payload = json.dumps(data, ensure_ascii=True).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    template = (ROOT / "python" / "viewer.html").read_text(encoding="utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(template.replace("__SOLVER_DATA__", payload), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("puzzle", choices=["nqueens", "cryptarithm", "replay"])
    parser.add_argument("value", help="N, WORD+WORD=WORD, or a saved JSON path")
    candidates = [ROOT / "build" / "solver", ROOT / "build" / "Release" / "solver.exe", ROOT / "build" / "solver.exe"]
    parser.add_argument("--solver", type=Path, default=next((p for p in candidates if p.exists()), candidates[0]))
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "replay.html")
    parser.add_argument("--save-json", type=Path)
    parser.add_argument("--open", action="store_true", help="open the generated file in your browser")
    parser.add_argument("--limit", type=int, default=1)
    parser.add_argument("--timeout-ms", type=int, default=2000)
    parser.add_argument("--max-nodes", type=int, default=0)
    parser.add_argument("--trace-limit", type=int, default=2000)
    parser.add_argument("--fix", action="append", default=[])
    parser.add_argument("--block", action="append", default=[])
    parser.add_argument("--no-ac3", action="store_true")
    args = parser.parse_args()
    try:
        if args.puzzle == "replay":
            data = json.loads(Path(args.value).read_text(encoding="utf-8"))
        else:
            command = [str(args.solver.resolve()), args.puzzle, args.value]
            for name in ["limit", "timeout_ms", "max_nodes", "trace_limit"]:
                command.extend(["--" + name.replace("_", "-"), str(getattr(args, name))])
            for name in ["fix", "block"]:
                for value in getattr(args, name):
                    command.extend(["--" + name, value])
            if args.no_ac3:
                command.append("--no-ac3")
            completed = subprocess.run(command, text=True, capture_output=True, check=True)
            data = json.loads(completed.stdout)
        output = args.output.resolve()
        if args.save_json:
            args.save_json.parent.mkdir(parents=True, exist_ok=True)
            args.save_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        render(data, output)
        print(f"{data['puzzle']['title']}: {data['status']}, {len(data['solutions'])} solution(s)")
        print(output)
        if args.open:
            webbrowser.open(output.as_uri())
    except FileNotFoundError as error:
        parser.exit(2, f"Missing file: {error.filename}. Build the solver with CMake first.\n")
    except subprocess.CalledProcessError as error:
        parser.exit(2, error.stderr)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"Cannot create replay: {error}\n")


if __name__ == "__main__":
    main()
