#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "event.h"
#include "event/timeout.h"
#include "schedstatistics.h"
#include "thread.h"
#include "ztimer.h"

#include "prasle.h"
#include "radio.h"

#define NO_VALUE        UINT16_MAX
#define SEND_GAP_MS     1

enum { IDLE, RUNNING, DONE };

typedef struct __attribute__((packed)) {
    uint16_t min;
    uint16_t leader;
} value_t;

static char stack[THREAD_STACKSIZE_DEFAULT + THREAD_EXTRA_STACKSIZE_PRINTF];
static event_queue_t queue;
static event_timeout_t round_timeout;

static uint16_t neighbors[PRASLE_MAX_NEIGHBORS];
static unsigned neighbor_count;
static uint16_t rank;
static unsigned k_rounds;
static unsigned round_ms;

static int state = IDLE;
static int rounds_left;
static unsigned rounds_run;
static uint16_t min, leader;
static uint16_t temp_min, temp_leader;
static unsigned sent, received;
static uint32_t started_ms;
static uint64_t started_busy_us;

static bool better(uint16_t min_a, uint16_t id_a, uint16_t min_b, uint16_t id_b)
{
    return min_a < min_b || (min_a == min_b && id_a < id_b);
}

static bool is_neighbor(uint16_t id)
{
    for (unsigned i = 0; i < neighbor_count; i++) {
        if (neighbors[i] == id) {
            return true;
        }
    }
    return false;
}

static uint64_t busy_us(void)
{
    uint64_t total = 0;

    for (kernel_pid_t pid = KERNEL_PID_FIRST; pid <= KERNEL_PID_LAST; pid++) {
        total += sched_pidlist[pid].runtime_us;
    }
    return total;
}

/* Spread first sends over one round by id so neighbors do not transmit at once.*/
static uint32_t first_round_delay(void)
{
    return ((radio_id() * 97U) & 0xffU) * round_ms / 256U;
}

static void begin(void)
{
    state = RUNNING;
    rounds_left = k_rounds + 1;
    rounds_run = 0;
    min = NO_VALUE;
    leader = radio_id();
    temp_min = rank;
    temp_leader = radio_id();
    sent = 0;
    received = 0;
    started_ms = ztimer_now(ZTIMER_MSEC);
    started_busy_us = busy_us();
    event_timeout_set(&round_timeout, first_round_delay());
}

static void finish(void)
{
    uint32_t elapsed_ms = ztimer_now(ZTIMER_MSEC) - started_ms;
    uint32_t cpu_permille = elapsed_ms ? (busy_us() - started_busy_us) / elapsed_ms : 0;

    state = DONE;
    event_timeout_clear(&round_timeout);
    printf("done rank=%u leader=%u rounds=%u time=%lu sent=%u received=%u cpu=%lu.%lu\n",
           rank, leader, rounds_run, (unsigned long)elapsed_ms, sent, received,
           (unsigned long)(cpu_permille / 10), (unsigned long)(cpu_permille % 10));
}

static void send_to_neighbors(void)
{
    value_t value = { .min = min, .leader = leader };

    for (unsigned i = 0; i < neighbor_count; i++) {
        /* A send returns once GNRC has the packet, and frames queued back to
         * back can still be dropped further down, so space them out. */
        if (i > 0) {
            ztimer_sleep(ZTIMER_MSEC, SEND_GAP_MS);
        }
        if (radio_send(neighbors[i], &value, sizeof(value)) >= 0) {
            sent++;
        }
    }
}

static void on_value(uint16_t sender, const void *data, size_t len)
{
    value_t value;

    if (len != sizeof(value) || state == DONE || !is_neighbor(sender)) {
        return;
    }
    /* Our own start was lost or is still on its way, so join the neighbour. */
    if (state == IDLE) {
        begin();
    }

    memcpy(&value, data, sizeof(value));
    received++;
    if (better(value.min, value.leader, temp_min, temp_leader)) {
        temp_min = value.min;
        temp_leader = value.leader;
    }
}

static void on_round(event_t *event)
{
    (void)event;

    if (state != RUNNING) {
        return;
    }
    event_timeout_set(&round_timeout, round_ms);
    rounds_left--;
    rounds_run++;

    if (better(temp_min, temp_leader, min, leader)) {
        min = temp_min;
        leader = temp_leader;
        send_to_neighbors();
    }
    else if (rounds_left <= 0) {
        finish();
    }
}

static void on_start(event_t *event)
{
    (void)event;

    if (state == IDLE && neighbor_count > 0) {
        begin();
    }
}

static event_t round_event = { .handler = on_round };
static event_t start_event = { .handler = on_start };

static void *run(void *arg)
{
    (void)arg;

    event_queue_init(&queue);
    event_timeout_ztimer_init(&round_timeout, ZTIMER_MSEC, &queue, &round_event);
    if (radio_init(&queue, on_value) < 0) {
        puts("radio init failed");
        return NULL;
    }
    event_loop(&queue);
    return NULL;
}

void prasle_init(void)
{
    thread_create(stack, sizeof(stack), THREAD_PRIORITY_MAIN - 1,
                  THREAD_CREATE_STACKTEST, run, NULL, "prasle");
}

int prasle_setup(uint16_t new_rank, unsigned new_k_rounds, unsigned new_round_ms,
                 const uint16_t *list, unsigned count)
{
    if (state != IDLE || new_rank == 0 || new_round_ms == 0 ||
        count == 0 || count > PRASLE_MAX_NEIGHBORS) {
        return -1;
    }
    rank = new_rank;
    k_rounds = new_k_rounds;
    round_ms = new_round_ms;
    memcpy(neighbors, list, count * sizeof(*list));
    neighbor_count = count;
    return 0;
}

void prasle_start(void)
{
    event_post(&queue, &start_event);
}
