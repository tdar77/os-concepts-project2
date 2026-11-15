#include "logger.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <semaphore.h>

/////////////////////////////////////////////////////////////////////////////////
// GLOBAL VARIABLES AND STRUCTURES

// Waiting Queue
typedef struct
{
    int    *data;       
    size_t  capacity;
    size_t  head;       // next index to pop from
    size_t  tail;       // next index to push to
    size_t  count;
} Queue;

int requiredVisits;
static Queue waitingRoom;   // FIFO of seated students (IDs), capacity = chairs
static pthread_mutex_t shopLock;   // protects waitingRoom and the chair check
static sem_t *studentSem;          // studentSem[id - 1]: tutor -> that student
static int *assignedTutor;         // assignedTutor[id - 1]: tutor ID, written before the post

/////////////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS

// Returns 0 on success, -1 on bad capacity or malloc failure
int queue_init(Queue *q, size_t capacity) {
    if (capacity == 0) return -1;
    q->data = malloc(capacity * sizeof *q->data);
    if (q->data == NULL) return -1;
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    return 0;
}

void queue_destroy(Queue *q) {
    free(q->data);
    q->data = NULL;
    q->capacity = q->head = q->tail = q->count = 0;
}

// Returns 0 on success, -1 if full
int queue_push(Queue *q, int value) {
    if (q->count == q->capacity) return -1;
    q->data[q->tail] = value;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return 0;
}

// Returns 0 on success, -1 if empty. Value goes in *out.
int queue_pop(Queue *q, int *out) {
    if (q->count == 0) return -1;
    *out = q->data[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return 0;
}

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

/* Simulates programming: sleeps a random 2-10 ms (thread-safe via rand_r). */
static void program_delay(unsigned int *seed)
{
    usleep(2000 + rand_r(seed) % 8001);
}

/////////////////////////////////////////////////////////////////////////////////
// THREADS

static void *student_thread(void *arg)
{
    int id = *(int *)arg;
    unsigned int seed = (unsigned int)id; // each student has a seed

    for (int visit = 1; visit <= requiredVisits; visit++)
    {
        bool seated = false;

        while (!seated)
        {
            program_delay(&seed); 

            pthread_mutex_lock(&shopLock);

            if (waitingRoom.count < waitingRoom.capacity)
            {
                queue_push(&waitingRoom, id);
                log_event(LOG_SEATED, id, -1, visit);
                seated = true;
            }
            else
            {
                log_event(LOG_NO_CHAIR, id, -1, visit);
            }
            
            pthread_mutex_unlock(&shopLock);
        }  
   
        // Wait until a tutor selects this student (the tutor posts this semaphore).
        // The tutor writes assignedTutor[] before posting, so it is safe to read here.
        sem_wait(&studentSem[id - 1]);

        // Simulated tutoring: 0.2 ms,   independent of the tutor's own sleep.
        usleep(200);
        log_event(LOG_STUDENT_DONE, id, assignedTutor[id - 1], visit);  
    }
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
    int result = 0;
    int students_started = 0;
    int tutors_started = 0;
    int coordinator_started = 0;
    int sems_inited = 0;

    requiredVisits = num_help;

    if (pthread_mutex_init(&shopLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize shop lock.\n");
        return -1;
    }

    /* Waiting room: one slot per chair. Only seated students are in it. */
    if (queue_init(&waitingRoom, (size_t)num_chairs) != 0) {
        fprintf(stderr, "Failed to initialize waiting room.\n");
        pthread_mutex_destroy(&shopLock);
        return -1;
    }

    pthread_t *student_threads = malloc(num_students * sizeof(pthread_t));
    pthread_t *tutor_threads = malloc(num_tutors * sizeof(pthread_t));
    int *student_ids = malloc(num_students * sizeof(int));
    int *tutor_ids = malloc(num_tutors * sizeof(int));
    studentSem = malloc(num_students * sizeof(sem_t));
    assignedTutor = malloc(num_students * sizeof(int));
    pthread_t coordinator;

    if (!student_threads || !tutor_threads || !student_ids || !tutor_ids || !studentSem || !assignedTutor) {
        fprintf(stderr, "Memory allocation failed.\n");
        result = -1;
        goto cleanup;
    }

    for (int i = 0; i < num_students; i++) {
        if (sem_init(&studentSem[i], 0, 0) != 0) {
            fprintf(stderr, "Failed to initialize student semaphore %d.\n", i + 1);
            result = -1;
            goto cleanup;
        }
        sems_inited++;
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
    for (int i = 0; i < sems_inited; i++) {
        sem_destroy(&studentSem[i]);
    }
    free(studentSem);
    studentSem = NULL;
    free(assignedTutor);
    assignedTutor = NULL;
    pthread_mutex_destroy(&shopLock);
    queue_destroy(&waitingRoom);
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
