/* PS4 event flags used by the Gravity Rush runtime.
 * Handles are guest-visible 32-bit IDs; all mutable state stays host-side. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERR(n) ((int32_t)(UINT32_C(0x80020000) | (n)))
#define ERR_EPERM ERR(1)
#define ERR_ESRCH ERR(3)
#define ERR_EACCES ERR(13)
#define ERR_EBUSY ERR(16)
#define ERR_EINVAL ERR(22)
#define ERR_ETIMEDOUT ERR(60)

#define EVF_WAIT_AND 0x01u
#define EVF_WAIT_OR  0x02u
#define EVF_CLEAR_ALL 0x10u
#define EVF_CLEAR_BITS 0x20u
#define EVF_ATTR_SINGLE 0x10u
#define EVF_ATTR_MULTI  0x20u

#define EVF_EVENT_HISTORY_SIZE 50

typedef struct EventFlagEvent {
    char type[16];           /* "CREATE", "SET", "CLEAR", "WAIT_FAST", "WAIT_BLOCK", "WAIT_WOKE", "TIMEOUT", "CANCEL" */
    char thread_name[32];
    uint64_t pattern;
    uint64_t bits_after;
    int32_t result;
    uint64_t timestamp_ms;
} EventFlagEvent;

typedef struct EventWaiter {
    uint64_t pattern;
    uint32_t mode;
    int done;
    int result;
    uint64_t matched;
    pthread_cond_t cond;
    struct EventWaiter *next;
} EventWaiter;

typedef struct EventFlag {
    uint32_t id;
    uint32_t attr;
    uint64_t bits;
    unsigned active;
    int deleted;
    char name[32];
    char creator_thread[32];
    uintptr_t creator_pc;
    char last_signaler[32];
    uint64_t last_signal_ms;
    uint64_t total_sets;
    uint64_t total_waits;
    uint64_t total_timeouts;

    /* Ring buffer */
    EventFlagEvent events[EVF_EVENT_HISTORY_SIZE];
    uint32_t event_idx;
    uint32_t event_count;

    pthread_cond_t drained;
    EventWaiter *waiters;
    struct EventFlag *next;
} EventFlag;

static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static EventFlag *event_flags;
static uint32_t next_event_id = 1;

static void eventflag_record_event(EventFlag *ef, const char *type, uint64_t pattern, uint64_t bits_after, int32_t result) {
    if (!ef) return;
    uint32_t idx = ef->event_idx;
    EventFlagEvent *ev = &ef->events[idx];
    snprintf(ev->type, sizeof(ev->type), "%s", type);
    const char *cur = runtime_thread_current_name();
    snprintf(ev->thread_name, sizeof(ev->thread_name), "%s", cur ? cur : "unknown");
    ev->pattern = pattern;
    ev->bits_after = bits_after;
    ev->result = result;
    ev->timestamp_ms = runtime_now_ms();
    ef->event_idx = (idx + 1) % EVF_EVENT_HISTORY_SIZE;
    if (ef->event_count < EVF_EVENT_HISTORY_SIZE) ef->event_count++;
}

static EventFlag *event_find(uint32_t id) {
    for (EventFlag *event = event_flags; event; event = event->next)
        if (event->id == id) return event;
    return NULL;
}

static int event_condition(const EventFlag *event, uint64_t pattern, uint32_t mode) {
    if ((mode & 0x0fu) == EVF_WAIT_AND) return (event->bits & pattern) == pattern;
    return (event->bits & pattern) != 0;
}

static void event_apply_clear(EventFlag *event, uint64_t pattern, uint32_t mode) {
    switch (mode & 0xf0u) {
    case 0: break;
    case EVF_CLEAR_ALL: event->bits = 0; break;
    case EVF_CLEAR_BITS: event->bits &= ~pattern; break;
    }
}

static void event_remove_waiter(EventFlag *event, EventWaiter *waiter) {
    EventWaiter **link = &event->waiters;
    while (*link && *link != waiter) link = &(*link)->next;
    if (*link) *link = waiter->next;
    if (event->deleted && event->active == 1) pthread_cond_signal(&event->drained);
    if (event->active) --event->active;
}

