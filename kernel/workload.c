// Project 1C workload harness.
//
// Runs the synthetic workloads defined in workloads.c and measures how
// the current scheduler treats them. Work is counted in ticks credited
// by the kernel, not in loop iterations, so results do not depend on
// how fast the host machine is.
//
// Do not modify. This file is replaced with the official copy at
// grading. The harness assumes a single CPU (CPUS=1).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "workload.h"

extern struct wl_def workloads[];
extern int nworkloads;

enum wl_phase { WL_WAIT, WL_CPU, WL_IO, WL_DONE };

// Kernel-side state of one synthetic process.
struct wl_state {
  struct wl_slot def;
  struct proc *p;       // attached worker, or 0
  enum wl_phase phase;
  int arrive_at;        // absolute tick of arrival
  int io_until;         // absolute tick simulated I/O completes
  int bursts_left;      // CPU bursts still to run, including this one
  int remaining;        // units of CPU left in the current burst
  int debt;             // switch cost not yet paid, in units
  int burst_done;       // current burst finished; block at next chance
  int ready_at;         // absolute tick the job last became ready, or -1
  struct wl_stats st;   // measurements, times relative to start
};

static struct spinlock wl_lock;
static int loaded;              // a workload is loaded
static int started;             // workload_start() has been called
static struct proc *controller; // process that loaded the workload
static struct wl_def *cur;      // the loaded workload
static int nslots;
static int ndone;
static int start_tick;
static struct proc *last_proc;  // last process dispatched
static struct wl_state ws[WL_MAXSLOTS];

// whole-run measurements
static int end_tick, busy, idle, other, fair_start, fair_end;

void
wlinit(void)
{
  initlock(&wl_lock, "workload");
}

static int
isbatch(struct wl_state *s)
{
  return s->def.io_burst == 0 && s->def.repeats == 1;
}

// Mark slot s ready to run a new CPU burst at tick now.
// Caller holds wl_lock.
static void
make_ready(struct wl_state *s, int now)
{
  s->phase = WL_CPU;
  s->remaining = s->def.cpu_burst * TICK_UNITS;
  s->ready_at = now;
  wakeup(s);
}

// Process arrivals and I/O completions due at tick now, and open the
// fairness window once every batch job is present. If a batch job
// finishes before the last one arrives, the window never opens.
// Caller holds wl_lock.
static void
events(int now)
{
  int i, nbatch = 0, arrived = 0, finished = 0;

  for (i = 0; i < nslots; i++) {
    struct wl_state *s = &ws[i];
    if (s->phase == WL_WAIT && now >= s->arrive_at) {
      s->st.arrival = now - start_tick;
      make_ready(s, now);
    } else if (s->phase == WL_IO && now >= s->io_until) {
      make_ready(s, now);
    }
    if (isbatch(s)) {
      nbatch++;
      if (s->phase != WL_WAIT)
        arrived++;
      if (s->phase == WL_DONE)
        finished++;
    }
  }
  if (fair_start < 0 && nbatch > 1 && arrived == nbatch && finished == 0)
    fair_start = now;
}

// Finish the current CPU burst of slot s at tick now: start simulated
// I/O, or complete the job. Caller holds wl_lock.
static void
advance(struct wl_state *s, int now)
{
  s->burst_done = 0;
  s->debt = 0;
  s->bursts_left--;
  if (s->bursts_left > 0) {
    s->phase = WL_IO;
    s->io_until = now + s->def.io_burst;
    return;
  }
  s->phase = WL_DONE;
  s->st.completion = now - start_tick;
  if (isbatch(s) && fair_start >= 0 && fair_end < 0)
    fair_end = now;
  if (++ndone == nslots)
    end_tick = now;
}

