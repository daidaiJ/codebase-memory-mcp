/*
 * mem_core.c — the allocation route. See mem_core.h for why it exists.
 */
#include "foundation/mem_core.h"

#include "foundation/constants.h"
#include "foundation/log.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Usable-size query, per platform.
 *
 * Deliberately NOT mi_usable_size: the mimalloc global override is off on
 * macOS (permanently — the two-level namespace aborts on cross-boundary
 * frees), so a pointer from plain malloc there is not a mimalloc block and
 * mi_usable_size would be undefined behaviour on it. Each platform's own query
 * is correct under whichever allocator is actually installed, including when
 * that allocator IS mimalloc via the Linux/MinGW override. */
#if defined(__APPLE__)
#include <malloc/malloc.h> /* malloc_size */
#define CBM_USABLE_SIZE(p) malloc_size(p)
#elif defined(_WIN32)
#include <malloc.h> /* _msize */
#define CBM_USABLE_SIZE(p) _msize((void *)(p))
#elif defined(__GLIBC__) || defined(__linux__)
#include <malloc.h> /* malloc_usable_size */
#define CBM_USABLE_SIZE(p) malloc_usable_size((void *)(p))
#else
/* BSD and anything unknown: no portable query. Accounting then tracks the
 * REQUESTED size, which understates by the rounding. Understating is the safe
 * direction for a diagnostic (it never invents memory), and the alternative --
 * a per-block header -- costs 1.6 GB at kernel scale. */
#define CBM_USABLE_SIZE_UNAVAILABLE 1
#endif

enum { MEM_CORE_REPORT_MIN = 64 };

typedef struct {
    atomic_size_t live_bytes;
    atomic_size_t live_blocks;
    atomic_size_t peak_bytes;
} mem_class_stats_t;

static mem_class_stats_t g_classes[CBM_MEM_CLASS_COUNT];

static const char *const g_class_names[CBM_MEM_CLASS_COUNT] = {
    "other",   "gbuf_node", "gbuf_edge", "gbuf_string", "gbuf_index",
    "extract", "semantic",  "dump",      "store",
};

const char *cbm_mem_class_name(cbm_mem_class_t cls) {
    if ((int)cls < 0 || (int)cls >= CBM_MEM_CLASS_COUNT) {
        return "invalid";
    }
    return g_class_names[cls];
}

/* Out-of-range classes are folded into OTHER rather than rejected: a
 * mis-tagged allocation must still be freed correctly. Accounting accuracy is
 * worth less than not corrupting the heap. */
static mem_class_stats_t *class_slot(cbm_mem_class_t cls) {
    if ((int)cls < 0 || (int)cls >= CBM_MEM_CLASS_COUNT) {
        return &g_classes[CBM_MEM_CLASS_OTHER];
    }
    return &g_classes[cls];
}

static void class_add(cbm_mem_class_t cls, size_t bytes, size_t blocks) {
    mem_class_stats_t *st = class_slot(cls);
    size_t now = atomic_fetch_add_explicit(&st->live_bytes, bytes, memory_order_relaxed) + bytes;
    if (blocks) {
        (void)atomic_fetch_add_explicit(&st->live_blocks, blocks, memory_order_relaxed);
    }
    /* Peak is best-effort under concurrency: a CAS loop here would serialise
     * every allocation in the hot path to make a DIAGNOSTIC exact. Racing
     * writers can leave the peak one increment low; that never changes a
     * decision, and the cost of exactness would. */
    size_t seen = atomic_load_explicit(&st->peak_bytes, memory_order_relaxed);
    while (now > seen) {
        if (atomic_compare_exchange_weak_explicit(&st->peak_bytes, &seen, now, memory_order_relaxed,
                                                  memory_order_relaxed)) {
            break;
        }
    }
}

