#include "logger.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

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
static void *student_thread(void *arg)
{
    (void)arg; /* TODO: student loop */
    return NULL;
}

static void *tutor_thread(void *arg)
{
    (void)arg; /* TODO: tutor loop */
    return NULL;
}

static void *coordinator_thread(void *arg)
{
    (void)arg; /* TODO: coordinator loop */
    return NULL;
}

static int run_simulation(int num_students, int num_tutors,
                          int num_chairs, int num_help)
{
    (void)num_chairs;
    (void)num_help;

    int result = 0;
    int students_started = 0;
    int tutors_started = 0;
    int coordinator_started = 0;

    pthread_t *student_threads = malloc(num_students * sizeof(pthread_t));
    pthread_t *tutor_threads = malloc(num_tutors * sizeof(pthread_t));
    int *student_ids = malloc(num_students * sizeof(int));
    int *tutor_ids = malloc(num_tutors * sizeof(int));
    pthread_t coordinator;

    if (!student_threads || !tutor_threads || !student_ids || !tutor_ids) {
        fprintf(stderr, "Memory allocation failed.\n");
        result = -1;
        goto cleanup;
    }

    /* Create threads. IDs start at 1 so they match the log examples. */
    for (int i = 0; i < num_students; i++) {
        student_ids[i] = i + 1;
        if (pthread_create(&student_threads[i], NULL, student_thread, &student_ids[i]) != 0) {
            fprintf(stderr, "Failed to create student thread %d.\n", i + 1);
            result = -1;
            goto join;
        }
        students_started++;
    }
    for (int i = 0; i < num_tutors; i++) {
        tutor_ids[i] = i + 1;
        if (pthread_create(&tutor_threads[i], NULL, tutor_thread, &tutor_ids[i]) != 0) {
            fprintf(stderr, "Failed to create tutor thread %d.\n", i + 1);
            result = -1;
            goto join;
        }
        tutors_started++;
    }
    if (pthread_create(&coordinator, NULL, coordinator_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create coordinator thread.\n");
        result = -1;
        goto join;
    }
    coordinator_started = 1;

join:
    /* Join only the threads that were actually created. */
    /* TODO: if a creation failed, signal shutdown before joining blocked threads. */
    for (int i = 0; i < students_started; i++) {
        pthread_join(student_threads[i], NULL);
    }
    for (int i = 0; i < tutors_started; i++) {
        pthread_join(tutor_threads[i], NULL);
    }
    if (coordinator_started) {
        pthread_join(coordinator, NULL);
    }

cleanup:
    free(student_threads);
    free(tutor_threads);
    free(student_ids);
    free(tutor_ids);
    return result;
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
