/* Kernel semaphores: 32-bit IDs, counted tokens and FIFO waiters.
 * Priority-ordered semaphores are approximated by FIFO order. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define SEMA_EVENT_HISTORY_SIZE 50

typedef struct SemaEvent {
    char type[16];           /* "CREATE", "SIGNAL", "WAIT_ACQ", "WAIT_BLOCK", "TIMEOUT", "POLL_ACQ", "POLL_BUSY", "CANCEL" */
    char thread_name[32];
    int32_t count_arg;       /* count requested / signaled */
    int32_t count_after;     /* s->count after operation */
    int32_t result;          /* return code */
    uint64_t timestamp_ms;
} SemaEvent;

typedef struct Waiter {
    int32_t need, result;
    int done;
    pthread_cond_t event;
    struct Waiter *next;
} Waiter;
typedef struct Sema {
    uint32_t id, attr;
    int32_t count, initial, maximum;
    unsigned active;
    int deleted;
    char name[32];
    char creator_thread[32];
    uintptr_t creator_pc;
    char last_signaler[32];
    uint64_t last_signal_ms;
    uint64_t total_signals;
    uint64_t total_waits;
    uint64_t total_timeouts;

    /* Ring buffer of last 50 events */
    SemaEvent events[SEMA_EVENT_HISTORY_SIZE];
    uint32_t event_idx;
    uint32_t event_count;

    Waiter *first;
    struct Sema *next;
} Sema;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Sema *registry;
static uint32_t next_id=1;
static size_t created, deleted, acquired, signaled, timed_out;

static void sema_record_event(Sema *s, const char *type, int32_t count_arg, int32_t count_after, int32_t result) {
    if (!s) return;
    uint32_t idx = s->event_idx;
    SemaEvent *ev = &s->events[idx];
    snprintf(ev->type, sizeof(ev->type), "%s", type);
    const char *cur = runtime_thread_current_name();
    snprintf(ev->thread_name, sizeof(ev->thread_name), "%s", cur ? cur : "unknown");
    ev->count_arg = count_arg;
    ev->count_after = count_after;
    ev->result = result;
    ev->timestamp_ms = runtime_now_ms();
    s->event_idx = (idx + 1) % SEMA_EVENT_HISTORY_SIZE;
    if (s->event_count < SEMA_EVENT_HISTORY_SIZE) s->event_count++;
}
static Sema *find(uint32_t id) {
    for (Sema *s=registry;s;s=s->next) if (s->id==id) return s;
    return NULL;
}
static void host_check(int e) {
    if (e) { fprintf(stderr,"STOP: semaphore host error %d\n",e); exit(21); }
}
/* Waits use this clock for their deadlines. winpthreads condition variables accept only
 * CLOCK_REALTIME (pthread_condattr_setclock(CLOCK_MONOTONIC) fails with EINVAL). */
