// Project 1C workload runner.
//
//   wl <workload-id> [scheduler-id]
//
// Optionally switches to the given scheduler, loads the workload,
// forks one worker per synthetic process, runs the workload to
// completion, and prints per-process results and three ratings:
// turnaround time, response time, and fairness.
// Lines starting with "CSV," are for the host-side runner script.
//
// Do not modify. This file is replaced with the official copy at
// grading.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/workload.h"
#include "user/user.h"

// Linux CFS weights for nice -20 .. 19. A process's fair share of
// the CPU is proportional to its weight; nice 0 has weight 1024.
static const int weight[40] = {
  88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
  9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
  1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
  110,   87,    70,    56,    45,    36,    29,    23,    18,    15,
};

static struct wl_stats st[WL_MAXSLOTS];
static int pids[WL_MAXSLOTS]; // pid of job i's worker, to match log lines

// Print v/100 with two decimal places.
static void
print2(uint64 v)
{
  printf("%d.%d%d", (int)(v / 100), (int)(v / 10 % 10), (int)(v % 10));
}

// Print v/1000 with three decimal places.
static void
print3(uint64 v)
{
  printf("%d.%d%d%d", (int)(v / 1000), (int)(v / 100 % 10),
         (int)(v / 10 % 10), (int)(v % 10));
}

// xv6 printf has no field widths, so the per-job table pads its
// columns with these.
static void
spaces(int n)
{
  while (n-- > 0)
    printf(" ");
}

// Number of characters printf("%d", v) prints.
static int
ndigits(int v)
{
  int n = 1;

  if (v < 0) {
    n++;
    v = -v;
  }
  for (; v >= 10; v /= 10)
    n++;
  return n;
}

// Print v right-aligned in a field of w characters.
static void
printw(int v, int w)
{
  spaces(w - ndigits(v));
  printf("%d", v);
}

// Print v/100 with two decimal places, right-aligned in w characters.
static void
print2w(uint64 v, int w)
{
  spaces(w - ndigits((int)(v / 100)) - 3);
  print2(v);
}

static void
worker(int index)
{
  int r;

  if (workload_attach(index) < 0) {
    printf("wl: worker %d: attach failed\n", index);
    exit(1);
  }
  // Use the CPU until the kernel says this job is finished. The kernel
  // blocks the worker inside workload_checkpoint() during simulated
  // I/O and before the job's arrival time.
  while ((r = workload_checkpoint()) == 0)
    for (volatile int k = 0; k < 1000; k++)
      ;
  exit(r == 1 ? 0 : 1);
}

int
main(int argc, char *argv[])
{
  int id, n, i;

  if (argc < 2 || argc > 3) {
    printf("usage: wl workload-id [scheduler-id]\n");
    exit(1);
  }
  id = atoi(argv[1]);
  if (argc == 3 && set_scheduler(atoi(argv[2])) < 0) {
    printf("wl: set_scheduler(%s) failed\n", argv[2]);
    exit(1);
  }
  if (workload_load(id) < 0) {
    printf("wl: no workload %d\n", id);
    exit(1);
  }
  n = workload_process_count();

  for (i = 0; i < n; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("wl: fork failed\n");
      workload_stop();
      exit(1);
    }
    if (pid == 0)
      worker(i);
    pids[i] = pid;
  }

  // Start once every worker is attached and waiting.
  for (i = 0; workload_start() < 0; i++) {
    if (i == 500) {
      printf("wl: workers never became ready\n");
      workload_stop();
      exit(1);
    }
    pause(1);
  }

  for (i = 0; i < n; i++)
    wait(0);

  for (i = 0; i < n; i++)
    workload_stats(i, &st[i]);
  workload_stop();

  // Per-process results.
  printf("workload %d, scheduler %d, switch cost %d units\n", id,
         st[0].sched_id, st[0].switch_cost);
  // cpu is in ticks; ovh and resp are in ticks with two decimals.
  printf("job  pid nice  kind arrive first  done  turn   cpu    ovh   resp  "
         "rmax\n");
  for (i = 0; i < n; i++) {
    struct wl_stats *s = &st[i];
    int turn = s->completion >= 0 ? s->completion - s->arrival : -1;
    printw(i, 3);
    printw(pids[i], 5);
    printw(s->nice, 5);
    printf(" %s", s->batch ? "batch" : "inter");
    printw(s->arrival, 7);
    printw(s->first_run, 6);
    printw(s->completion, 6);
    printw(turn, 6);
    printw(s->cpu_units / TICK_UNITS, 6);
    print2w(s->ovh_units, 7);
    // Mean response over this job's CPU bursts.
    if (s->resp_count > 0)
      print2w((uint64)s->resp_sum * 100 / s->resp_count, 7);
    else
      printw(-1, 7);
    printw(s->resp_max, 6);
    printf("\n");
    printf("CSV,job,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", i, s->nice,
           s->batch, s->arrival, s->first_run, s->completion, turn,
           s->cpu_units, s->ovh_units, s->resp_count, s->resp_sum,
           s->resp_max);
  }

  // Turnaround: mean (completion - arrival) of batch jobs.
  uint64 tsum = 0, tn = 0;
  // Response: mean (dispatch - ready) over every CPU burst of every
  // job. A burst becomes ready at arrival or when its I/O finishes.
  uint64 rsum = 0, rn = 0, rmax = 0;
  // Fairness: Jain's index over batch jobs' CPU inside the window
  // when all batch jobs are present, each divided by its weight.
  uint64 xsum = 0, x2sum = 0, xn = 0;
  uint64 ovh = 0;

  for (i = 0; i < n; i++) {
    struct wl_stats *s = &st[i];
    ovh += s->ovh_units;
    rsum += s->resp_sum;
    rn += s->resp_count;
    if (s->resp_max > rmax)
      rmax = s->resp_max;
    if (s->batch) {
      tsum += s->completion - s->arrival;
      tn++;
      uint64 x = (uint64)s->fair_units * 1024 / weight[s->nice + 20];
      xsum += x;
      x2sum += x * x;
      xn++;
    }
  }

  uint64 turn = tn ? tsum * 100 / tn : 0;
  uint64 resp = rn ? rsum * 100 / rn : 0;
  int havefair = st[0].fair_start >= 0 && st[0].fair_end >= 0 && x2sum;
  uint64 fair = havefair ? xsum * xsum * 1000 / (xn * x2sum) : 0;
  uint64 ovhpct = st[0].busy_ticks ? ovh * 100 / st[0].busy_ticks : 0;

  printf("turnaround (mean, batch jobs): ");
  print2(turn);
  printf(" ticks\nresponse (mean, all CPU bursts): ");
  print2(resp);
  printf(" ticks, worst %d\nfairness (nice-weighted Jain): ", (int)rmax);
  if (havefair) {
    print3(fair);
    printf(" over ticks %d-%d\n", st[0].fair_start, st[0].fair_end);
  } else {
    printf("n/a\n");
  }
  printf("switch overhead: ");
  print2(ovhpct);
  printf("%% of busy time; total %d ticks, idle %d, other %d\n", st[0].end,
         st[0].idle_ticks, st[0].other_ticks);
  printf("CSV,summary,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", id, st[0].sched_id,
         st[0].switch_cost, (int)turn, (int)resp, (int)rmax,
         havefair ? (int)fair : -1, (int)ovhpct, st[0].end,
         st[0].other_ticks);
  exit(0);
}
