// Project 1C workload harness: definitions shared by the kernel
// (workload.c, workloads.c) and the user program wl.c.
// Do not modify. This file is replaced with the official copy at grading.

#define WL_MAXSLOTS 16 // maximum synthetic processes in one workload

// One synthetic process. Its job is: after arriving, repeat `repeats`
// times { run on the CPU for cpu_burst ticks; then, except after the
// last burst, block for simulated I/O for io_burst ticks }.
// A slot with io_burst == 0 and repeats == 1 is a batch job.
struct wl_slot {
  int arrival;   // ticks after workload_start() that the job arrives
  int nice;      // nice value applied when the worker attaches
  int cpu_burst; // ticks of CPU needed per burst
  int io_burst;  // ticks of simulated I/O between bursts
  int repeats;   // number of CPU bursts
};

struct wl_def {
  int id;
  char *name;
  int nslots;
  struct wl_slot slots[WL_MAXSLOTS];
};

// Results for one slot, returned by workload_stats(). All times are
// in ticks unless the name says units (1 tick = TICK_UNITS units).
struct wl_stats {
  // the slot's definition
  int nice;
  int cpu_burst;
  int io_burst;
  int repeats;
  int batch; // 1 if this is a batch job (no I/O)

  // per-slot measurements
  int arrival;     // tick the job arrived (relative to start)
  int first_run;   // tick of first dispatch after arrival (-1 if never)
  int completion;  // tick the job finished (-1 if not finished)
  int cpu_units;   // useful CPU received
  int ovh_units;   // CPU lost to switch cost
  int fair_units;  // useful CPU received inside the fairness window
  int dispatches;  // times the scheduler dispatched this worker
  int resp_count;  // number of times the job became ready
  int resp_sum;    // total ticks from becoming ready to being dispatched
  int resp_max;    // worst ticks from becoming ready to being dispatched

  // whole-run measurements (same in every slot's stats)
  int sched_id;      // scheduler that ran the workload
  int end;           // tick the last job finished (-1 if not finished)
  int busy_ticks;    // ticks a worker was running
  int idle_ticks;    // ticks no process was running
  int other_ticks;   // ticks a non-worker process was running
  int switch_cost;   // units charged per switch
  int fair_start;    // first tick of the fairness window (-1 if none)
  int fair_end;      // last tick of the fairness window (-1 if none)
};
