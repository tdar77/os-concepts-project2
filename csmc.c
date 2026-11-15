#include "logger.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* This starter deliberately DOES NOT implement the concurrency assignment. */

/* Accepts decimal digits only, yielding an integer in [1, INT_MAX]. */
static int parse_positive_int(const char *text, int *result)
{
    if (text == NULL || *text == '\0') {
        return -1;
    }

    int value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        const int digit = (int)(*p - '0');
        if (value > (INT_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }
    if (value == 0) {
        return -1;
    }
    *result = value;
    return 0;
}

/*
 * TODO: Implement the simulation.
 *
 * Students must design and implement:
 *  - student, coordinator, and tutor thread routines;
 *  - waiting-area and priority-queue data structures;
 *  - all mutex/semaphore synchronization and event logging;
 *  - worker-thread creation, shutdown, and joining.
 *
 * Return 0 ONLY after every worker thread has terminated and been joined.
 * Return nonzero if the simulation cannot complete successfully.
 *
 * The function name/organization is a starting point, not a required design.
 */
static int run_simulation(int num_students, int num_tutors,
                          int num_chairs, int num_help)
{
    (void)num_students;
    (void)num_tutors;
    (void)num_chairs;
    (void)num_help;

    fprintf(stderr, "Project 2 simulation is not yet implemented.\n");
    return -1;
}

int main(int argc, char *argv[])
{
    int num_students, num_tutors, num_chairs, num_help;

    if (argc != 5 ||
        parse_positive_int(argc > 1 ? argv[1] : NULL, &num_students) != 0 ||
        parse_positive_int(argc > 2 ? argv[2] : NULL, &num_tutors) != 0 ||
        parse_positive_int(argc > 3 ? argv[3] : NULL, &num_chairs) != 0 ||
        parse_positive_int(argc > 4 ? argv[4] : NULL, &num_help) != 0) {
        fprintf(stderr,
                "Usage: %s <students> <tutors> <chairs> <help>\n"
                "All four arguments must be positive integers.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (init_log(num_students, num_help) != 0) {
        perror("init_log");
        return EXIT_FAILURE;
    }

    /* TODO: Replace run_simulation() with your thread implementation. */
    if (run_simulation(num_students, num_tutors, num_chairs, num_help) != 0) {
        destroy_log();
        return EXIT_FAILURE;
    }

    /* Do not call until student, tutor, and coordinator threads are joined. */
    print_log();
    destroy_log();
    return EXIT_SUCCESS;
}
