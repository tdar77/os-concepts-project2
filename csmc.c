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

// One node per student. A student is in at most one priority list at a time,
// so each student's node is reused for every visit (intrusive link).
typedef struct PriorityNode
{
    int studentId;
    int visit;                      // 1-based visit number; priority = visit (1 is highest)
    struct PriorityNode *next;
} PriorityNode;

// One FIFO list per priority level.
typedef struct
{
    PriorityNode *head;             // oldest entry; tutors remove from here
    PriorityNode *tail;             // newest entry; coordinator appends here
} PriorityList;

int requiredVisits;
static Queue waitingRoom;   // FIFO of seated students (IDs), capacity = chairs
static pthread_mutex_t shopLock;   // protects waitingRoom and the chair check
static sem_t *studentSem;          // studentSem[id - 1]: tutor -> that student
static int *assignedTutor;         // assignedTutor[id - 1]: tutor ID, written before the post
static sem_t arrivals;             // student -> coordinator: one post per seated student
static PriorityList *priorityLists; // priorityLists[visit - 1]: one list per level (H levels)
static PriorityNode *priorityNodes; // priorityNodes[id - 1]: the node for each student
static int *completedVisits;        // completedVisits[id - 1]: finished tutoring sessions
static long totalVisits;            // num_students * num_help: total arrivals the coordinator serves
static sem_t work;                  // coordinator -> tutors: one post per queued student, plus one per tutor at shutdown
static int freeChairs;              // chairs not held by a student; freed by the tutor at selection
static int numTutors;
static int numStudents;
static bool shuttingDown;           // set only when a thread fails to start; every thread checks it and exits

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

// Appends a node to the back of its level (visit - 1). Caller holds shopLock.
void priority_append(PriorityNode *node)
{
    PriorityList *level = &priorityLists[node->visit - 1];
    node->next = NULL;
    if (level->tail == NULL) {
        level->head = level->tail = node;
    } else {
        level->tail->next = node;
        level->tail = node;
    }
}

// Removes the oldest node from the highest-priority non-empty level.
// Returns NULL if every level is empty. Caller holds shopLock.
PriorityNode *priority_take_best(void)
{
    for (int v = 0; v < requiredVisits; v++) {
        PriorityList *level = &priorityLists[v];
        if (level->head != NULL) {
            PriorityNode *node = level->head;
            level->head = node->next;
            if (level->head == NULL) {
                level->tail = NULL;
            }
            node->next = NULL;
            return node;
        }
    }
    return NULL;
}

// Reads the shutdown flag under shopLock.
static bool is_shutting_down(void)
{
    pthread_mutex_lock(&shopLock);
    bool flag = shuttingDown;
    pthread_mutex_unlock(&shopLock);
    return flag;
}

// Used only when a thread fails to start. Sets the flag, then posts every semaphore
// a blocked thread could be waiting on, so each one wakes, sees the flag, and exits.
static void begin_shutdown(void)
{
    pthread_mutex_lock(&shopLock);
    shuttingDown = true;
    pthread_mutex_unlock(&shopLock);

    for (int i = 0; i < numStudents; i++) {
        sem_post(&studentSem[i]);
    }
    sem_post(&arrivals);
    for (int t = 0; t < numTutors; t++) {
        sem_post(&work);
    }
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

            if (shuttingDown) {          // failed startup elsewhere: leave without sitting
                pthread_mutex_unlock(&shopLock);
                return NULL;
            }

            if (freeChairs > 0)
            {
                freeChairs--;                 // chair is held until a tutor selects this student
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
   
        // Post coordinator semaphore
        sem_post(&arrivals);

        // Wait until a tutor selects this student (the tutor posts this semaphore).
        // The tutor writes assignedTutor[] before posting, so it is safe to read here.
        sem_wait(&studentSem[id - 1]);
        if (is_shutting_down()) {
            return NULL;                 // woken by begin_shutdown(), not by a tutor
        }

        // Simulated tutoring: 0.2 ms,   independent of the tutor's own sleep.
        usleep(200);
        log_event(LOG_STUDENT_DONE, id, assignedTutor[id - 1], visit);

        // Count the finished visit. The coordinator reads this under shopLock
        // after the student re-enters the queue, so the lock orders the access.
        completedVisits[id - 1]++;  
    }
    return NULL;
}

