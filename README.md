# CS 5348 Project 2 — Starter files

This starter contains **no implementation of the concurrent simulation**.
Students must implement the thread functions, queues, synchronization, thread
creation, and orderly thread termination independently.

## Files

- `csmc.c`: `main()` with argument validation and calls to the logger. The
  `run_simulation()` function is deliberately unimplemented.
- `logger.h`: supplied logging API.
- `logger.c`: buffered, thread-safe event recorder.

## Compile

```sh
gcc -std=c11 -Wall -Wextra -Werror -pthread csmc.c logger.c -o csmc
```

## Run

```sh
./csmc 10 3 4 5 > output.log
```

The starter correctly reports that the simulation is not implemented and exits
with an error until the student writes the thread logic.

For live diagnostic output, enable debug mode:

```sh
CSMC_LOG_DEBUG=1 ./csmc 10 3 4 5 2> debug.log
```

The **default is buffered mode**. With debug enabled, events are both stored in
memory and printed immediately to `stderr`. The final buffered event log is
written to `stdout` after all threads terminate. Debugging output changes
thread scheduling and should not be used for normal grading runs.

## Events

Each **successful** visit is identified by `(student ID, visit number)`, with
visit numbers starting at 1.

| Event | Call | Meaning |
| --- | --- | --- |
| `SEATED` | `log_event(LOG_SEATED, student, -1, visit);` | Student occupies a chair and joins the arrival queue |
| `QUEUED` | `log_event(LOG_QUEUED, student, -1, visit);` | Coordinator moves request to the priority queue |
| `ASSIGNED` | `log_event(LOG_ASSIGNED, student, tutor, visit);` | Tutor selects student and frees their waiting-room chair |
| `STUDENT_DONE` | `log_event(LOG_STUDENT_DONE, student, tutor, visit);` | Student's simulated tutoring sleep finishes |
| `TUTOR_DONE` | `log_event(LOG_TUTOR_DONE, student, tutor, visit);` | Tutor's simulated tutoring sleep finishes |
| `NO_CHAIR` | `log_event(LOG_NO_CHAIR, student, -1, visit);` | Unsuccessful seating attempt; counted but not buffered |

An example of the eventual output format (not executable by the unimplemented
starter):

```text
S SEATED student=3 visit=1
C QUEUED student=3 visit=1
T ASSIGNED student=3 tutor=2 visit=1
S STUDENT_DONE student=3 tutor=2 visit=1
T TUTOR_DONE student=3 tutor=2 visit=1
SUMMARY expected=1 seated=1 queued=1 assigned=1 student_done=1 tutor_done=1 no_chair=0
```

## Logging rules

- `init_log(num_students, num_help)` allocates enough records for five events
  per expected successful tutoring session. It rejects invalid sizes and
  allocation failures.
- `LOG_NO_CHAIR` is counted separately; the number of failed attempts is not
  bounded by input parameters.
- Log application state transitions **while holding the lock protecting the
  corresponding state**, at the point the transition occurs. Reserving a log
  position atomically does **not** automatically make state transitions correct
  or enforce their order.
- The logger has no knowledge of chairs, waiting queues, tutor assignment, or
  synchronization correctness. It can report incorrect application events if
  called incorrectly.
- `STUDENT_DONE` and `TUTOR_DONE` need not occur in either particular order;
  the agreed simulation does not synchronize tutoring completion between them.
- All worker threads must be joined before `print_log()`.
- The logger's atomic reservation can still affect thread timing, even when
  debug mode is disabled. Logs help check properties but are not proof of
  race freedom or deadlock freedom.
