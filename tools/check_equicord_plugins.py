#!/usr/bin/env python3
"""Derive and report the status of every Equicord plugin in Qord.

Exit codes:
  0  no integrity errors (and, with --strict, every feasible plugin is at
     least Tested and every Blocked plugin has a reason)
  1  integrity errors, or --strict requirements not met
  2  generated docs are stale (--check-docs)
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qord_inventory as inv  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true", help="also fail unless every feasible plugin is at least Tested")
    ap.add_argument("--test-results", type=Path, help="ctest --output-junit file; Tested then requires a passing run")
    ap.add_argument("--write-docs", action="store_true", help="regenerate the compatibility matrix, source doc, roadmap and README plugin lists")
    ap.add_argument("--check-docs", action="store_true", help="fail if the generated docs are out of date")
    ap.add_argument("--json", action="store_true", help="print the summary as JSON")
    ap.add_argument("--list", choices=inv.ALL_STATUSES, help="list plugins with this status")
    ap.add_argument("-v", "--verbose", action="store_true", help="print warnings")
    args = ap.parse_args()

    if args.test_results and not args.test_results.is_file():
        print(f"error: test results file {args.test_results} not found", file=sys.stderr)
        return 1

    records, errors, warnings, ctx = inv.evaluate(args.test_results)
    summary = inv.summarize(records)

    if args.json:
        print(json.dumps({"summary": summary, "errors": [str(e) for e in errors],
                          "warnings": [str(w) for w in warnings]}, indent=2))
    else:
        st = ctx["stats"]
        print(f"Inventory: {st['total']} plugins "
              f"({', '.join(f'{n} {k}' for k, n in st['by_root'].items())})")
        if not ctx["test_results_supplied"]:
            print("Note: no --test-results given; Tested means a test is present and registered, not that it passed.")
        print()
        width = max(len(s) for s in inv.ALL_STATUSES)
        for s in inv.ALL_STATUSES:
            print(f"  {s:<{width}}  {summary[s]:>4}")
        print(f"  {'Total':<{width}}  {summary['total']:>4}")
        print()
        print(f"  Implemented or better:   {summary['implemented_or_better']}")
        print(f"  Tested or better:        {summary['tested_or_better']}")
        print(f"  Missing (not evaluated): {summary['missing']}")
        print(f"  Without metadata:        {summary['without_metadata']}")
        print(f"  Manifests without tests: {summary['without_tests']}")
        print(f"  Blocked without reason:  {summary['blocked_without_reason']}")

    if args.list:
        print()
        for r in sorted(records, key=lambda r: r.name.lower()):
            if r.status == args.list:
                print(f"  {r.name}  {r.qord_path}  {r.notes}")

    if args.verbose and warnings:
        print("\nWarnings:")
        for w in warnings:
            print(f"  {w}")

    rc = 0
    if errors:
        print(f"\n{len(errors)} integrity error(s):", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        rc = 1

    if args.strict:
        not_done = [r for r in records if r.status not in (inv.TESTED, inv.COMPLETE, inv.BLOCKED)]
        no_reason = [r for r in records if r.status == inv.BLOCKED and len(r.notes) < 20]
        if not_done or no_reason:
            print(f"\nstrict: {len(not_done)} feasible plugin(s) below Tested, "
                  f"{len(no_reason)} Blocked without a reason", file=sys.stderr)
            rc = 1

    if args.write_docs:
        for p in inv.write_docs(records, ctx):
            print(f"wrote {p.relative_to(inv.ROOT).as_posix()}")
    elif args.check_docs:
        stale = inv.docs_up_to_date(records, ctx)
        if stale:
            print("\nstale generated docs (run tools/check_equicord_plugins.py --write-docs): "
                  + ", ".join(p.relative_to(inv.ROOT).as_posix() for p in stale), file=sys.stderr)
            rc = rc or 2
    return rc


if __name__ == "__main__":
    sys.exit(main())
