#define _GNU_SOURCE
#include "asm_profiler.h"

#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static int ap_perf_event_open(struct perf_event_attr *attr, pid_t pid) {
    return (int) syscall(SYS_perf_event_open, attr, pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

void ap_perf_sampler_init(ap_perf_sampler *sampler) {
    memset(sampler, 0, sizeof(*sampler));
    sampler->fd = -1;
}

void ap_perf_sampler_destroy(ap_perf_sampler *sampler) {
    if (sampler->mapping && sampler->mapping_len > 0) {
        munmap(sampler->mapping, sampler->mapping_len);
    }

    if (sampler->fd >= 0) {
        close(sampler->fd);
    }

    ap_perf_sampler_init(sampler);
}

int ap_perf_sampler_open(ap_perf_sampler *sampler, pid_t pid, uint64_t sample_period) {
    struct perf_event_attr attr;
    long page_size = sysconf(_SC_PAGESIZE);
    size_t data_pages = 8;

    if (page_size <= 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sample_period = sample_period;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;
    attr.disabled = 1;
    attr.enable_on_exec = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.freq = 0;
    attr.wakeup_events = 1;

    sampler->fd = ap_perf_event_open(&attr, pid);
    if (sampler->fd < 0) {
        return -1;
    }

    sampler->mapping_len = (size_t) page_size * (data_pages + 1);
    sampler->mapping = mmap(NULL, sampler->mapping_len, PROT_READ | PROT_WRITE, MAP_SHARED, sampler->fd, 0);
    if (sampler->mapping == MAP_FAILED) {
        sampler->mapping = NULL;
        ap_perf_sampler_destroy(sampler);
        return -1;
    }

    sampler->data_size = (size_t) page_size * data_pages;
    sampler->sample_period = sample_period;
    return 0;
}

int ap_perf_sampler_disable(ap_perf_sampler *sampler) {
    if (ioctl(sampler->fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
        return -1;
    }

    return 0;
}

static uint64_t ap_ring_read_head(struct perf_event_mmap_page *meta) {
    return __atomic_load_n(&meta->data_head, __ATOMIC_ACQUIRE);
}

static void ap_ring_write_tail(struct perf_event_mmap_page *meta, uint64_t tail) {
    __atomic_store_n(&meta->data_tail, tail, __ATOMIC_RELEASE);
}

static void ap_copy_from_ring(uint8_t *dst, const uint8_t *ring, size_t ring_size, uint64_t offset, size_t len) {
    size_t begin = (size_t) (offset % ring_size);
    size_t first = ring_size - begin;
    if (first > len) {
        first = len;
    }

    memcpy(dst, ring + begin, first);
    if (first < len) {
        memcpy(dst + first, ring, len - first);
    }
}

int ap_perf_sampler_drain(ap_perf_sampler *sampler, ap_ip_vec *ips) {
    struct perf_event_mmap_page *meta = sampler->mapping;
    uint8_t *ring = (uint8_t *) sampler->mapping + sysconf(_SC_PAGESIZE);
    uint64_t head = ap_ring_read_head(meta);
    uint64_t tail = meta->data_tail;

    while (tail < head) {
        struct perf_event_header header;

        ap_copy_from_ring((uint8_t *) &header, ring, sampler->data_size, tail, sizeof(header));
        if (header.size < sizeof(header)) {
            errno = EPROTO;
            return -1;
        }

        if (header.type == PERF_RECORD_SAMPLE) {
            struct {
                struct perf_event_header header;
                uint64_t ip;
                uint32_t pid;
                uint32_t tid;
            } sample;

            if (header.size < sizeof(sample)) {
                errno = EPROTO;
                return -1;
            }

            ap_copy_from_ring((uint8_t *) &sample, ring, sampler->data_size, tail, sizeof(sample));
            if (ap_ip_vec_increment(ips, sample.ip) != 0) {
                return -1;
            }

            sampler->sample_count += 1;
        } else if (header.type == PERF_RECORD_LOST) {
            struct {
                struct perf_event_header header;
                uint64_t id;
                uint64_t lost;
            } lost;

            if (header.size < sizeof(lost)) {
                errno = EPROTO;
                return -1;
            }

            ap_copy_from_ring((uint8_t *) &lost, ring, sampler->data_size, tail, sizeof(lost));
            sampler->lost_count += lost.lost;
        }

        tail += header.size;
    }

    ap_ring_write_tail(meta, tail);
    return 0;
}
