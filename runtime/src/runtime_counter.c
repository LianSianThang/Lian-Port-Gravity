/* runtime_counter.c — per-import call-count instrumentation & stall watchdog (Task 1).
 *
 * Supports:
 *   1. Full import call counting for BOTH runtime-resolved and stub-bound imports.
 *   2. LIAN_NO_COUNTERS=1 / SPARTAN_NO_COUNTERS=1 to disable all wrapping at runtime.
 *   3. Missing 154 symbol name lookup (missing_154.inc) for human-readable report.
 *   4. Logging-only watchdog monitoring:
 *      - No new frame submitted for >= 5 seconds
 *      - Black/idle screen (<=3 draws/frame) for >= 10 seconds
 *      - Prints thread name, blocking state, and recent imports for every guest thread.
 */
#define _CRT_RAND_S
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#endif

#define MAX_IMPORTS      4096
#define STUB_BYTES       128
#define STUBS_PER_PAGE   32         /* 4096 / 128 */
#define STUB_PAGES       ((MAX_IMPORTS + STUBS_PER_PAGE - 1) / STUBS_PER_PAGE)

#define RET_HIST_SIZE    8

static const unsigned char *watch_image;
static size_t watch_image_size;
void runtime_counter_set_image(const void *base, size_t size) {
    watch_image=base;
    watch_image_size=size;
}

static void dump_memory_watches(void) {
#ifdef _WIN32
    const char *spec=getenv("LIAN_WATCH_RVA");
    if (!spec || !watch_image) return;
    unsigned watches=0;
    while (*spec && watches++<8) {
        char *end;
        unsigned long long offset=strtoull(spec,&end,0);
        if (end==spec || *end!=':') break;
        spec=end+1;
        unsigned long long size=strtoull(spec,&end,0);
        if (end==spec || (*end && *end!=',')) break;
        spec=*end ? end+1 : end;
        if (!size || size>256 || offset>watch_image_size || size>watch_image_size-offset) {
            fprintf(stderr,"Memory watch: rejected range 0x%llx:%llu\n",offset,size);
            continue;
        }
        unsigned char bytes[256];
        SIZE_T copied=0;
        if (!ReadProcessMemory(GetCurrentProcess(),watch_image+(size_t)offset,bytes,(SIZE_T)size,&copied) || copied!=size) {
            fprintf(stderr,"Memory watch: unreadable RVA 0x%llx\n",offset);
            continue;
        }
        char hex[513];
        const char digits[]="0123456789abcdef";
        for (size_t i=0;i<(size_t)size;++i) {
            hex[i*2]=digits[bytes[i]>>4]; hex[i*2+1]=digits[bytes[i]&15];
        }
        hex[size*2]=0;
        fprintf(stderr,"Memory watch: RVA 0x%llx size=%llu bytes=%s\n",offset,size,hex);
    }
#endif
}

typedef struct {
    uint64_t val;
    _Atomic uint64_t count;
} RetHist;

typedef struct {
    char              nid[64];   /* "NID#library" string */
    char              sym[128];  /* human name, or NID */
    uintptr_t         real_fn;
    _Atomic uint64_t  count;
    int               is_data;  /* 1 = data import; 0 = function import */
    int               is_stub;  /* 1 = bound to synthetic stub; 0 = runtime resolved */
    _Atomic uint64_t  ret_total;
    RetHist           ret_hist[RET_HIST_SIZE];
    _Atomic uint64_t  ret_other;
} CounterSlot;

static CounterSlot    slots[MAX_IMPORTS];
static uint64_t       slot_count;
static unsigned char *stub_mem;
static int            g_no_counters = -1;
static _Atomic uint64_t g_last_frame_ms;
static _Atomic uint64_t g_total_frames;
static int            g_game_started;

/* ------------------------------------------------------------------
 * Symbol lookup for missing 154 symbols (AeroLib / gravity_stubs.prx)
 * ------------------------------------------------------------------ */
static const struct { const char *nid, *symbol; } missing_154[] = {
#include "missing_154.inc"
};