// Called by clockintr() on every tick, with tickslock held.
// p is the process that was running during the tick that just ended.
void
wl_clock(struct proc *p)
{
  int now = ticks;

  acquire(&wl_lock);
  if (!loaded || !started || end_tick >= 0) {
    release(&wl_lock);
    return;
  }

  // Credit the tick that just ended.
  if (p == 0) {
    idle++;
  } else if (p->wl_slot < 0) {
    other++;
  } else {
    struct wl_state *s = &ws[p->wl_slot];
    busy++;
    if (s->phase == WL_CPU && !s->burst_done) {
      int units = TICK_UNITS;
      int d = s->debt < units ? s->debt : units;
      s->debt -= d;
      s->st.ovh_units += d;
      units -= d;
      if (units > s->remaining)
        units = s->remaining; // the burst ended partway through the tick
      s->st.cpu_units += units;
      s->remaining -= units;
      if (isbatch(s) && fair_start >= 0 && fair_end < 0)
        s->st.fair_units += units;
      if (s->remaining <= 0)
        s->burst_done = 1;
    }
  }

  events(now);
  release(&wl_lock);
}

// Called by the scheduler just before it switches to p, with
// interrupts off. Charges the switch cost and records response time.
// Does not take wl_lock: with one CPU and interrupts off, nothing
// else can touch the workload state.
void
wl_dispatch(struct proc *p)
{
  int switched = (p != last_proc);

  last_proc = p;
  if (!loaded || !started || p->wl_slot < 0)
    return;

  struct wl_state *s = &ws[p->wl_slot];
  s->st.dispatches++;
  if (switched && s->phase == WL_CPU)
    s->debt += SWITCH_COST;
  if (s->ready_at >= 0) {
    int lat = ticks - s->ready_at;
    if (s->st.first_run < 0)
      s->st.first_run = ticks - start_tick;
    s->st.resp_count++;
    s->st.resp_sum += lat;
    if (lat > s->st.resp_max)
      s->st.resp_max = lat;
    s->ready_at = -1;
  }
}

// Has worker p used up its current CPU burst?
int
wl_burst_done(struct proc *p)
{
  return loaded && p->wl_slot >= 0 && ws[p->wl_slot].burst_done;
}

// Is a workload loaded? set_scheduler() is refused while one is.
int
wl_loaded(void)
{
  return loaded;
}

// Clear all workload state and release any workers.
// Caller holds wl_lock.
static void
clear(void)
{
  for (int i = 0; i < nslots; i++) {
    if (ws[i].p) {
      ws[i].p->wl_slot = -1;
      wakeup(&ws[i]);
    }
    ws[i].p = 0;
  }
  loaded = 0;
  started = 0;
  controller = 0;
  cur = 0;
  nslots = 0;
}

int
wl_load(int id)
{
  int i;
  struct wl_def *d = 0;

  for (i = 0; i < nworkloads; i++)
    if (workloads[i].id == id)
      d = &workloads[i];

  acquire(&wl_lock);
  if (d == 0 || loaded || d->nslots < 1 || d->nslots > WL_MAXSLOTS) {
    release(&wl_lock);
    return -1;
  }
  cur = d;
  nslots = d->nslots;
  ndone = 0;
  controller = myproc();
  end_tick = fair_start = fair_end = -1;
  busy = idle = other = 0;
  for (i = 0; i < nslots; i++) {
    struct wl_state *s = &ws[i];
    memset(s, 0, sizeof(*s));
    s->def = d->slots[i];
    s->phase = WL_WAIT;
    s->bursts_left = s->def.repeats;
    s->ready_at = -1;
    s->st.nice = s->def.nice;
    s->st.cpu_burst = s->def.cpu_burst;
    s->st.io_burst = s->def.io_burst;
    s->st.repeats = s->def.repeats;
    s->st.batch = isbatch(s);
    s->st.arrival = s->st.first_run = s->st.completion = -1;
  }
  loaded = 1;
  release(&wl_lock);
  return 0;
}

