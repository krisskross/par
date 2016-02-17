// SPRUNE :: https://github.com/prideout/par
// Sweep and Prune library for detecting axis-aligned box collisions in 2D.
//
// In addition to the comment block above each function declaration, the API
// has informal documentation here:
//
//     http://github.prideout.net/work-in-progress/
//
// The MIT License
// Copyright (c) 2015 Philip Rideout

#ifndef PAR_SPRUNE_H
#define PAR_SPRUNE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#ifndef PAR_SPRUNE_INT
#define PAR_SPRUNE_INT int32_t
#endif

#ifndef PAR_SPRUNE_FLT
#define PAR_SPRUNE_FLT float
#endif

// -----------------------------------------------------------------------------
// BEGIN PUBLIC API
// -----------------------------------------------------------------------------

typedef struct {
    PAR_SPRUNE_INT const* const collision_pairs; // list of two-tuples
    PAR_SPRUNE_INT const ncollision_pairs;       // number of two-tuples
    PAR_SPRUNE_INT const* const culled;          // filled by par_sprune_cull
    PAR_SPRUNE_INT const nculled;                // set by par_sprune_cull
} par_sprune_context;

void par_sprune_free_context(par_sprune_context* context);

// Takes an array of 4-tuples (minx miny maxx maxy) and performs SaP. Populates
// "collision_pairs" and "ncollision_pairs".  Optionally takes an existing
// context to avoid memory churn; pass NULL for initial construction.
par_sprune_context* par_sprune_overlap(PAR_SPRUNE_FLT const* aabbs,
    PAR_SPRUNE_INT naabbs, par_sprune_context* previous);

// Reads new aabb data from the same pointer that was passed to the overlap
// function and refreshes the "collision_pairs" field.  Exploits temporal
// coherence so it's very efficient for animation.  If this returns false,
// no changes to the collision set were detected.
bool par_sprune_update(par_sprune_context* ctx);

// Examines all collision groups and creates a culling set such that no
// boxes would overlap if the culled boxes are removed.  This function
// populates the "culled" and "nculled" fields in par_sprune_context.
// This is useful for hiding labels in GIS applications.
void par_sprune_cull(par_sprune_context* context);

// -----------------------------------------------------------------------------
// END PUBLIC API
// -----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#ifdef PAR_SPRUNE_IMPLEMENTATION
#define PARINT PAR_SPRUNE_INT
#define PARFLT PAR_SPRUNE_FLT

#include <stdlib.h>
#include <assert.h>

#ifndef PAR_PI
#define PAR_PI (3.14159265359)
#define PAR_MIN(a, b) (a > b ? b : a)
#define PAR_MAX(a, b) (a > b ? a : b)
#define PAR_CLAMP(v, lo, hi) PAR_MAX(lo, PAR_MIN(hi, v))
#define PAR_SWAP(T, A, B) { T tmp = B; B = A; A = tmp; }
#define PAR_SQR(a) ((a) * (a))
#endif

#ifndef PAR_MALLOC
#define PAR_MALLOC(T, N) ((T*) malloc(N * sizeof(T)))
#define PAR_CALLOC(T, N) ((T*) calloc(N * sizeof(T), 1))
#define PAR_REALLOC(T, BUF, N) ((T*) realloc(BUF, sizeof(T) * (N)))
#define PAR_FREE(BUF) free(BUF)
#endif

#ifndef PAR_ARRAY
#define pa_free(a) ((a) ? PAR_FREE(pa___raw(a)), 0 : 0)
#define pa_push(a, v) (pa___maybegrow(a, 1), (a)[pa___n(a)++] = (v))
#define pa_count(a) ((a) ? pa___n(a) : 0)
#define pa_add(a, n) (pa___maybegrow(a, n), pa___n(a) += (n))
#define pa_last(a) ((a)[pa___n(a) - 1])
#define pa_end(a) (a + pa_count(a))
#define pa_clear(arr) if (arr) pa___n(arr) = 0
#define pa___raw(a) ((int*) (a) -2)
#define pa___m(a) pa___raw(a)[0]
#define pa___n(a) pa___raw(a)[1]
#define pa___needgrow(a, n) ((a) == 0 || pa___n(a) + (n) >= pa___m(a))
#define pa___maybegrow(a, n) (pa___needgrow(a, (n)) ? pa___grow(a, n) : 0)
#define pa___grow(a, n) (*((void**)& (a)) = pa___growf((void*) (a), (n), \
        sizeof(*(a))))

