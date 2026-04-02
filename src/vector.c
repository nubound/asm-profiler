#include "asm_profiler.h"

#include <stdlib.h>
#include <string.h>

static int ap_grow(void **buffer, size_t *cap, size_t elem_size) {
    size_t next = (*cap == 0) ? 16 : (*cap * 2);
    void *grown = ap_reallocarray(*buffer, next, elem_size);
    if (!grown) {
        return -1;
    }

    *buffer = grown;
    *cap = next;
    return 0;
}

void ap_ip_vec_destroy(ap_ip_vec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

int ap_ip_vec_increment(ap_ip_vec *vec, uint64_t ip) {
    for (size_t i = 0; i < vec->len; ++i) {
        if (vec->items[i].ip == ip) {
            vec->items[i].samples += 1;
            return 0;
        }
    }

    if (vec->len == vec->cap && ap_grow((void **) &vec->items, &vec->cap, sizeof(*vec->items)) != 0) {
        return -1;
    }

    vec->items[vec->len].ip = ip;
    vec->items[vec->len].samples = 1;
    vec->len += 1;
    return 0;
}

void ap_hot_symbol_vec_destroy(ap_hot_symbol_vec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

int ap_hot_symbol_vec_push(ap_hot_symbol_vec *vec, const ap_hot_symbol *item) {
    if (vec->len == vec->cap && ap_grow((void **) &vec->items, &vec->cap, sizeof(*vec->items)) != 0) {
        return -1;
    }

    vec->items[vec->len] = *item;
    vec->len += 1;
    return 0;
}