static ABI int32_t event_create(uint32_t *out, const char *name, uint32_t attr,
                                uint64_t initial, const void *options) {
    if (!out || !name || options || (attr & ~0x33u) ||
        ((attr & 0x0fu) > 2u) || ((attr & 0xf0u) != 0 &&
        (attr & 0xf0u) != EVF_ATTR_SINGLE && (attr & 0xf0u) != EVF_ATTR_MULTI))
        return ERR_EINVAL;
    if (strnlen(name, 32) >= 32) return ERR(63); /* ENAMETOOLONG */
    EventFlag *event = calloc(1, sizeof(*event));
    if (!event) return ERR(12);
    if (pthread_cond_init(&event->drained, NULL)) { free(event); return ERR(12); }
    pthread_mutex_lock(&event_lock);
    if (!next_event_id || next_event_id == UINT32_MAX) {
        pthread_mutex_unlock(&event_lock);
        pthread_cond_destroy(&event->drained);
        free(event);
        return ERR(28);
    }
    event->id = next_event_id++;
    event->attr = attr;
    event->bits = initial;
    if (name) snprintf(event->name, sizeof(event->name), "%s", name);
    const char *cur = runtime_thread_current_name();
    if (cur) snprintf(event->creator_thread, sizeof(event->creator_thread), "%s", cur);
    event->creator_pc = (uintptr_t)__builtin_return_address(0);
    event->next = event_flags;
    event_flags = event;
    *out = event->id;
    eventflag_record_event(event, "CREATE", initial, event->bits, 0);
    printf("Runtime: eventflag id=%u ('%s'), attr=0x%x, initial=0x%llx\n",
           event->id, event->name, attr, (unsigned long long)initial);
    pthread_mutex_unlock(&event_lock);
    return 0;
}

static ABI int32_t event_delete(uint32_t id) {
    pthread_mutex_lock(&event_lock);
    EventFlag **link = &event_flags;
    while (*link && (*link)->id != id) link = &(*link)->next;
    if (!*link) { pthread_mutex_unlock(&event_lock); return ERR_ESRCH; }
    EventFlag *event = *link;
    *link = event->next;
    event->deleted = 1;
    for (EventWaiter *w = event->waiters; w; w = w->next) {
        w->result = ERR_EACCES;
        w->done = 1;
        pthread_cond_signal(&w->cond);
    }
    while (event->active) pthread_cond_wait(&event->drained, &event_lock);
    pthread_mutex_unlock(&event_lock);
    pthread_cond_destroy(&event->drained);
    free(event);
    return 0;
}

static ABI int32_t event_set(uint32_t id, uint64_t pattern) {
    pthread_mutex_lock(&event_lock);
    EventFlag *event = event_find(id);
    if (!event) { pthread_mutex_unlock(&event_lock); return ERR_ESRCH; }
    event->bits |= pattern;
    ++event->total_sets;
    const char *cur = runtime_thread_current_name();
    if (cur) snprintf(event->last_signaler, sizeof(event->last_signaler), "%s", cur);
    event->last_signal_ms = runtime_now_ms();
    eventflag_record_event(event, "SET", pattern, event->bits, 0);
    for (EventWaiter *w = event->waiters; w; w = w->next) {
        if (w->done || !event_condition(event, w->pattern, w->mode)) continue;
        w->matched = event->bits;
        event_apply_clear(event, w->pattern, w->mode);
        w->done = 1;
        pthread_cond_signal(&w->cond);
    }
    pthread_mutex_unlock(&event_lock);
    return 0;
}

static ABI int32_t event_clear(uint32_t id, uint64_t mask) {
    pthread_mutex_lock(&event_lock);
    EventFlag *event = event_find(id);
    if (!event) { pthread_mutex_unlock(&event_lock); return ERR_ESRCH; }
    event->bits &= mask;
    eventflag_record_event(event, "CLEAR", mask, event->bits, 0);
    pthread_mutex_unlock(&event_lock);
    return 0;
}

