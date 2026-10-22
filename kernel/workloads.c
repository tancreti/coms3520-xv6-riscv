// Project 1C predefined workloads.
// The course staff replace this file with hidden workloads at grading,
// so a scheduler must not depend on these exact definitions.
// You may add your own workloads below with new IDs.
//
// Times are in ticks (1 tick = 10 ms, see TICK_INTERVAL in param.h).
// Slot fields: { arrival, nice, cpu_burst, io_burst, repeats }
//
// Nonzero arrival times and I/O lengths are not multiples of 2, 3 or
// 5, so they do not line up with quantum boundaries. A job that arrives exactly when a quantum
// expires is dispatched at once, which hides the scheduler's real
// response time. Check new workloads with tools/check_workloads.py.

#include "types.h"
#include "workload.h"

struct wl_def workloads[] = {
  // 1: batch jobs of different lengths with staggered arrivals; each
  // later job is shorter. Turnaround depends heavily on the order jobs
  // are run, and response on how long a new job waits for a quantum.
  { 1, "batch", 5, {
    {  0, 0, 400, 0, 1 },
    { 11, 0, 300, 0, 1 },
    { 29, 0, 200, 0, 1 },
    { 31, 0, 100, 0, 1 },
    { 43, 0,  50, 0, 1 },
  } },

  // 2: interactive jobs (short bursts, long I/O) competing with
  // CPU-bound batch jobs. Response time versus turnaround.
  { 2, "interactive", 6, {
    {  0, 0, 300,  0,  1 },
    {  0, 0, 300,  0,  1 },
    {  0, 0, 300,  0,  1 },
    {  7, 0,   1, 11, 30 },
    { 19, 0,   2, 17, 20 },
    { 31, 0,   1, 23, 20 },
  } },

  // 3: batch jobs with different nice values plus one high-priority
  // interactive job. Fairness is measured against nice-weighted shares.
  // The nice values span the whole range a scheduler might group by:
  // -10 and below, -9 to 10, and 11 and above.
  { 3, "nice", 5, {
    {  0, -10, 300,  0,  1 },
    {  0,  -5, 300,  0,  1 },
    {  0,   5, 300,  0,  1 },
    {  0,  11, 300,  0,  1 },
    { 13, -10,   1, 11, 30 },
  } },

  // 4: short jobs arriving one at a time while two long jobs run.
  // Most jobs are short, so a scheduler that finishes new, short jobs
  // first (without knowing their lengths) gets a much lower mean
  // turnaround. Short jobs finish before the last one arrives, so
  // fairness is not measured.
  { 4, "short", 12, {
    {   0, 0, 360, 0, 1 },
    {   0, 0, 300, 0, 1 },
    {  17, 0,   8, 0, 1 },
    {  47, 0,  25, 0, 1 },
    {  89, 0,  12, 0, 1 },
    { 131, 0,  40, 0, 1 },
    { 173, 0,   6, 0, 1 },
    { 229, 0,  30, 0, 1 },
    { 271, 0,  15, 0, 1 },
    { 331, 0,  50, 0, 1 },
    { 397, 0,  10, 0, 1 },
    { 457, 0,  20, 0, 1 },
  } },

  // 5: I/O-bound jobs that keep Q1 busy while two CPU-bound jobs wait.
  // Each I/O-bound job uses about a tick of CPU, then waits a tick for
  // I/O. The batch jobs have nice 11, so an MLFQ starts them in Q0, and
  // they arrive after the I/O-bound jobs. Under an MLFQ without an
  // allotment or boost, they starve until the I/O-bound jobs finish;
  // the allotment and the priority boost fix that.
  { 5, "starve", 6, {
    {  7, 11, 150, 0,   1 },
    { 11, 11, 150, 0,   1 },
    {  0,  0,   1, 1, 149 },
    {  0,  0,   1, 1, 149 },
    {  0,  0,   1, 1, 149 },
    {  0,  0,   1, 1, 149 },
  } },
};

int nworkloads = sizeof(workloads) / sizeof(workloads[0]);
