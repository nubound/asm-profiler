#include "asm_profiler.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void ap_print_usage(FILE *stream) {
    fprintf(stream,
            "usage: asm-profiler [--period N] -- <command> [args...]\n"
            "\n"
            "options:\n"
            "  --period N   sample period for PERF_COUNT_SW_CPU_CLOCK\n"
            "  -h, --help   show this help text\n");
}

int main(int argc, char **argv) {
    ap_options options;
    ap_perf_sampler sampler;
    ap_symbol_table symbols;
    char *error_msg = NULL;
    pid_t pid = -1;
    int exit_code = 1;

    ap_perf_sampler_init(&sampler);
    ap_symbol_table_init(&symbols);
    ap_options_init(&options);

    if (ap_parse_args(&options, argc, argv, &error_msg) != 0) {
        fprintf(stderr, "argument error: %s\n", error_msg ? error_msg : "unknown failure");
        ap_print_usage(stderr);
        return 2;
    }

    if (options.show_help) {
        ap_print_usage(stdout);
        return 0;
    }

    if (ap_spawn_target(&options.target, &pid) != 0) {
        fprintf(stderr, "failed to spawn target: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_perf_sampler_open(&sampler, pid, options.sample_period) != 0) {
        fprintf(stderr, "failed to open perf sampler: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_start_target(&options.target) != 0) {
        fprintf(stderr, "failed to start target: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_wait_target_exec(pid) != 0) {
        fprintf(stderr, "target failed before profiling could begin: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_symbol_table_load_main_exe(&symbols, pid) != 0) {
        fprintf(stderr, "failed to load target symbols: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_resume_target(pid) != 0) {
        fprintf(stderr, "failed to resume target: %s\n", strerror(errno));
        goto cleanup;
    }

    if (ap_ui_run(&options, pid, &sampler, &symbols) != 0) {
        fprintf(stderr, "profiler session failed\n");
        goto cleanup;
    }

    exit_code = 0;

cleanup:
    if (options.target.start_fd >= 0) {
        close(options.target.start_fd);
        options.target.start_fd = -1;
    }

    if (sampler.fd >= 0) {
        ap_perf_sampler_disable(&sampler);
    }

    if (pid > 0) {
        int status = 0;
        bool exited = false;
        if (ap_wait_target_nonblock(pid, &status, &exited) == 0 && !exited) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
        }
    }

    ap_perf_sampler_destroy(&sampler);
    ap_symbol_table_destroy(&symbols);
    ap_options_destroy(&options);
    return exit_code;
}
