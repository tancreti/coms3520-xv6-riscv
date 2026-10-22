#!/usr/bin/env python3
"""Project 1C workload checker.

Reads a workload definition file (kernel/workloads.c by default) and
checks the rules that keep results meaningful:

  - slot counts match, at most WL_MAXSLOTS slots, nice in [-20, 19]
  - nonzero arrivals and I/O lengths are not multiples of 2, 3, or 5,
    so they do not line up with common quantum boundaries (a job that
    arrives exactly when a quantum expires is dispatched at once, which
    hides the scheduler's real response time)
  - whether fairness can be measured: every batch job must be present
    at once, so the last batch arrival must come before any batch job
    could finish

Examples:
  python3 tools/check_workloads.py
  python3 tools/check_workloads.py kernel/workloads.c
"""
import re
import sys

MAXSLOTS = 16
QUANTUM_FACTORS = (2, 3, 5)

DEF_RE = re.compile(r'\{\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*\{(.*?)\}\s*\}', re.S)
SLOT_RE = re.compile(r'\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}')


def aligned(v):
    return v > 0 and any(v % f == 0 for f in QUANTUM_FACTORS)


def check(path):
    src = re.sub(r"//[^\n]*", "", open(path).read())
    errors = 0
    seen = set()
    for m in DEF_RE.finditer(src):
        wid, name, n = int(m.group(1)), m.group(2), int(m.group(3))
        slots = [tuple(map(int, s)) for s in SLOT_RE.findall(m.group(4))]
        problems, notes = [], []
        if wid in seen:
            problems.append("duplicate id")
        seen.add(wid)
        if n != len(slots):
            problems.append("nslots is %d but %d slots are listed" % (n, len(slots)))
        if not 1 <= len(slots) <= MAXSLOTS:
            problems.append("needs 1-%d slots" % MAXSLOTS)
        for i, (arr, nice, cpu, io, rep) in enumerate(slots):
            if not -20 <= nice <= 19:
                problems.append("slot %d: nice %d out of range" % (i, nice))
            if cpu < 1 or rep < 1 or arr < 0:
                problems.append("slot %d: bad arrival, cpu_burst, or repeats" % i)
            if (io == 0) != (rep == 1):
                problems.append("slot %d: batch jobs need io 0 and repeats 1; "
                                "interactive jobs need both > 0" % i)
            if aligned(arr):
                problems.append("slot %d: arrival %d is a multiple of 2, 3, or 5" % (i, arr))
            if aligned(io):
                problems.append("slot %d: I/O %d is a multiple of 2, 3, or 5" % (i, io))

        batch = [(a, c) for a, _, c, io, r in slots if io == 0 and r == 1]
        if len(batch) < 2:
            notes.append("fairness n/a (fewer than 2 batch jobs)")
        else:
            last = max(a for a, _ in batch)
            first_done = min(a + c for a, c in batch)
            if last <= first_done:
                notes.append("fairness window opens at tick %d" % last)
            else:
                notes.append("fairness n/a for preemptive schedulers (a batch "
                             "job can finish by %d, last arrives at %d)" % (first_done, last))
        cpu = sum(c * r for _, _, c, _, r in slots)
        span = max(a + c * r + io * (r - 1) for a, _, c, io, r in slots)
        notes.append("%d batch, %d interactive, %d ticks of CPU, at least %d ticks long"
                     % (len(batch), len(slots) - len(batch), cpu, max(cpu, span)))

        print("workload %d (%s): %s" % (wid, name, "FAIL" if problems else "ok"))
        for p in problems:
            print("  error:", p)
        for p in notes:
            print("  note:", p)
        errors += len(problems)
    if not seen:
        print("no workloads found in", path)
        errors += 1
    return errors


if __name__ == "__main__":
    paths = sys.argv[1:] or ["kernel/workloads.c"]
    sys.exit(1 if sum(check(p) for p in paths) else 0)