static void class_sub(cbm_mem_class_t cls, size_t bytes, size_t blocks) {
    mem_class_stats_t *st = class_slot(cls);
    /* Never wrap. A mismatched class on free (the one way a caller can get
     * this wrong) would otherwise turn a small drift into a colossal bogus
     * number that looks like a leak and sends someone hunting a phantom. */
    size_t seen = atomic_load_explicit(&st->live_bytes, memory_order_relaxed);
    while (seen > 0) {
        size_t want = bytes > seen ? 0 : seen - bytes;
        if (atomic_compare_exchange_weak_explicit(&st->live_bytes, &seen, want,
                                                  memory_order_relaxed, memory_order_relaxed)) {
            break;
        }
    }
    if (blocks) {
        size_t b = atomic_load_explicit(&st->live_blocks, memory_order_relaxed);
        while (b > 0) {
            if (atomic_compare_exchange_weak_explicit(&st->live_blocks, &b, b - 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {
                break;
            }
        }
    }
}

/* Charge what the allocator actually handed us where the platform can say, and
 * fall back to the request otherwise. Called once per successful allocation.
 * Two definitions rather than one with a constant-folded branch: on a platform
 * with no usable-size query the fallback simply IS the request, and cppcheck
 * rightly objects to a condition that can never be true. */
#ifdef CBM_USABLE_SIZE_UNAVAILABLE
static size_t charge_size(const void *block, size_t requested) {
    (void)block;
    return requested;
}

size_t cbm_mem_usable_size(const void *block) {
    (void)block;
    return 0;
}
#else
static size_t charge_size(const void *block, size_t requested) {
    size_t usable = CBM_USABLE_SIZE(block);
    return usable ? usable : requested;
}

size_t cbm_mem_usable_size(const void *block) {
    if (!block) {
        return 0;
    }
    return CBM_USABLE_SIZE(block);
}
#endif

void *cbm_alloc(cbm_mem_class_t cls, size_t bytes) {
    void *block = malloc(bytes ? bytes : CBM_ALLOC_ONE);
    if (!block) {
        return NULL;
    }
    class_add(cls, charge_size(block, bytes), CBM_ALLOC_ONE);
    return block;
}

void *cbm_calloc(cbm_mem_class_t cls, size_t bytes) {
    void *block = calloc(CBM_ALLOC_ONE, bytes ? bytes : CBM_ALLOC_ONE);
    if (!block) {
        return NULL;
    }
    class_add(cls, charge_size(block, bytes), CBM_ALLOC_ONE);
    return block;
}

void *cbm_realloc(cbm_mem_class_t cls, void *block, size_t bytes) {
    if (!block) {
        return cbm_alloc(cls, bytes);
    }
    /* Measure BEFORE: after realloc the old block is gone and its size is
     * unknowable, so the decrement has to be computed first. */
    size_t old = charge_size(block, 0);
    void *next = realloc(block, bytes ? bytes : CBM_ALLOC_ONE);
    if (!next) {
        return NULL; /* original intact and still charged - correct */
    }
    class_sub(cls, old, 0);
    class_add(cls, charge_size(next, bytes), 0);
    return next;
}

char *cbm_mem_strdup(cbm_mem_class_t cls, const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + CBM_ALLOC_ONE;
    char *copy = (char *)cbm_alloc(cls, len);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

void cbm_free(cbm_mem_class_t cls, void *block) {
    if (!block) {
        return;
    }
    class_sub(cls, charge_size(block, 0), CBM_ALLOC_ONE);
    free(block);
}

void cbm_mem_class_add_external(cbm_mem_class_t cls, size_t bytes) {
    class_add(cls, bytes, 0);
}

void cbm_mem_class_remove_external(cbm_mem_class_t cls, size_t bytes) {
    class_sub(cls, bytes, 0);
}

size_t cbm_mem_class_live_bytes(cbm_mem_class_t cls) {
    return atomic_load_explicit(&class_slot(cls)->live_bytes, memory_order_relaxed);
}

size_t cbm_mem_class_live_blocks(cbm_mem_class_t cls) {
    return atomic_load_explicit(&class_slot(cls)->live_blocks, memory_order_relaxed);
}

size_t cbm_mem_class_peak_bytes(cbm_mem_class_t cls) {
    return atomic_load_explicit(&class_slot(cls)->peak_bytes, memory_order_relaxed);
}

size_t cbm_mem_tracked_live_bytes(void) {
    size_t total = 0;
    for (int i = 0; i < CBM_MEM_CLASS_COUNT; i++) {
        total += atomic_load_explicit(&g_classes[i].live_bytes, memory_order_relaxed);
    }
    return total;
}

void cbm_mem_class_reset_peaks(void) {
    for (int i = 0; i < CBM_MEM_CLASS_COUNT; i++) {
        size_t live = atomic_load_explicit(&g_classes[i].live_bytes, memory_order_relaxed);
        atomic_store_explicit(&g_classes[i].peak_bytes, live, memory_order_relaxed);
    }
}

int cbm_mem_class_report_json(char *out, size_t size) {
    if (!out || size < MEM_CORE_REPORT_MIN) {
        return 0;
    }
    int order[CBM_MEM_CLASS_COUNT];
    int n = 0;
    for (int i = 0; i < CBM_MEM_CLASS_COUNT; i++) {
        if (atomic_load_explicit(&g_classes[i].peak_bytes, memory_order_relaxed) > 0) {
            order[n++] = i;
        }
    }
    if (n == 0) {
        return 0;
    }
    /* Insertion sort: n is at most CBM_MEM_CLASS_COUNT. */
    for (int i = 1; i < n; i++) {
        int key = order[i];
        size_t kv = atomic_load_explicit(&g_classes[key].live_bytes, memory_order_relaxed);
        int j = i - 1;
        while (j >= 0 &&
               atomic_load_explicit(&g_classes[order[j]].live_bytes, memory_order_relaxed) < kv) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    int written = snprintf(out, size, "[");
    for (int i = 0; i < n && written > 0 && (size_t)written < size; i++) {
        int idx = order[i];
        int add = snprintf(out + written, size - (size_t)written,
                           "%s{\"class\":\"%s\",\"live_bytes\":%zu,\"live_blocks\":%zu,"
                           "\"peak_bytes\":%zu}",
                           i ? "," : "", g_class_names[idx],
                           atomic_load_explicit(&g_classes[idx].live_bytes, memory_order_relaxed),
                           atomic_load_explicit(&g_classes[idx].live_blocks, memory_order_relaxed),
                           atomic_load_explicit(&g_classes[idx].peak_bytes, memory_order_relaxed));
        if (add < 0 || (size_t)(written + add) >= size) {
            return 0; /* truncated: a partial JSON array is worse than none */
        }
        written += add;
    }
    if ((size_t)written + CBM_ALLOC_ONE >= size) {
        return 0;
    }
    out[written++] = ']';
    out[written] = '\0';
    return written;
}

void cbm_mem_class_log(const char *tag) {
    char report[CBM_SZ_1K];
    if (cbm_mem_class_report_json(report, sizeof(report)) <= 0) {
        return;
    }
    cbm_log_info("mem.classes", "tag", tag ? tag : "-", "classes", report);
}