#ifdef _WIN32
#define WAIT_CLOCK CLOCK_REALTIME
#else
#define WAIT_CLOCK CLOCK_MONOTONIC
#endif
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(WAIT_CLOCK,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static ABI int32_t sem_create(uint32_t *out,const char *name,uint32_t attr,
                              int32_t initial,int32_t maximum,const void *options) {
    if (!out || !name || attr>2 || initial<0 || maximum<=0 || initial>maximum) return ERR(22);
    if (options) { fputs("STOP: semaphore options are not implemented\n",stderr); exit(21); }
    Sema *s=calloc(1,sizeof(*s));
    if (!s) return ERR(12);
    pthread_mutex_lock(&lock);
    if (next_id==UINT32_MAX) { pthread_mutex_unlock(&lock); free(s); return ERR(28); }
    s->id=next_id++; s->attr=attr; s->count=s->initial=initial; s->maximum=maximum;
    if (name) snprintf(s->name, sizeof(s->name), "%s", name);
    const char *cur = runtime_thread_current_name();
    if (cur) snprintf(s->creator_thread, sizeof(s->creator_thread), "%s", cur);
    s->creator_pc = (uintptr_t)__builtin_return_address(0);
    s->next=registry; registry=s; *out=s->id; ++created;
    sema_record_event(s, "CREATE", initial, s->count, 0);
    printf("Runtime: semaphore id=%u ('%s'), attr=%u, initial=%d, max=%d\n",s->id,s->name,attr,initial,maximum);
    pthread_mutex_unlock(&lock);
    return 0;
}
static int32_t wait_count(uint32_t id,int32_t need,uint32_t *timeout,int block) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (need<=0 || need>s->maximum) result=ERR(22);
    else if (s->count>=need) {
        s->count-=need; ++acquired; ++s->total_waits;
        sema_record_event(s, block ? "WAIT_FAST" : "POLL_OK", need, s->count, 0);
    }
    else if (!block) {
        result=ERR(16);
        sema_record_event(s, "POLL_BUSY", need, s->count, result);
    }
    else if (timeout && !*timeout) {
        result=ERR(60); ++timed_out; ++s->total_timeouts;
        sema_record_event(s, "TIMEOUT_0", need, s->count, result);
    }
    else {
        /* Priority-ordered semaphores (attr 2) are served FIFO: there is no
           PS4 priority scheduler, host threads run at equal priority. */
        if (getenv("SPARTAN_TRACE_SEMA")) fprintf(stderr,"Runtime: blocking wait on semaphore %u (need %d, count %d)\n",id,need,s->count); /* deadlock diagnosis */
        Waiter w={.need=need};
        pthread_condattr_t attr;
        host_check(pthread_condattr_init(&attr));
        host_check(pthread_condattr_setclock(&attr,WAIT_CLOCK));
        host_check(pthread_cond_init(&w.event,&attr));
        host_check(pthread_condattr_destroy(&attr));
        Waiter **tail=&s->first;
        while (*tail) tail=&(*tail)->next;
        *tail=&w; ++s->active;
        uint64_t deadline=timeout ? now_ns()+(uint64_t)*timeout*1000 : 0;
        struct timespec end={.tv_sec=(time_t)(deadline/1000000000),.tv_nsec=(long)(deadline%1000000000)};
        runtime_thread_set_blocked_obj("sceKernelWaitSema", s->id, s->name, s->creator_thread, s->creator_pc, s->last_signaler, s->last_signal_ms);
        sema_record_event(s, "WAIT_BLOCK", need, s->count, 0);
        while (!w.done) {
            int e=timeout ? pthread_cond_timedwait(&w.event,&lock,&end) : pthread_cond_wait(&w.event,&lock);
            if (e==ETIMEDOUT && !w.done) {
                Waiter **p=&s->first;
                while (*p!=&w) p=&(*p)->next;
                *p=w.next; w.result=ERR(60); w.done=1; ++timed_out; ++s->total_timeouts;
                sema_record_event(s, "TIMEOUT", need, s->count, ERR(60));
            } else if (e!=ETIMEDOUT) host_check(e);
        }
        runtime_thread_set_blocked(NULL);
        result=w.result;
        if (!result) {
            ++acquired; ++s->total_waits;
            sema_record_event(s, "WAIT_WOKE", need, s->count, 0);
        }
        if (timeout) {
            uint64_t now=now_ns();
            *timeout=result ? 0 : (uint32_t)(now>=deadline ? 0 : (deadline-now)/1000);
        }
        host_check(pthread_cond_destroy(&w.event));
        if (!--s->active && s->deleted) free(s);
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t sem_wait(uint32_t id,int32_t need,uint32_t *timeout) { return wait_count(id,need,timeout,1); }
static ABI int32_t sem_poll(uint32_t id,int32_t need) { return wait_count(id,need,NULL,0); }
static ABI int32_t sem_signal(uint32_t id,int32_t count) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) {
        result=ERR(3);
    } else if (count<=0) {
        result=ERR(22);
        fprintf(stderr, "Runtime: sem_signal FAILED (invalid count=%d) on sem id=%u ('%s') by thread '%s'\n",
                count, id, s->name, runtime_thread_current_name());
        fflush(stderr);
        sema_record_event(s, "SIG_FAIL", count, s->count, result);
    } else if (count > s->maximum - s->count) {
        result=ERR(22);
        fprintf(stderr, "Runtime: sem_signal EXCEEDED MAX! sem id=%u ('%s') cur=%d max=%d signal=%d by thread '%s'\n",
                id, s->name, s->count, s->maximum, count, runtime_thread_current_name());
        fflush(stderr);
        sema_record_event(s, "SIG_OVER", count, s->count, result);
    } else {
        s->count+=count; ++signaled; ++s->total_signals;
        const char *cur = runtime_thread_current_name();
        if (cur) snprintf(s->last_signaler, sizeof(s->last_signaler), "%s", cur);
        s->last_signal_ms = runtime_now_ms();
        sema_record_event(s, "SIGNAL", count, s->count, 0);
        Waiter **p=&s->first;
        while (*p) {
            Waiter *w=*p;
            if (w->need>s->count) { p=&w->next; continue; }
            s->count-=w->need; *p=w->next; w->done=1;
            host_check(pthread_cond_signal(&w->event));
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static int32_t wake_all(Sema *s,int32_t result) {
    int32_t count=0;
    while (s->first) {
        Waiter *w=s->first; s->first=w->next;
        w->result=result; w->done=1; ++count;
        host_check(pthread_cond_signal(&w->event));
    }
    return count;
}
static ABI int32_t sem_cancel(uint32_t id,int32_t count,int32_t *waiters) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (count>s->maximum) result=ERR(22);
    else {
        int32_t n=wake_all(s,ERR(85));
        if (waiters) *waiters=n;
        s->count=count<0 ? s->initial : count;
        sema_record_event(s, "CANCEL", count, s->count, 0);
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t sem_delete(uint32_t id) {
    pthread_mutex_lock(&lock);
    Sema **p=&registry;
    while (*p && (*p)->id!=id) p=&(*p)->next;
    if (!*p) { pthread_mutex_unlock(&lock); return ERR(3); }
    Sema *s=*p; *p=s->next; s->deleted=1; ++deleted;
    wake_all(s,ERR(13));
    if (!s->active) free(s);
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Read-only test/diagnostic snapshot, not exposed to guest code. */
unsigned runtime_sema_waiters(uint32_t id) {
    pthread_mutex_lock(&lock);
    Sema *s=find(id); unsigned n=0;
    if (s) for (Waiter *w=s->first;w;w=w->next) ++n;
    pthread_mutex_unlock(&lock);
    return n;
}
uintptr_t runtime_sema_resolve(const char *name) {
    if (!strcmp(name,"188x57JYp0g#libkernel")) return (uintptr_t)sem_create;
    if (!strcmp(name,"Zxa0VhQVTsk#libkernel")) return (uintptr_t)sem_wait;
    if (!strcmp(name,"4czppHBiriw#libkernel")) return (uintptr_t)sem_signal;
    if (!strcmp(name,"12wOHk8ywb0#libkernel")) return (uintptr_t)sem_poll;
    if (!strcmp(name,"4DM06U2BNEY#libkernel")) return (uintptr_t)sem_cancel;
    if (!strcmp(name,"R1Jvn8bSCW8#libkernel")) return (uintptr_t)sem_delete;
    return 0;
}
void runtime_sema_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: semaphores created=%zu, deleted=%zu, acquired=%zu, signals=%zu, timeouts=%zu\n",
           created,deleted,acquired,signaled,timed_out);
    pthread_mutex_unlock(&lock);
}

void runtime_sema_dump_history(uint32_t id, void *file_ptr) {
    FILE *out = file_ptr ? (FILE *)file_ptr : stderr;
    pthread_mutex_lock(&lock);
    Sema *s = find(id);
    if (!s) {
        fprintf(out, "       [Sema History] id=%u not found in registry\n", id);
        pthread_mutex_unlock(&lock);
        return;
    }
    fprintf(out, "       [Sema Stats] id=0x%x (%u) name='%s' cur_count=%d max=%d | total_signals=%llu total_waits=%llu total_timeouts=%llu\n",
            s->id, s->id, s->name, s->count, s->maximum,
            (unsigned long long)s->total_signals,
            (unsigned long long)s->total_waits,
            (unsigned long long)s->total_timeouts);
    uint32_t count = s->event_count;
    uint32_t start = (s->event_count == SEMA_EVENT_HISTORY_SIZE) ? s->event_idx : 0;
    fprintf(out, "       [Sema History: last %u events for id=0x%x]\n", count, s->id);
    uint64_t now = runtime_now_ms();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (start + i) % SEMA_EVENT_HISTORY_SIZE;
        SemaEvent *ev = &s->events[idx];
        uint64_t ago = (now >= ev->timestamp_ms) ? (now - ev->timestamp_ms) : 0;
        fprintf(out, "         #%02u: [%-10s] %llu ms ago by '%s' arg=%d after=%d rc=0x%08x\n",
                i + 1, ev->type, (unsigned long long)ago, ev->thread_name, ev->count_arg, ev->count_after, (uint32_t)ev->result);
    }
    pthread_mutex_unlock(&lock);
}

