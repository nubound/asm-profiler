#include "asm_profiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void ap_options_init(ap_options *options) {
    memset(options, 0, sizeof(*options));
    options->sample_period = 1000000;
    options->target.start_fd = -1;
}

static void ap_destroy_target(ap_target_command *target) {
    if (target->start_fd >= 0) {
        close(target->start_fd);
    }

    target->start_fd = -1;
    target->argv = NULL;
}

void ap_options_destroy(ap_options *options) {
    ap_destroy_target(&options->target);
}

int ap_parse_args(ap_options *options, int argc, char **argv, char **error_msg) {
    ap_options_init(options);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            options->show_help = true;
            return 0;
        }

        if (strcmp(argv[i], "--period") == 0) {
            char *end = NULL;
            unsigned long long parsed = 0;

            if (i + 1 >= argc) {
                *error_msg = "missing value for --period";
                return -1;
            }

            parsed = strtoull(argv[++i], &end, 10);
            if (!end || *end != '\0' || parsed == 0ULL) {
                *error_msg = "invalid numeric value for --period";
                return -1;
            }

            options->sample_period = (uint64_t) parsed;
            continue;
        }

        if (strcmp(argv[i], "--") == 0) {
            if (i + 1 >= argc) {
                *error_msg = "missing target command after --";
                return -1;
            }

            options->target.argv = &argv[i + 1];
            return 0;
        }

        *error_msg = "unknown argument";
        return -1;
    }

    *error_msg = "missing target command, use -- <command>";
    return -1;
}
