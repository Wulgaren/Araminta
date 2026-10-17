/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The ThreadSanitizer canary (tests/run_tests.sh runs it with the ThreadSanitizer units): a race
 * the runtime has to report, so a quiet ThreadSanitizer run means "no races", not "nobody looked".
 * A runtime that never reports a race fails the unit instead of passing as clean; it cannot vouch
 * for every race shape — Apple's TSAN reported a plain race like this one and still missed the
 * SEND clock race of the second core (slicer.c's sl_bus_blk write from the driver's core, both
 * sides instrumented) that GCC's and clang's on Linux caught.
 *   build/host/tsan_canary [parent|fork]
 *   parent: two threads race over one cell, no sync between them (the default)
 *   fork:   the same race in a forked child, the way the suite's renders run
 * Built and run with the same flags and TSAN_OPTIONS as tests/dualcore_test.c: under a working
 * runtime the process dies on the report (halt_on_error=1) before its "survived" line. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int canary_cell;    /* written and read by both threads: the race to be reported */

static void *canary_thread(void *arg) {
    int id = (int)(long)arg, sink = 0, i;
    for (i = 0; i < 1000; i++) {
        canary_cell = id + i;       /* the write */
        sink += canary_cell;        /* the read; both unordered with the other thread's */
    }
    return (void *)(long)sink;
}

static void canary_race(void) {
    pthread_t a, b;
    void *ra, *rb;
    pthread_create(&a, NULL, canary_thread, (void *)1L);
    pthread_create(&b, NULL, canary_thread, (void *)2L);
    pthread_join(a, &ra);
    pthread_join(b, &rb);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "fork") == 0) {
        int st = 0;
        pid_t pid;
        fputs("canary: a forked child races two threads over one cell\n", stderr);
        pid = fork();
        if (pid == 0) {
            canary_race();
            fputs("canary: the child survived the race (no ThreadSanitizer report)\n", stderr);
            _exit(0);
        }
        waitpid(pid, &st, 0);
        if (WIFEXITED(st))
            fprintf(stderr, "canary: the child exited %d\n", WEXITSTATUS(st));
        else
            fprintf(stderr, "canary: the child died on signal %d\n", WTERMSIG(st));
        return 0;
    }
    fputs("canary: two threads race over one cell\n", stderr);
    canary_race();
    fputs("canary: survived the race (no ThreadSanitizer report)\n", stderr);
    return 0;
}