static ABI int32_t event_wait(uint32_t id, uint64_t pattern, uint32_t mode,
                              uint64_t *result, uint32_t *timeout_us) {
    uint32_t wait_mode = mode & 0x0fu;
    uint32_t clear_mode = mode & 0xf0u;
    if (!pattern || (wait_mode != EVF_WAIT_AND && wait_mode != EVF_WAIT_OR) ||
        (clear_mode != 0 && clear_mode != EVF_CLEAR_ALL && clear_mode != EVF_CLEAR_BITS) ||
        (mode & ~0xffu)) return ERR_EINVAL;

    pthread_mutex_lock(&event_lock);
    EventFlag *event = event_find(id);
    if (!event) { pthread_mutex_unlock(&event_lock); return ERR_ESRCH; }
    if ((event->attr & 0xf0u) != EVF_ATTR_MULTI && event->waiters) {
        pthread_mutex_unlock(&event_lock);
        return ERR_EPERM;
    }
    if (event_condition(event, pattern, mode)) {
        if (result) *result = event->bits;
        event_apply_clear(event, pattern, mode);
        ++event->total_waits;
        eventflag_record_event(event, "WAIT_FAST", pattern, event->bits, 0);
        pthread_mutex_unlock(&event_lock);
        return 0;
    }
    if (timeout_us && *timeout_us == 0) {
        if (result) *result = event->bits;
        eventflag_record_event(event, "TIMEOUT_0", pattern, event->bits, ERR_EBUSY);
        pthread_mutex_unlock(&event_lock);
        return ERR_EBUSY;
    }

    EventWaiter waiter = {.pattern = pattern, .mode = mode};
    if (pthread_cond_init(&waiter.cond, NULL)) {
        pthread_mutex_unlock(&event_lock);
        return ERR(12);
    }
    waiter.next = event->waiters;
    event->waiters = &waiter;
    ++event->active;
    int wait_error = 0;
    struct timespec deadline = {0};
    if (timeout_us) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += *timeout_us / 1000000u;
        deadline.tv_nsec += (long)(*timeout_us % 1000000u) * 1000L;
        if (deadline.tv_nsec >= 1000000000L) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L; }
    }
    runtime_thread_set_blocked_obj("sceKernelWaitEventFlag", event->id, event->name, event->creator_thread, event->creator_pc, event->last_signaler, event->last_signal_ms);
    eventflag_record_event(event, "WAIT_BLOCK", pattern, event->bits, 0);
    while (!waiter.done && !wait_error) {
        wait_error = timeout_us ? pthread_cond_timedwait(&waiter.cond, &event_lock, &deadline)
                                : pthread_cond_wait(&waiter.cond, &event_lock);
    }
    runtime_thread_set_blocked(NULL);
    if (wait_error == ETIMEDOUT && !waiter.done) {
        if (timeout_us) *timeout_us = 0;
        if (result) *result = event->bits;
        wait_error = ERR_ETIMEDOUT;
        ++event->total_timeouts;
        eventflag_record_event(event, "TIMEOUT", pattern, event->bits, ERR_ETIMEDOUT);
    } else if (wait_error) {
        wait_error = ERR(5);
        eventflag_record_event(event, "WAIT_ERR", pattern, event->bits, wait_error);
    } else {
        wait_error = waiter.result;
        if (!wait_error && result) *result = waiter.matched;
        if (!wait_error && timeout_us) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            int64_t left = (int64_t)(deadline.tv_sec - now.tv_sec) * 1000000 +
                           (deadline.tv_nsec - now.tv_nsec) / 1000;
            *timeout_us = left > 0 ? (uint32_t)left : 0;
        }
        if (!wait_error) {
            ++event->total_waits;
            eventflag_record_event(event, "WAIT_WOKE", pattern, event->bits, 0);
        }
    }
    event_remove_waiter(event, &waiter);
    pthread_cond_destroy(&waiter.cond);
    pthread_mutex_unlock(&event_lock);
    return wait_error;
}

void runtime_eventflag_dump_history(uint32_t id, void *file_ptr) {
    FILE *out = file_ptr ? (FILE *)file_ptr : stderr;
    pthread_mutex_lock(&event_lock);
    EventFlag *ef = event_find(id);
    if (!ef) {
        fprintf(out, "       [EventFlag History] id=%u not found in registry\n", id);
        pthread_mutex_unlock(&event_lock);
        return;
    }
    fprintf(out, "       [EventFlag Stats] id=0x%x (%u) name='%s' cur_bits=0x%llx | total_sets=%llu total_waits=%llu total_timeouts=%llu\n",
            ef->id, ef->id, ef->name, (unsigned long long)ef->bits,
            (unsigned long long)ef->total_sets,
            (unsigned long long)ef->total_waits,
            (unsigned long long)ef->total_timeouts);
    uint32_t count = ef->event_count;
    uint32_t start = (ef->event_count == EVF_EVENT_HISTORY_SIZE) ? ef->event_idx : 0;
    fprintf(out, "       [EventFlag History: last %u events for id=0x%x]\n", count, ef->id);
    uint64_t now = runtime_now_ms();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (start + i) % EVF_EVENT_HISTORY_SIZE;
        EventFlagEvent *ev = &ef->events[idx];
        uint64_t ago = (now >= ev->timestamp_ms) ? (now - ev->timestamp_ms) : 0;
        fprintf(out, "         #%02u: [%-10s] %llu ms ago by '%s' pat=0x%llx after=0x%llx rc=0x%08x\n",
                i + 1, ev->type, (unsigned long long)ago, ev->thread_name,
                (unsigned long long)ev->pattern, (unsigned long long)ev->bits_after, (uint32_t)ev->result);
    }
    pthread_mutex_unlock(&event_lock);
}

uintptr_t runtime_eventflag_resolve(const char *name) {
    if (!strcmp(name, "BpFoboUJoZU#libkernel")) return (uintptr_t)event_create;
    if (!strcmp(name, "8mql9OcQnd4#libkernel")) return (uintptr_t)event_delete;
    if (!strcmp(name, "IOnSvHzqu6A#libkernel")) return (uintptr_t)event_set;
    if (!strcmp(name, "7uhBFWRAS60#libkernel")) return (uintptr_t)event_clear;
    if (!strcmp(name, "JTvBflhYazQ#libkernel")) return (uintptr_t)event_wait;
    return 0;
}
