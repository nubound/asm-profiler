#include "asm_profiler.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool ap_mul_overflow(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }

    if (a > SIZE_MAX / b) {
        return true;
    }

    *out = a * b;
    return false;
}

void *ap_callocarray(size_t nmemb, size_t size) {
    size_t total = 0;

    if (ap_mul_overflow(nmemb, size, &total)) {
        return NULL;
    }

    return calloc(1, total);
}

void *ap_reallocarray(void *ptr, size_t nmemb, size_t size) {
    size_t total = 0;

    if (ap_mul_overflow(nmemb, size, &total)) {
        return NULL;
    }

    return realloc(ptr, total);
}

char *ap_strdup(const char *value) {
    size_t len = strlen(value) + 1;
    char *copy = malloc(len);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len);
    return copy;
}

uint64_t ap_now_millis(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((uint64_t) ts.tv_sec * 1000ULL) + ((uint64_t) ts.tv_nsec / 1000000ULL);
}