static void* pa___growf(void* arr, int increment, int itemsize)
{
    int dbl_cur = arr ? 2 * pa___m(arr) : 0;
    int min_needed = pa_count(arr) + increment;
    int m = dbl_cur > min_needed ? dbl_cur : min_needed;
    int* p = PAR_REALLOC(int, arr ? pa___raw(arr) : 0,
        itemsize * m / sizeof(int) + 2);
    if (p) {
        if (!arr) {
            p[1] = 0;
        }
        p[0] = m;
        return p + 2;
    }
    return (void*) (2 * sizeof(int));
}

#endif

typedef struct {

    // Public:
    PARINT* collision_pairs;
    PARINT ncollision_pairs;
    PARINT* culled;
    PARINT nculled;

    // Private:
    PARFLT const* aabbs;
    PARINT naabbs;
    PARINT* sorted_indices[2];
    bool* overlap_flags[2];

} par_sprune__context;

static inline int par_qsort_cmpswap(char *__restrict a, char *__restrict b,
    size_t w,
    int (*compar)(const void *_a, const void *_b,
    void *_arg),
    void *arg)
{
    char tmp, *end = a+w;
    if (compar(a, b, arg) > 0) {
        for(; a < end; a++, b++) { tmp = *a; *a = *b; *b = tmp; }
        return 1;
    }
    return 0;
}

// qsort doesn't take a context, so we have our own portable implementation.
// Parameters:
//     base is the array to be sorted
//     nel is the number of elements in the array
//     w is the size in bytes of each element of the array
//     compar is the comparison function
//     arg is a pointer to be passed to the comparison function
//
static inline void par_qsort(
    void *base,
    size_t nel,
    size_t w,
    int (*compar)(const void *_a, const void *_b, void *_arg),
    void *arg)
{
    char *b = (char*) base, *end = (char*) (b + nel * w);
    if (nel < 7) {
        char *pi, *pj;
        for (pi = b+w; pi < end; pi += w) {
            for (pj = pi; pj > b && par_qsort_cmpswap(pj-w, pj, w, compar, arg);
                pj -= w) {}
        }
        return;
    }
    char *x, *y, *xend, ch;
    char *pl, *pr;
    char *last = b+w*(nel-1), *tmp;
    char *l[3];
    l[0] = b;
    l[1] = b+w*(nel/2);
    l[2] = last;
    if (compar(l[0],l[1],arg) > 0) {
        tmp=l[0]; l[0]=l[1]; l[1]=tmp;
    }
    if (compar(l[1],l[2],arg) > 0) {
        tmp=l[1]; l[1]=l[2]; l[2]=tmp;
        if (compar(l[0],l[1],arg) > 0) {
            tmp=l[0]; l[0]=l[1]; l[1]=tmp;
        }
    }
    for(x = l[1], y = last, xend = x+w; x<xend; x++, y++) {
        ch = *x; *x = *y; *y = ch;
    }
    pl = b;
    pr = last;
    while (pl < pr) {
        for (; pl < pr; pl += w) {
            if (par_qsort_cmpswap(pl, pr, w, compar, arg)) {
                pr -= w;
                break;
            }
        }
        for (; pl < pr; pr -= w) {
            if (par_qsort_cmpswap(pl, pr, w, compar, arg)) {
                pl += w;
                break;
            }
        }
    }
    par_qsort(b, (pl-b) / w, w, compar, arg);
    par_qsort(pl+w, (end - (pl+w)) / w, w, compar, arg);
}

void par_sprune_free_context(par_sprune_context* context)
{
    par_sprune__context* ctx = (par_sprune__context*) context;
    pa_free(ctx->sorted_indices[0]);
    pa_free(ctx->sorted_indices[1]);
    pa_free(ctx->overlap_flags[0]);
    pa_free(ctx->overlap_flags[1]);
    pa_free(ctx->collision_pairs);
    PAR_FREE(ctx);
}

static void par_sprune__remove(PARINT* arr, PARINT val)
{
    int i = pa_count(arr) - 1;
    for (; i >= 0; i--) {
        if (arr[i] == val) {
            break;
        }
    }
    assert(i >= 0);
    for (++i; i < pa_count(arr); i++) {
        PAR_SWAP(PARINT, arr[i - 1], arr[i]);
    }
    pa___n(arr)--;
}

typedef struct {
    PARFLT const* aabbs;
} par__sprune_sorter;

