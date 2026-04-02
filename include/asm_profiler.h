#ifndef ASM_PROFILER_H
#define ASM_PROFILER_H

#include <elf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    char **argv;
    int start_fd;
} ap_target_command;

typedef struct {
    ap_target_command target;
    uint64_t sample_period;
    bool show_help;
} ap_options;

typedef struct {
    uint64_t ip;
    uint64_t samples;
} ap_ip_count;

typedef struct {
    ap_ip_count *items;
    size_t len;
    size_t cap;
} ap_ip_vec;

typedef struct {
    char name[256];
    uint64_t start;
    uint64_t end;
    uint64_t samples;
} ap_hot_symbol;

typedef struct {
    ap_hot_symbol *items;
    size_t len;
    size_t cap;
} ap_hot_symbol_vec;

typedef struct {
    int fd;
    void *mapping;
    size_t mapping_len;
    size_t data_size;
    uint64_t sample_period;
    uint64_t sample_count;
    uint64_t lost_count;
} ap_perf_sampler;

typedef struct {
    char *path;
    uint64_t base_vaddr;
    ap_hot_symbol_vec symbols;
} ap_symbol_table;

typedef struct {
    bool running;
    int child_status;
    uint64_t elapsed_ms;
    uint64_t total_samples;
    uint64_t lost_samples;
    ap_hot_symbol_vec hot_symbols;
} ap_session_view;

void ap_options_init(ap_options *options);
int ap_parse_args(ap_options *options, int argc, char **argv, char **error_msg);
void ap_options_destroy(ap_options *options);

void ap_ip_vec_destroy(ap_ip_vec *vec);
int ap_ip_vec_increment(ap_ip_vec *vec, uint64_t ip);

void ap_hot_symbol_vec_destroy(ap_hot_symbol_vec *vec);
int ap_hot_symbol_vec_push(ap_hot_symbol_vec *vec, const ap_hot_symbol *item);

void *ap_callocarray(size_t nmemb, size_t size);
void *ap_reallocarray(void *ptr, size_t nmemb, size_t size);
char *ap_strdup(const char *value);
uint64_t ap_now_millis(void);

int ap_spawn_target(ap_target_command *command, pid_t *pid_out);
int ap_start_target(ap_target_command *command);
int ap_wait_target_exec(pid_t pid);
int ap_resume_target(pid_t pid);
int ap_wait_target_nonblock(pid_t pid, int *status_out, bool *exited_out);

void ap_perf_sampler_init(ap_perf_sampler *sampler);
void ap_perf_sampler_destroy(ap_perf_sampler *sampler);
int ap_perf_sampler_open(ap_perf_sampler *sampler, pid_t pid, uint64_t sample_period);
int ap_perf_sampler_disable(ap_perf_sampler *sampler);
int ap_perf_sampler_drain(ap_perf_sampler *sampler, ap_ip_vec *ips);

void ap_symbol_table_init(ap_symbol_table *table);
void ap_symbol_table_destroy(ap_symbol_table *table);
int ap_symbol_table_load_main_exe(ap_symbol_table *table, pid_t pid);
int ap_symbol_table_aggregate(const ap_symbol_table *table, const ap_ip_vec *ips, ap_hot_symbol_vec *out);

int ap_ui_run(const ap_options *options, pid_t pid, ap_perf_sampler *sampler, const ap_symbol_table *symbols);

#endif
