#!/usr/bin/env python3
"""Project 1C host-side runner.

Boots xv6 in QEMU once, runs `wl <workload> <scheduler>` for every
combination given, and writes the results to a CSV file.

Examples (run from the repository root, inside the dev container):
  python3 tools/run_wl.py                       # workloads 1-5, schedulers 0-2
  python3 tools/run_wl.py -w 2 -s 0 1 -o out.csv
  python3 tools/run_wl.py POLICY=policy -w 2 -s 0 1   # make variables first
"""
import argparse
import csv
import os
import subprocess
import sys
import time

SUMMARY_FIELDS = ["workload", "scheduler", "switch_cost", "turnaround",
                  "response", "response_worst", "fairness", "overhead_pct",
                  "total_ticks", "other_ticks"]
JOB_FIELDS = ["job", "nice", "batch", "arrival", "first_run", "completion",
              "turnaround", "cpu_units", "ovh_units", "resp_count",
              "resp_sum", "resp_max"]


class Xv6:
    def __init__(self, make_args):
        self.proc = subprocess.Popen(
            ["make", "-s", "qemu"] + make_args, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.buf = b""
        self.read_until(b"$ ", 120)

    def read_until(self, marker, timeout):
        end = time.time() + timeout
        os.set_blocking(self.proc.stdout.fileno(), False)
        while marker not in self.buf:
            if time.time() > end:
                raise TimeoutError("waiting for %r; got:\n%s" %
                                   (marker, self.buf.decode(errors="replace")))
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU exited:\n" + self.buf.decode(errors="replace"))
            chunk = self.proc.stdout.read(4096)
            if chunk:
                self.buf += chunk
            else:
                time.sleep(0.05)
        i = self.buf.index(marker) + len(marker)
        out, self.buf = self.buf[:i], self.buf[i:]
        return out.decode(errors="replace")

    def run(self, cmd, timeout):
        self.proc.stdin.write((cmd + "\n").encode())
        self.proc.stdin.flush()
        return self.read_until(b"\n$ ", timeout)

    def close(self):
        self.proc.terminate()
        self.proc.wait()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-w", "--workloads", type=int, nargs="+", default=[1, 2, 3, 4, 5])
    ap.add_argument("-s", "--schedulers", type=int, nargs="+", default=[0, 1, 2])
    ap.add_argument("-o", "--out", default="results.csv")
    ap.add_argument("--jobs-out", default=None,
                    help="also write per-job rows to this CSV file")
    ap.add_argument("--timeout", type=int, default=600,
                    help="seconds allowed per wl run")
    ap.add_argument("make_args", nargs="*",
                    help="extra make variables, e.g. ICOUNT=off POLICY=policy")
    args = ap.parse_args()

    subprocess.run(["make", "-s", "kernel/kernel", "fs.img"] + args.make_args,
                   check=True)
    summaries, jobs = [], []
    vm = Xv6(args.make_args)
    try:
        for w in args.workloads:
            for s in args.schedulers:
                t0 = time.time()
                out = vm.run("wl %d %d" % (w, s), args.timeout)
                secs = time.time() - t0
                got = False
                for line in out.splitlines():
                    parts = line.strip().split(",")
                    if parts[0] != "CSV":
                        continue
                    if parts[1] == "summary":
                        r = dict(zip(SUMMARY_FIELDS, map(int, parts[2:])))
                        # wl prints fixed-point integers; convert for the CSV
                        for k in ("turnaround", "response", "overhead_pct"):
                            r[k] = r[k] / 100
                        r["fairness"] = "" if r["fairness"] < 0 else r["fairness"] / 1000
                        summaries.append(r)
                        got = True
                    elif parts[1] == "job":
                        row = dict(zip(JOB_FIELDS, map(int, parts[2:])))
                        jobs.append({"workload": w, "scheduler": s, **row})
                if not got:
                    print(out)
                    print("wl %d %d: no results" % (w, s), file=sys.stderr)
                    continue
                r = summaries[-1]
                fair = "n/a" if r["fairness"] == "" else "%.3f" % r["fairness"]
                print("workload %d scheduler %d: turnaround %.2f, response %.2f "
                      "(worst %d), fairness %s, overhead %.2f%%  [%.1fs]" % (
                          w, s, r["turnaround"], r["response"],
                          r["response_worst"], fair, r["overhead_pct"], secs))
    finally:
        vm.close()

    with open(args.out, "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
        wr.writeheader()
        wr.writerows(summaries)
    if args.jobs_out:
        with open(args.jobs_out, "w", newline="") as f:
            wr = csv.DictWriter(f, fieldnames=["workload", "scheduler"] + JOB_FIELDS)
            wr.writeheader()
            wr.writerows(jobs)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
