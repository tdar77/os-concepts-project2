#ifndef CSMC_LOGGER_H
#define CSMC_LOGGER_H

/*
 * Project 2 event logger.
 *
 * Normal mode: events are buffered; print_log() emits them to stdout.
 * Debug mode: set CSMC_LOG_DEBUG=1 to ALSO print events to stderr
 *             immediately as they are recorded.
 *
 * The logger does not manage chairs, queues, tutors, or student state.
 * All log_event() calls must accurately describe application events.
 */

typedef enum {
    LOG_SEATED,
    LOG_QUEUED,
    LOG_ASSIGNED,
    LOG_STUDENT_DONE,
    LOG_TUTOR_DONE,
    LOG_NO_CHAIR
} EventType;

/* Returns 0 on success, -1 on error (with errno set).
 * Call once before creating any worker threads.
 */
int init_log(int num_students, int num_help);

/*
 * Records a single event. The event's student ID and visit number
 * identify a request; visits are numbered starting at 1.
 *
 * tutor_id is ignored for LOG_SEATED, LOG_QUEUED, and LOG_NO_CHAIR;
 * pass -1 for those events.
 *
 * LOG_NO_CHAIR increases a counter instead of occupying a buffer slot.
 * log_event() terminates the program if the buffer capacity is exceeded.
 *
 * For events that represent changes to shared application state,
 * call log_event() while holding the appropriate application lock,
 * at the point when the state transition occurs.
 */
void log_event(EventType type, int student_id, int tutor_id, int visit);

/* Call once, only after ALL worker threads have terminated and been joined. */
void print_log(void);

/* Call after print_log(), when no worker threads are running. */
void destroy_log(void);

#endif /* CSMC_LOGGER_H */
