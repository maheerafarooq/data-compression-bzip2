#include "bzip2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char *g_bwt_input = NULL;
static size_t g_bwt_len = 0;

int compare_rotations(const void *a, const void *b) {
    const Rotation *ra = (const Rotation *)a;
    const Rotation *rb = (const Rotation *)b;

    size_t n  = g_bwt_len;
    int    ia = ra->index;
    int    ib = rb->index;

    for (size_t k = 0; k < n; k++) {
        unsigned char ca = g_bwt_input[(ia + (int)k) % n];
        unsigned char cb = g_bwt_input[(ib + (int)k) % n];
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    return 0;
}

void bwt_encode(unsigned char *input, size_t len,
                unsigned char *output, int *primary_index) {
    if (!input || !output || !primary_index || len == 0) return;

    g_bwt_input = input;
    g_bwt_len   = len;

    Rotation *rotations = (Rotation *)malloc(len * sizeof(Rotation));
    if (!rotations) {
        fprintf(stderr, "Error: BWT encode - malloc failed\n");
        return;
    }

    for (size_t i = 0; i < len; i++) {
        rotations[i].rotation = NULL;
        rotations[i].index    = (int)i;
    }

    qsort(rotations, len, sizeof(Rotation), compare_rotations);

    *primary_index = -1;
    for (size_t i = 0; i < len; i++) {
        output[i] = input[(size_t)(rotations[i].index + (int)len - 1) % len];
        if (rotations[i].index == 0) {
            *primary_index = (int)i;
        }
    }

    free(rotations);
}

void bwt_decode(unsigned char *input, size_t len,
                int primary_index, unsigned char *output) {
    if (!input || !output || len == 0 || primary_index < 0) return;

    int freq[256] = {0};
    for (size_t i = 0; i < len; i++) {
        freq[(unsigned char)input[i]]++;
    }

    int starts[256] = {0};
    int total = 0;
    for (int c = 0; c < 256; c++) {
        starts[c] = total;
        total += freq[c];
    }

    unsigned char *F = (unsigned char *)malloc(len);
    if (!F) {
        fprintf(stderr, "Error: BWT decode - malloc F failed\n");
        return;
    }
    {
        int cnt[256] = {0};
        for (size_t i = 0; i < len; i++) {
            cnt[input[i]]++;
        }
        size_t p = 0;
        for (int c = 0; c < 256; c++) {
            for (int k = 0; k < cnt[c]; k++) {
                F[p++] = (unsigned char)c;
            }
        }
    }

    int *next = (int *)malloc(len * sizeof(int));
    if (!next) {
        fprintf(stderr, "Error: BWT decode - malloc next failed\n");
        free(F);
        return;
    }

    int pos[256];
    memcpy(pos, starts, sizeof(starts));

    for (size_t j = 0; j < len; j++) {
        unsigned char c = input[j];
        next[pos[c]] = (int)j;
        pos[c]++;
    }

    int idx = primary_index;
    for (size_t i = 0; i < len; i++) {
        output[i] = F[idx];
        idx = next[idx];
    }

    free(F);
    free(next);
}

/* =========================================================
 * Extra feature 8.2: fast suffix-array BWT for cyclic rotations
 *
 * Implementation: prefix doubling (Manber–Myers style) over the
 * cyclic string, with one qsort per doubling pass. This makes the
 * suffix-array BWT O(n log^2 n) instead of the O(n^2 log n) naive
 * compare in the matrix variant, so 100KB–900KB blocks finish in
 * milliseconds and large benchmark files complete quickly.
 * ========================================================= */
static int *g_sa_rank = NULL;
static int  g_sa_n_pd = 0;
static int  g_sa_k_pd = 0;

static int compare_doubled(const void *pa, const void *pb) {
    int a = *(const int *)pa;
    int b = *(const int *)pb;
    int ra = g_sa_rank[a];
    int rb = g_sa_rank[b];
    if (ra != rb) {
        return ra < rb ? -1 : 1;
    }
    int a2 = a + g_sa_k_pd;
    int b2 = b + g_sa_k_pd;
    if (a2 >= g_sa_n_pd) a2 -= g_sa_n_pd;
    if (b2 >= g_sa_n_pd) b2 -= g_sa_n_pd;
    int ra2 = g_sa_rank[a2];
    int rb2 = g_sa_rank[b2];
    if (ra2 != rb2) {
        return ra2 < rb2 ? -1 : 1;
    }
    return 0;
}

int *build_suffix_array(unsigned char *text, int n) {
    if (!text || n <= 0) {
        return NULL;
    }
    int *sa   = (int *)malloc((size_t)n * sizeof(int));
    int *rank = (int *)malloc((size_t)n * sizeof(int));
    int *tmp  = (int *)malloc((size_t)n * sizeof(int));
    if (!sa || !rank || !tmp) {
        free(sa);
        free(rank);
        free(tmp);
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        sa[i]   = i;
        rank[i] = (int)text[i];
    }
    g_sa_rank = rank;
    g_sa_n_pd = n;

    for (int k = 1;; k *= 2) {
        g_sa_k_pd = k;
        qsort(sa, (size_t)n, sizeof(int), compare_doubled);
        tmp[sa[0]] = 0;
        for (int i = 1; i < n; i++) {
            int prev = sa[i - 1];
            int cur  = sa[i];
            int eq   = (rank[prev] == rank[cur]);
            if (eq) {
                int p2 = prev + k;
                int c2 = cur + k;
                if (p2 >= n) p2 -= n;
                if (c2 >= n) c2 -= n;
                eq = (rank[p2] == rank[c2]);
            }
            tmp[cur] = tmp[prev] + (eq ? 0 : 1);
        }
        memcpy(rank, tmp, (size_t)n * sizeof(int));
        if (rank[sa[n - 1]] == n - 1) {
            break;
        }
        if (k >= n) {
            break;
        }
    }
    free(rank);
    free(tmp);
    g_sa_rank = NULL;
    return sa;
}

void bwt_encode_suffix_array(unsigned char *input, size_t len,
                             unsigned char *output, int *primary_index) {
    if (!input || !output || !primary_index || len == 0) return;
    int *sa = build_suffix_array(input, (int)len);
    if (!sa) {
        *primary_index = -1;
        return;
    }
    *primary_index = -1;
    for (size_t i = 0; i < len; i++) {
        int idx = sa[i];
        output[i] = input[(idx + (int)len - 1) % (int)len];
        if (idx == 0) *primary_index = (int)i;
    }
    free(sa);
}