static void *tutor_thread(void *arg)
{
    int tutorId = *(int *)arg;

    for (;;)
    {
        // Pend until the coordinator has queued a student, or until shutdown.
        sem_wait(&work);
        if (is_shutting_down()) {
            break;                       // woken by begin_shutdown()
        }

        pthread_mutex_lock(&shopLock);

        // Select the highest-priority waiting student.
        PriorityNode *node = priority_take_best();
        if (node == NULL) {
            // Empty list: this post was a shutdown token, so this tutor is done.
            pthread_mutex_unlock(&shopLock);
            break;
        }

        int id = node->studentId;
        int visit = node->visit;

        // The student leaves the waiting area now, so its chair is free immediately.
        freeChairs++;
        assignedTutor[id - 1] = tutorId;     // written before the post, read by the student
        log_event(LOG_ASSIGNED, id, tutorId, visit);

        pthread_mutex_unlock(&shopLock);

        // Notify that specific student, then simulate tutoring.
        sem_post(&studentSem[id - 1]);
        usleep(200);
        log_event(LOG_TUTOR_DONE, id, tutorId, visit);
    }
    return NULL;
}

static void *coordinator_thread(void *arg)
{
    (void)arg;

    // Each seated student posts `arrivals` once per visit, so the coordinator
    // gets exactly totalVisits posts. Serving that many arrivals ends the loop.
    for (long served = 0; served < totalVisits; served++)
    {
        // Pend until a student has been seated and appended to the waiting room.
        sem_wait(&arrivals);
        if (is_shutting_down()) {
            break;                       // woken by begin_shutdown()
        }

        pthread_mutex_lock(&shopLock);

        // Pop to the customer that has arrived first in waiting room
        // (the post guarantees the queue is non-empty, so the return value is not checked)
        int id;
        queue_pop(&waitingRoom, &id);

        // Check the priority level and add to the correct linked list
        // Visit number = sessions already completed + 1, which is also the priority level.
        int visit = completedVisits[id - 1] + 1;
        priorityNodes[id - 1].visit = visit;
        priority_append(&priorityNodes[id - 1]);
        log_event(LOG_QUEUED, id, -1, visit);

        pthread_mutex_unlock(&shopLock);
        sem_post(&work);   // tell one tutor that a student is waiting in the priority lists

        // find a way to end the for loop correctly
    }

    // Shutdown: every visit is queued. Post one token per tutor. A tutor that wakes
    // to an empty priority list takes a token as its signal to exit.
    for (int t = 0; t < numTutors; t++) {
        sem_post(&work);
    }
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
    totalVisits = (long)num_students * num_help;
    numTutors = num_tutors;
    numStudents = num_students;
    freeChairs = num_chairs;

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

    if (sem_init(&arrivals, 0, 0) != 0) {
        fprintf(stderr, "Failed to initialize arrivals semaphore.\n");
        queue_destroy(&waitingRoom);
        pthread_mutex_destroy(&shopLock);
        return -1;
    }

    if (sem_init(&work, 0, 0) != 0) {
        fprintf(stderr, "Failed to initialize work semaphore.\n");
        sem_destroy(&arrivals);
        queue_destroy(&waitingRoom);
        pthread_mutex_destroy(&shopLock);
        return -1;
    }

    pthread_t *student_threads = malloc(num_students * sizeof(pthread_t));
    pthread_t *tutor_threads = malloc(num_tutors * sizeof(pthread_t));
    int *student_ids = malloc(num_students * sizeof(int));
    int *tutor_ids = malloc(num_tutors * sizeof(int));
    studentSem = malloc(num_students * sizeof(sem_t));
    assignedTutor = malloc(num_students * sizeof(int));
    priorityLists = calloc(num_help, sizeof(PriorityList));
    priorityNodes = calloc(num_students, sizeof(PriorityNode));
    completedVisits = calloc(num_students, sizeof(int));
    pthread_t coordinator;

    if (!student_threads || !tutor_threads || !student_ids || !tutor_ids || !studentSem || !assignedTutor
        || !priorityLists || !priorityNodes || !completedVisits) {
        fprintf(stderr, "Memory allocation failed.\n");
        result = -1;
        goto cleanup;
    }

    for (int i = 0; i < num_students; i++) {
        priorityNodes[i].studentId = i + 1;   // node i belongs to student i + 1
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
    // Only a creation failure sets result, so the normal path never shuts down early.
    if (result != 0) {
        begin_shutdown();
    }
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
    sem_destroy(&arrivals);   // always initialized by this point
    sem_destroy(&work);
    for (int i = 0; i < sems_inited; i++) {
        sem_destroy(&studentSem[i]);
    }
    free(studentSem);
    studentSem = NULL;
    free(assignedTutor);
    assignedTutor = NULL;
    free(priorityLists);
    priorityLists = NULL;
    free(priorityNodes);
    priorityNodes = NULL;
    free(completedVisits);
    completedVisits = NULL;
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