int par__cmpinds(const void* pa, const void* pb, void* psorter)
{
    PARINT a = *((const PARINT*) pa);
    PARINT b = *((const PARINT*) pb);
    par__sprune_sorter* sorter = (par__sprune_sorter*) psorter;
    PARFLT const* aabbs = sorter->aabbs;
    PARFLT vala = aabbs[a];
    PARFLT valb = aabbs[b];
    if (vala > valb) return 1;
    if (vala < valb) return -1;
    if (a > b) return 1;
    if (a < b) return -1;
    return 0;
}

int par__cmppairs(const void* pa, const void* pb, void* unused)
{
    PARINT a = *((const PARINT*) pa);
    PARINT b = *((const PARINT*) pb);
    if (a > b) return 1;
    if (a < b) return -1;
    a = *(1 + (const PARINT*) pa);
    b = *(1 + (const PARINT*) pb);
    if (a > b) return 1;
    if (a < b) return -1;
    return 0;
}

int par__cmpfind(const void* pa, const void* pb)
{
    PARINT a = *((const PARINT*) pa);
    PARINT b = *((const PARINT*) pb);
    if (a > b) return 1;
    if (a < b) return -1;
    a = *(1 + (const PARINT*) pa);
    b = *(1 + (const PARINT*) pb);
    if (a > b) return 1;
    if (a < b) return -1;
    return 0;
}

par_sprune_context* par_sprune_overlap(PARFLT const* aabbs, PARINT naabbs,
    par_sprune_context* previous)
{
    par_sprune__context* ctx = (par_sprune__context*) previous;
    if (!ctx) {
        ctx = PAR_CALLOC(par_sprune__context, 1);
    }
    ctx->aabbs = aabbs;
    ctx->naabbs = naabbs;
    for (int axis = 0; axis < 2; axis++) {
        pa_clear(ctx->sorted_indices[axis]);
        pa_add(ctx->sorted_indices[axis], naabbs * 2);
        pa_clear(ctx->overlap_flags[axis]);
        pa_add(ctx->overlap_flags[axis], naabbs * 2);
    }
    for (PARINT i = 0; i < naabbs; i++) {
        ctx->sorted_indices[0][i * 2 + 0] = i * 4 + 0;
        ctx->sorted_indices[1][i * 2 + 0] = i * 4 + 1;
        ctx->sorted_indices[0][i * 2 + 1] = i * 4 + 2;
        ctx->sorted_indices[1][i * 2 + 1] = i * 4 + 3;
        ctx->overlap_flags[0][i] = false;
        ctx->overlap_flags[1][i] = false;
    }
    par__sprune_sorter sorter;
    sorter.aabbs = ctx->aabbs;
    PARINT* active = 0;
    PARINT* pairs[2] = {0};

    for (int axis = 0; axis < 2; axis++) {
        PARINT* indices = ctx->sorted_indices[axis];
        par_qsort(indices, naabbs * 2, sizeof(PARINT), par__cmpinds, &sorter);
        pa_clear(active);
        for (PARINT i = 0; i < naabbs * 2; i++) {
            PARINT fltindex = indices[i];
            PARINT boxindex = fltindex / 4;
            bool ismin = ((fltindex - axis) % 4) == 0;
            if (ismin) {
                for (int j = 0; j < pa_count(active); j++) {
                    PARINT a = PAR_MIN(active[j], boxindex);
                    PARINT b = PAR_MAX(active[j], boxindex);
                    pa_push(pairs[axis], a);
                    pa_push(pairs[axis], b);
                }
                pa_push(active, boxindex);
            } else {
                par_sprune__remove(active, boxindex);
            }
        }
    }

    pa_free(active);
    par_qsort(pairs[0], pa_count(pairs[0]) / 2, 2 * sizeof(PARINT),
        par__cmppairs, 0);
    par_qsort(pairs[1], pa_count(pairs[1]) / 2, 2 * sizeof(PARINT),
        par__cmppairs, 0);
    pa_clear(ctx->collision_pairs);
    for (int i = 0; i < pa_count(pairs[0]); i += 2) {
        PARINT* key = pairs[0] + i;
        void* found = bsearch(key, pairs[1], pa_count(pairs[1]) / 2,
            sizeof(PARINT) * 2, par__cmpfind);
        if (found) {
            pa_push(ctx->collision_pairs, key[0]);
            pa_push(ctx->collision_pairs, key[1]);
        }
    }
    ctx->ncollision_pairs = pa_count(ctx->collision_pairs) / 2;
    pa_free(pairs[0]);
    pa_free(pairs[1]);
    return (par_sprune_context*) ctx;
}

#undef PARINT
#undef PARFLT
#endif // PAR_SPRUNE_IMPLEMENTATION
#endif // PAR_SPRUNE_H