int
wl_count(void)
{
  int n;

  acquire(&wl_lock);
  n = (loaded && myproc() == controller) ? nslots : -1;
  release(&wl_lock);
  return n;
}

int
wl_attach(int index)
{
  struct proc *p = myproc();

  acquire(&wl_lock);
  if (!loaded || started || p == controller || p->wl_slot >= 0 ||
      index < 0 || index >= nslots || ws[index].p != 0) {
    release(&wl_lock);
    return -1;
  }
  ws[index].p = p;
  p->wl_slot = index;
  release(&wl_lock);

  setnice(p, ws[index].def.nice);
  return 0;
}

int
wl_start(void)
{
  acquire(&wl_lock);
  if (!loaded || started || myproc() != controller) {
    release(&wl_lock);
    return -1;
  }
  // Every worker must be attached and blocked in workload_checkpoint(),
  // so no worker is runnable before its arrival time.
  for (int i = 0; i < nslots; i++) {
    struct proc *p = ws[i].p;
    int asleep = 0;
    if (p) {
      acquire(&p->lock);
      asleep = (p->state == SLEEPING);
      release(&p->lock);
    }
    if (!asleep) {
      release(&wl_lock);
      return -1;
    }
  }
  // Policy state built up while the workers were forked and attached
  // depends on timing, so reset it; then every run starts the same.
  sched_reset(sched_current);
  started = 1;
  start_tick = ticks;
  last_proc = 0;
  for (int i = 0; i < nslots; i++)
    ws[i].arrive_at = start_tick + ws[i].def.arrival;
  events(start_tick);
  release(&wl_lock);
  return 0;
}

// Called repeatedly by a worker. Blocks while the job has not arrived
// or is in simulated I/O. Returns 0 while the job needs the CPU,
// 1 when the job is finished, and -1 on error.
int
wl_checkpoint(void)
{
  struct proc *p = myproc();

  acquire(&wl_lock);
  for (;;) {
    if (!loaded || p->wl_slot < 0 || killed(p)) {
      release(&wl_lock);
      return -1;
    }
    struct wl_state *s = &ws[p->wl_slot];
    if (s->burst_done)
      advance(s, ticks);
    if (s->phase == WL_DONE) {
      release(&wl_lock);
      return 1;
    }
    if (s->phase == WL_CPU) {
      release(&wl_lock);
      return 0;
    }
    // Not arrived yet, or in simulated I/O: block until the clock
    // wakes this slot.
    sleep_prepare(s);
    release(&wl_lock);
    sleep();
    acquire(&wl_lock);
  }
}

int
wl_stop(void)
{
  acquire(&wl_lock);
  if (!loaded || myproc() != controller) {
    release(&wl_lock);
    return -1;
  }
  clear();
  release(&wl_lock);
  return 0;
}

int
wl_stats(int index, uint64 addr)
{
  struct wl_stats st;
  struct proc *p = myproc();

  acquire(&wl_lock);
  if (!loaded || p != controller || index < 0 || index >= nslots) {
    release(&wl_lock);
    return -1;
  }
  st = ws[index].st;
  st.sched_id = sched_current;
  st.end = end_tick >= 0 ? end_tick - start_tick : -1;
  st.busy_ticks = busy;
  st.idle_ticks = idle;
  st.other_ticks = other;
  st.switch_cost = SWITCH_COST;
  st.fair_start = fair_start >= 0 ? fair_start - start_tick : -1;
  st.fair_end = fair_end >= 0 ? fair_end - start_tick : -1;
  release(&wl_lock);

  if (copyout(p->pagetable, p->sz, addr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

// Called by kexit(). A controller that exits stops its workload;
// a worker that exits is detached from its slot.
void
wl_exit(struct proc *p)
{
  acquire(&wl_lock);
  if (loaded && p == controller) {
    clear();
  } else if (p->wl_slot >= 0) {
    ws[p->wl_slot].p = 0;
    p->wl_slot = -1;
  }
  release(&wl_lock);
}