static const char *resolve_human_name(const char *nid_scoped) {
    /* 1. Try standard runtime_symbol */
    const char *s = runtime_symbol(nid_scoped);
    if (s) return s;

    /* 2. Strip library suffix "#..." to query missing_154 table */
    char bare_nid[64];
    const char *hash = strchr(nid_scoped, '#');
    if (hash) {
        size_t len = (size_t)(hash - nid_scoped);
        if (len >= sizeof(bare_nid)) len = sizeof(bare_nid) - 1;
        memcpy(bare_nid, nid_scoped, len);
        bare_nid[len] = '\0';
    } else {
        strncpy(bare_nid, nid_scoped, sizeof(bare_nid) - 1);
        bare_nid[sizeof(bare_nid) - 1] = '\0';
    }

    for (size_t i = 0; i < sizeof(missing_154)/sizeof(*missing_154); ++i) {
        if (!strcmp(bare_nid, missing_154[i].nid))
            return missing_154[i].symbol;
    }
    return NULL;
}

static uint64_t get_time_monotonic_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

uint64_t runtime_now_ms(void) {
    return get_time_monotonic_ms();
}

/* ------------------------------------------------------------------
 * Watchdog background worker (logging only)
 * ------------------------------------------------------------------ */
void runtime_counter_dump_stall_profile(uint32_t window_ms);

#ifdef _WIN32
static DWORD WINAPI watchdog_thread(LPVOID param) {
    (void)param;
    uint64_t last_report_ms = 0;
    uint64_t last_normal_draws_ms = 0;
    uint64_t prev_draws = 0, prev_frames = 0;
    while (1) {
        Sleep(1000);
        if (!g_game_started) continue;

        uint64_t now = get_time_monotonic_ms();
        uint64_t last_frame = atomic_load(&g_last_frame_ms);

        if (last_frame == 0) continue;

        uint64_t total_draws = spartanportgpu_total_draws();
        // The Vulkan runtime records frames directly; the legacy notify hook is
        // not called on this path. Using its untouched counter falsely reported
        // a stall throughout normally rendered title menus and gameplay.
        uint64_t total_frames = spartanportgpu_total_frames();
        if (total_frames != prev_frames) {
            last_frame = now;
            atomic_store(&g_last_frame_ms, now);
        }

        uint64_t frame_delta = total_frames - prev_frames;
        uint64_t draw_delta = total_draws - prev_draws;
        prev_frames = total_frames;
        prev_draws = total_draws;

        double draws_per_frame = frame_delta ? (double)draw_delta / (double)frame_delta : 0.0;
        if (draws_per_frame > 3.0) {
            last_normal_draws_ms = now;
        }

        int no_frame_stall = (now >= last_frame + 5000);
        int black_screen_stall = (last_normal_draws_ms > 0 && now >= last_normal_draws_ms + 10000);

        if ((no_frame_stall || black_screen_stall) && (now >= last_report_ms + 15000)) {
            last_report_ms = now;
            fprintf(stderr, "\n================================================================================\n");
            if (no_frame_stall) {
                fprintf(stderr, "[WATCHDOG STALL DETECTED] No new frame submitted for >= 5.0 seconds!\n");
                fprintf(stderr, "Elapsed since last frame: %llu ms | Total frames: %llu | Total draws: %llu\n",
                        (unsigned long long)(now - last_frame),
                        (unsigned long long)total_frames,
                        (unsigned long long)total_draws);
            } else {
                fprintf(stderr, "[WATCHDOG STALL DETECTED] Black/idle screen (<=3 draws/frame) for >= 10.0 seconds!\n");
                fprintf(stderr, "Elapsed since normal rendering: %llu ms | Recent draws/frame: %.2f | Total frames: %llu | Total draws: %llu\n",
                        (unsigned long long)(now - last_normal_draws_ms),
                        draws_per_frame,
                        (unsigned long long)total_frames,
                        (unsigned long long)total_draws);
            }
            /* Task 2a Part B: Stall-window 5.0s import call delta & return value histogram profile */
            dump_memory_watches();
            runtime_counter_dump_stall_profile(5000);
            runtime_thread_dump_watchdog();
            fprintf(stderr, "================================================================================\n\n");
            fflush(stderr);
        }
    }
    return 0;
}
#endif

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */
int runtime_counter_is_disabled(void) {
    if (g_no_counters < 0) {
        const char *env = getenv("LIAN_NO_COUNTERS");
        if (!env) env = getenv("SPARTAN_NO_COUNTERS");
        g_no_counters = (env && !strcmp(env, "1")) ? 1 : 0;
    }
    return g_no_counters;
}

