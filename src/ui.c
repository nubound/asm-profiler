#include "asm_profiler.h"

#include <errno.h>
#include <ncurses.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t ap_interrupted = 0;

static void ap_on_sigint(int signo) {
    (void) signo;
    ap_interrupted = 1;
}

static int ap_setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ap_on_sigint;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return -1;
    }

    return 0;
}

static void ap_draw_table(const ap_session_view *view) {
    mvprintw(0, 0, "asm-profiler");
    mvprintw(1, 0, "status: %s", view->running ? "running" : "exited");
    mvprintw(2, 0, "runtime: %llums", (unsigned long long) view->elapsed_ms);
    mvprintw(3, 0, "samples: %llu  lost: %llu",
             (unsigned long long) view->total_samples,
             (unsigned long long) view->lost_samples);
    mvprintw(5, 0, "%-8s %-10s %-18s %s", "rank", "samples", "address", "symbol");

    size_t limit = (size_t) ((LINES > 8) ? (LINES - 8) : 0);
    if (limit > view->hot_symbols.len) {
        limit = view->hot_symbols.len;
    }

    for (size_t i = 0; i < limit; ++i) {
        const ap_hot_symbol *sym = &view->hot_symbols.items[i];
        if (sym->samples == 0) {
            break;
        }

        mvprintw((int) i + 6, 0, "%-8zu %-10llu 0x%016llx %s",
                 i + 1,
                 (unsigned long long) sym->samples,
                 (unsigned long long) sym->start,
                 sym->name);
    }
}

int ap_ui_run(const ap_options *options, pid_t pid, ap_perf_sampler *sampler, const ap_symbol_table *symbols) {
    ap_ip_vec ips = {0};
    ap_session_view view = {0};
    uint64_t start_ms = ap_now_millis();
    int rc = -1;

    (void) options;

    if (ap_setup_signals() != 0) {
        return -1;
    }

    if (initscr() == NULL) {
        return -1;
    }

    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);

    while (!ap_interrupted) {
        bool exited = false;
        int status = 0;
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            break;
        }

        if (ap_perf_sampler_drain(sampler, &ips) != 0) {
            break;
        }

        if (ap_wait_target_nonblock(pid, &status, &exited) != 0) {
            break;
        }

        view.running = !exited;
        view.child_status = status;
        view.elapsed_ms = ap_now_millis() - start_ms;
        view.total_samples = sampler->sample_count;
        view.lost_samples = sampler->lost_count;

        if (ap_symbol_table_aggregate(symbols, &ips, &view.hot_symbols) != 0) {
            break;
        }

        erase();
        ap_draw_table(&view);
        mvprintw(LINES - 1, 0, "q to quit");
        refresh();

        if (exited) {
            rc = 0;
            break;
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000L;
        nanosleep(&ts, NULL);
    }

    endwin();
    ap_hot_symbol_vec_destroy(&view.hot_symbols);
    ap_ip_vec_destroy(&ips);
    return rc;
}
