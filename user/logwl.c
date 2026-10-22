// Project 1C: run a workload with logging on.
//
//   logwl <workload-id> [scheduler-id]
//
// Turns logging on, runs `wl <workload-id> [scheduler-id]`, and turns
// logging off when it finishes. Use it to collect rule-check evidence
// from a workload run: for example, `logwl 5 3` is the same as
// `wl 5 3` with the kernel's "running <pid> at <tick>" lines added.
//
// Printing log lines takes time, so results from a logged run can
// differ slightly from an unlogged run. Take measurements with wl.

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *args[4];
  int i, pid;

  if (argc < 2 || argc > 3) {
    printf("usage: logwl workload-id [scheduler-id]\n");
    exit(1);
  }

  args[0] = "wl";
  for (i = 1; i < argc; i++)
    args[i] = argv[i];
  args[argc] = 0;

  startLogging();
  // Fork right away, so the log shows as little as possible besides
  // the workload.
  pid = fork();
  if (pid < 0) {
    stopLogging();
    printf("logwl: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    exec("wl", args);
    printf("logwl: exec wl failed\n");
    exit(1);
  }
  wait(0);
  stopLogging();
  exit(0);
}
