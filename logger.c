#include "logger.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORDED_EVENTS_PER_SESSION 5U

typedef struct {
    EventType type;
    int student_id;
    int tutor_id;
    int visit;
} EventRecord;

typedef struct {
    EventRecord *records;
    size_t capacity;
    size_t expected_sessions;
    atomic_size_t next_index;
    atomic_uint_fast64_t no_chair_count;
    bool debug_enabled;
} LogState;

/* Initialized and destroyed only by the main thread. */
static LogState log_state;

static void logger_fatal(const char *message)
{
    fprintf(stderr, "logger: %s\n", message);
    abort();
}

int init_log(int num_students, int num_help)
{
    if (log_state.records != NULL) {
        errno = EALREADY;
        return -1;
    }
    if (num_students <= 0 || num_help <= 0) {
        errno = EINVAL;
        return -1;
    }

    const size_t students = (size_t)num_students;
    const size_t helps = (size_t)num_help;
    if (students > SIZE_MAX / helps) {
        errno = EOVERFLOW;
        return -1;
    }

    const size_t sessions = students * helps;
    if (sessions > SIZE_MAX / RECORDED_EVENTS_PER_SESSION) {
        errno = EOVERFLOW;
        return -1;
    }
    const size_t capacity = sessions * RECORDED_EVENTS_PER_SESSION;
    if (capacity > SIZE_MAX / sizeof(EventRecord)) {
        errno = EOVERFLOW;
        return -1;
    }

    EventRecord *records = calloc(capacity, sizeof(*records));
    if (records == NULL) {
        errno = ENOMEM;
        return -1;
    }

    const char *debug = getenv("CSMC_LOG_DEBUG");
    log_state.records = records;
    log_state.capacity = capacity;
    log_state.expected_sessions = sessions;
    atomic_init(&log_state.next_index, 0);
    atomic_init(&log_state.no_chair_count, 0);
    log_state.debug_enabled = (debug != NULL && strcmp(debug, "1") == 0);
    return 0;
}

/* One fprintf() per event keeps individual output lines intact. */
static void write_event(FILE *stream, const EventRecord *event)
{
    switch (event->type) {
    case LOG_SEATED:
        fprintf(stream, "S SEATED student=%d visit=%d\n",
                event->student_id, event->visit);
        break;
    case LOG_QUEUED:
        fprintf(stream, "C QUEUED student=%d visit=%d\n",
                event->student_id, event->visit);
        break;
    case LOG_ASSIGNED:
        fprintf(stream, "T ASSIGNED student=%d tutor=%d visit=%d\n",
                event->student_id, event->tutor_id, event->visit);
        break;
    case LOG_STUDENT_DONE:
        fprintf(stream, "S STUDENT_DONE student=%d tutor=%d visit=%d\n",
                event->student_id, event->tutor_id, event->visit);
        break;
    case LOG_TUTOR_DONE:
        fprintf(stream, "T TUTOR_DONE student=%d tutor=%d visit=%d\n",
                event->student_id, event->tutor_id, event->visit);
        break;
    case LOG_NO_CHAIR:
        fprintf(stream, "S NO_CHAIR student=%d visit=%d\n",
                event->student_id, event->visit);
        break;
    default:
        logger_fatal("invalid event type");
    }
}

void log_event(EventType type, int student_id, int tutor_id, int visit)
{
    if (log_state.records == NULL) {
        logger_fatal("log_event() called before init_log() or after destroy_log()");
    }

    if (type < LOG_SEATED || type > LOG_NO_CHAIR) {
        logger_fatal("invalid event type");
    }

    const EventRecord event = {type, student_id, tutor_id, visit};

    if (type == LOG_NO_CHAIR) {
        atomic_fetch_add_explicit(&log_state.no_chair_count, 1,
                                  memory_order_relaxed);
    } else {
        /* No output or application-level synchronization in normal mode. */
        const size_t index = atomic_fetch_add_explicit(
            &log_state.next_index, 1, memory_order_relaxed);
        if (index >= log_state.capacity) {
            logger_fatal("event buffer full (too many events recorded)");
        }
        log_state.records[index] = event;
    }

    if (log_state.debug_enabled) {
        /* Stdio serializes each fprintf() call, but debug output changes timing. */
        write_event(stderr, &event);
        fflush(stderr);
    }
}

void print_log(void)
{
    if (log_state.records == NULL) {
        logger_fatal("print_log() called before init_log()");
    }

    const size_t count = atomic_load_explicit(&log_state.next_index,
                                               memory_order_relaxed);
    if (count > log_state.capacity) {
        logger_fatal("event buffer overflow");
    }

    size_t seated = 0;
    size_t queued = 0;
    size_t assigned = 0;
    size_t student_done = 0;
    size_t tutor_done = 0;

    /* All worker threads MUST have been joined before reading records. */
    for (size_t i = 0; i < count; ++i) {
        const EventRecord *event = &log_state.records[i];
        write_event(stdout, event);
        switch (event->type) {
        case LOG_SEATED:       ++seated; break;
        case LOG_QUEUED:       ++queued; break;
        case LOG_ASSIGNED:     ++assigned; break;
        case LOG_STUDENT_DONE: ++student_done; break;
        case LOG_TUTOR_DONE:   ++tutor_done; break;
        case LOG_NO_CHAIR:
        default:
            logger_fatal("unrecognized buffered event");
        }
    }

    const uint_fast64_t no_chair = atomic_load_explicit(
        &log_state.no_chair_count, memory_order_relaxed);
    printf("SUMMARY expected=%zu seated=%zu queued=%zu assigned=%zu "
           "student_done=%zu tutor_done=%zu no_chair=%" PRIuFAST64 "\n",
           log_state.expected_sessions, seated, queued, assigned,
           student_done, tutor_done, no_chair);
}

void destroy_log(void)
{
    /* Precondition: no worker threads are running. */
    free(log_state.records);
    log_state.records = NULL;
    log_state.capacity = 0;
    log_state.expected_sessions = 0;
    log_state.debug_enabled = false;
    atomic_store_explicit(&log_state.next_index, 0, memory_order_relaxed);
    atomic_store_explicit(&log_state.no_chair_count, 0, memory_order_relaxed);
}