void runtime_counter_init(void) {
    if (runtime_counter_is_disabled()) {
        puts("Runtime: LIAN_NO_COUNTERS=1 active (import counters, watchdog, and thread blocking hooks disabled)");
        return;
    }

    size_t total = (size_t)STUB_PAGES * 4096;
#ifdef _WIN32
    stub_mem = (unsigned char *)VirtualAlloc(
        NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub_mem) { fputs("runtime_counter: VirtualAlloc failed\n", stderr); exit(1); }

    HANDLE h = CreateThread(NULL, 65536, watchdog_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
#else
    stub_mem = (unsigned char *)mmap(
        NULL, total, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub_mem == MAP_FAILED) { fputs("runtime_counter: mmap failed\n", stderr); exit(1); }
#endif
}

void runtime_counter_notify_start(void) {
    if (runtime_counter_is_disabled()) return;
    g_game_started = 1;
    atomic_store(&g_last_frame_ms, get_time_monotonic_ms());
}

void runtime_counter_notify_frame(void) {
    if (runtime_counter_is_disabled()) return;
    atomic_store(&g_last_frame_ms, get_time_monotonic_ms());
    atomic_fetch_add(&g_total_frames, 1);
}

static int64_t find_or_create(const char *nid, uintptr_t real_fn, int is_data, int is_stub) {
    for (uint64_t i = 0; i < slot_count; ++i) {
        if (!strcmp(slots[i].nid, nid))
            return (int64_t)i;
    }
    if (slot_count >= MAX_IMPORTS) return -1;

    uint64_t idx = slot_count++;
    CounterSlot *s = &slots[idx];
    strncpy(s->nid, nid, sizeof(s->nid) - 1);
    s->nid[sizeof(s->nid) - 1] = '\0';
    s->real_fn  = real_fn;
    s->is_data  = is_data;
    s->is_stub  = is_stub;
    atomic_store(&s->count, 0);
    atomic_store(&s->ret_total, 0);
    atomic_store(&s->ret_other, 0);
    for (int h = 0; h < RET_HIST_SIZE; ++h) {
        s->ret_hist[h].val = 0;
        atomic_store(&s->ret_hist[h].count, 0);
    }

    const char *sym = resolve_human_name(nid);
    if (sym) {
        strncpy(s->sym, sym, sizeof(s->sym) - 1);
        s->sym[sizeof(s->sym) - 1] = '\0';
    } else {
        strncpy(s->sym, nid, sizeof(s->sym) - 1);
        s->sym[sizeof(s->sym) - 1] = '\0';
    }
    return (int64_t)idx;
}

ABI void runtime_counter_record_ret(uint64_t idx, uint64_t val) {
    if (idx >= slot_count) return;
    CounterSlot *s = &slots[idx];
    atomic_fetch_add_explicit(&s->ret_total, 1, memory_order_relaxed);
    for (int i = 0; i < RET_HIST_SIZE; ++i) {
        if (s->ret_hist[i].val == val && atomic_load_explicit(&s->ret_hist[i].count, memory_order_relaxed) > 0) {
            atomic_fetch_add_explicit(&s->ret_hist[i].count, 1, memory_order_relaxed);
            return;
        }
    }
    for (int i = 0; i < RET_HIST_SIZE; ++i) {
        uint64_t expected = 0;
        if (atomic_compare_exchange_strong(&s->ret_hist[i].count, &expected, 1)) {
            s->ret_hist[i].val = val;
            return;
        }
    }
    atomic_fetch_add_explicit(&s->ret_other, 1, memory_order_relaxed);
}

static void emit_stub(unsigned char *t, int64_t idx, uintptr_t real_fn) {
    _Atomic uint64_t *counter_ptr = &slots[idx].count;
    int pos = 0;

    /* mov r11, &counter_ptr */
    t[pos++] = 0x49; t[pos++] = 0xBB;
    memcpy(t + pos, &counter_ptr, 8); pos += 8;
    /* lock inc qword [r11] */
    t[pos++] = 0xF0; t[pos++] = 0x49; t[pos++] = 0xFF; t[pos++] = 0x03;
    /* mov r11, real_fn */
    t[pos++] = 0x49; t[pos++] = 0xBB;
    memcpy(t + pos, &real_fn, 8); pos += 8;
    /* jmp r11 (pure tail-call: zero stack impact, ABI compliant, preserves all args & return addresses) */
    t[pos++] = 0x41; t[pos++] = 0xFF; t[pos++] = 0xE3;

    for (int k = pos; k < STUB_BYTES; ++k) t[k] = 0xCC;

#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), t, STUB_BYTES);
#endif
}

uintptr_t runtime_counter_wrap(const char *nid, uintptr_t real_fn) {
    if (nid && real_fn) real_fn=runtime_ngs2_trace_wrap(resolve_human_name(nid),real_fn);
    if (g_no_counters) return real_fn;
    if (!stub_mem || !nid || !real_fn) return real_fn;

    int64_t idx = find_or_create(nid, real_fn, 0, 0);
    if (idx < 0) return real_fn;

    unsigned char *t = stub_mem + (uint64_t)idx * STUB_BYTES;
    emit_stub(t, idx, real_fn);
    return (uintptr_t)t;
}

uintptr_t runtime_counter_wrap_stub(const char *nid, uintptr_t real_fn) {
    if (nid && real_fn) real_fn=runtime_ngs2_trace_wrap(resolve_human_name(nid),real_fn);
    if (g_no_counters) return real_fn;
    if (!stub_mem || !nid || !real_fn) return real_fn;

    int64_t idx = find_or_create(nid, real_fn, 0, 1);
    if (idx < 0) return real_fn;

    unsigned char *t = stub_mem + (uint64_t)idx * STUB_BYTES;
    emit_stub(t, idx, real_fn);
    return (uintptr_t)t;
}

void runtime_counter_record_data(const char *nid, uintptr_t real_fn) {
    if (g_no_counters) return;
    if (!stub_mem || !nid || !real_fn) return;
    find_or_create(nid, real_fn, 1, 0);
}

typedef struct {
    uint64_t idx;
    uint64_t delta;
} DeltaItem;

static int cmp_delta(const void *a, const void *b) {
    uint64_t da = ((const DeltaItem *)a)->delta;
    uint64_t db = ((const DeltaItem *)b)->delta;
    return (da > db) ? -1 : (da < db) ? 1 : 0;
}

static int is_target_subsystem(const char *nid, const char *sym) {
    if (!nid && !sym) return 0;
    const char *targets[] = {
        "SaveData", "libSceSaveData", "sceSaveData",
        "libSceNp", "sceNp",
        "SystemService", "libSceSystemService", "sceSystemService",
        "MsgDialog", "libSceMsgDialog", "sceMsgDialog",
        "AvPlayer", "libSceAvPlayer", "sceAvPlayer",
        "Fios2", "libSceFios2", "fios",
        "UserService", "libSceUserService", "sceUserService",
        "libScePad", "scePad"
    };
    for (size_t i = 0; i < sizeof(targets)/sizeof(*targets); ++i) {
        if ((nid && strstr(nid, targets[i])) || (sym && strstr(sym, targets[i])))
            return 1;
    }
    return 0;
}

static void print_return_hist(CounterSlot *s) {
    uint64_t total = atomic_load(&s->ret_total);
    if (total == 0) {
        fprintf(stderr, "     returns: (no calls or tail-jumped)\n");
        return;
    }
    fprintf(stderr, "     returns: [total=%llu tracked] ", (unsigned long long)total);
    int printed = 0;
    for (int h = 0; h < RET_HIST_SIZE; ++h) {
        uint64_t cnt = atomic_load(&s->ret_hist[h].count);
        if (cnt > 0) {
            double pct = (double)cnt * 100.0 / (double)total;
            int64_t val = (int64_t)s->ret_hist[h].val;
            fprintf(stderr, "%s0x%llx (%lld): %llu (%.1f%%)",
                    printed > 0 ? ", " : "",
                    (unsigned long long)val, (long long)val, (unsigned long long)cnt, pct);
            ++printed;
        }
    }
    uint64_t other = atomic_load(&s->ret_other);
    if (other > 0) {
        double pct = (double)other * 100.0 / (double)total;
        fprintf(stderr, "%sother: %llu (%.1f%%)", printed > 0 ? ", " : "", (unsigned long long)other, pct);
    }
    fprintf(stderr, "\n");
}

void runtime_counter_dump_stall_profile(uint32_t window_ms) {
    if (g_no_counters || slot_count == 0) return;

    uint64_t *s1 = (uint64_t *)malloc(slot_count * sizeof(uint64_t));
    if (!s1) return;
    for (uint64_t i = 0; i < slot_count; ++i) {
        s1[i] = atomic_load(&slots[i].count);
    }

    fprintf(stderr, "\n=== [STALL PROFILE] Snapshot 1 taken (%llu imports). Waiting %u ms for delta profiling... ===\n",
            (unsigned long long)slot_count, window_ms);
    fflush(stderr);

    Sleep(window_ms);

    DeltaItem *deltas = (DeltaItem *)malloc(slot_count * sizeof(DeltaItem));
    if (!deltas) { free(s1); return; }

    for (uint64_t i = 0; i < slot_count; ++i) {
        uint64_t c2 = atomic_load(&slots[i].count);
        deltas[i].idx = i;
        deltas[i].delta = (c2 >= s1[i]) ? (c2 - s1[i]) : 0;
    }

    qsort(deltas, slot_count, sizeof(DeltaItem), cmp_delta);

    double sec = (double)window_ms / 1000.0;
    fprintf(stderr, "\n=== Stall-Window Import Profile (%.1fs window) ===\n", sec);
    fprintf(stderr, "--- Top 30 imports by call DELTA in stall window ---\n");
    uint32_t printed = 0;
    for (uint64_t i = 0; i < slot_count && printed < 30; ++i) {
        if (slots[deltas[i].idx].is_data) continue;
        uint64_t d = deltas[i].delta;
        uint64_t lifetime = atomic_load(&slots[deltas[i].idx].count);
        fprintf(stderr, "  #%02u: %10llu calls (%8.1f/s) | %-42s [total: %10llu]%s\n",
                printed + 1, (unsigned long long)d, (double)d / sec,
                slots[deltas[i].idx].sym, (unsigned long long)lifetime,
                slots[deltas[i].idx].is_stub ? " [stub]" : "");
        ++printed;
    }

    fprintf(stderr, "\n--- Return Value Histograms (Top 15 delta + Target Subsystems) ---\n");
    int *marked = (int *)calloc(slot_count, sizeof(int));
    for (uint64_t i = 0; i < 15 && i < slot_count; ++i) {
        uint64_t idx = deltas[i].idx;
        marked[idx] = 1;
        fprintf(stderr, "  [%02u] %-45s | delta: %llu, total: %llu%s\n",
                (unsigned)i + 1, slots[idx].sym,
                (unsigned long long)deltas[i].delta,
                (unsigned long long)atomic_load(&slots[idx].count),
                slots[idx].is_stub ? " [stub]" : "");
        print_return_hist(&slots[idx]);
    }

    fprintf(stderr, "--- Target Subsystems (SaveData, Np, SystemService, MsgDialog, AvPlayer, Fios2, UserService, Pad) ---\n");
    for (uint64_t i = 0; i < slot_count; ++i) {
        if (marked[i]) continue;
        if (!is_target_subsystem(slots[i].nid, slots[i].sym)) continue;
        uint64_t cnt = atomic_load(&slots[i].count);
        uint64_t d = 0;
        for (uint64_t k = 0; k < slot_count; ++k) {
            if (deltas[k].idx == i) { d = deltas[k].delta; break; }
        }
        if (cnt == 0 && d == 0) continue;
        fprintf(stderr, "  [SYS] %-45s | delta: %llu, total: %llu%s\n",
                slots[i].sym, (unsigned long long)d, (unsigned long long)cnt,
                slots[i].is_stub ? " [stub]" : "");
        print_return_hist(&slots[i]);
    }

    free(marked);
    free(deltas);
    free(s1);
    fprintf(stderr, "=== End Stall-Window Profile ===\n\n");
    fflush(stderr);
}

static int cmp_desc(const void *a, const void *b) {
    uint64_t ca = atomic_load(&((const CounterSlot *)a)->count);
    uint64_t cb = atomic_load(&((const CounterSlot *)b)->count);
    return (ca > cb) ? -1 : (ca < cb) ? 1 : 0;
}

void runtime_counter_report(void) {
    if (g_no_counters) {
        puts("Counter: LIAN_NO_COUNTERS=1 was active; no import counts gathered");
        return;
    }
    if (!slot_count) {
        puts("Counter: no imports were wrapped");
        return;
    }

    CounterSlot *sorted = (CounterSlot *)malloc(slot_count * sizeof(CounterSlot));
    if (!sorted) return;
    memcpy(sorted, slots, slot_count * sizeof(CounterSlot));
    qsort(sorted, slot_count, sizeof(CounterSlot), cmp_desc);

    printf("\n=== Import call counters (%llu unique imports) ===\n",
           (unsigned long long)slot_count);

    /* Top 40 function imports */
    puts("--- Top 40 function imports by call count ---");
    uint64_t printed = 0;
    for (uint64_t i = 0; i < slot_count && printed < 40; ++i) {
        if (sorted[i].is_data) continue;
        printf("  %10llu  %s%s\n",
               (unsigned long long)atomic_load(&sorted[i].count),
               sorted[i].sym,
               sorted[i].is_stub ? " [stub-bound]" : "");
        ++printed;
    }

    /* Top 20 stub-bound imports */
    puts("\n--- Top 20 stub-bound imports by call count ---");
    printed = 0;
    for (uint64_t i = 0; i < slot_count && printed < 20; ++i) {
        if (!sorted[i].is_stub || sorted[i].is_data) continue;
        printf("  %10llu  %s\n",
               (unsigned long long)atomic_load(&sorted[i].count),
               sorted[i].sym);
        ++printed;
    }
    if (printed == 0) puts("  (none called)");

    /* libSceNgs2 */
    puts("\n--- libSceNgs2* imports ---");
    int found = 0;
    for (uint64_t i = 0; i < slot_count; ++i) {
        if (!strstr(slots[i].nid, "libSceNgs2") && !strstr(slots[i].sym, "sceNgs2"))
            continue;
        uint64_t cnt = atomic_load(&slots[i].count);
        printf("  %10llu  %-50s  [%s, %s]\n",
               (unsigned long long)cnt, slots[i].sym,
               slots[i].is_stub ? "generic stub" : "real handler",
               cnt > 0 ? "CALLED" : "never called");
        found = 1;
    }
    if (!found) puts("  (none)");

    /* libSceFont / libSceFontFt */
    puts("\n--- libSceFont* and libSceFontFt* imports ---");
    found = 0;
    for (uint64_t i = 0; i < slot_count; ++i) {
        if (!strstr(slots[i].nid, "libSceFont") && !strstr(slots[i].sym, "sceFont"))
            continue;
        uint64_t cnt = atomic_load(&slots[i].count);
        printf("  %10llu  %-50s  [%s, %s]\n",
               (unsigned long long)cnt, slots[i].sym,
               slots[i].is_stub ? "generic stub" : "real handler",
               cnt > 0 ? "CALLED" : "never called");
        found = 1;
    }
    if (!found) puts("  (none)");

    free(sorted);
    puts("=== End import call counters ===\n");
    fflush(stdout);
}
