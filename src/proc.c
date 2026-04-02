#include "asm_profiler.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int ap_spawn_target(ap_target_command *command, pid_t *pid_out) {
    int gate[2] = {-1, -1};
    pid_t pid;

    if (pipe(gate) != 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(gate[0]);
        close(gate[1]);
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        char start = 0;

        close(gate[1]);

        if (devnull >= 0) {
            (void) dup2(devnull, STDIN_FILENO);
            (void) dup2(devnull, STDOUT_FILENO);
            (void) dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }

        if (read(gate[0], &start, 1) < 0) {
            _exit(127);
        }

        close(gate[0]);
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) {
            _exit(127);
        }

        execvp(command->argv[0], command->argv);
        _exit(127);
    }

    close(gate[0]);
    command->start_fd = gate[1];
    *pid_out = pid;
    return 0;
}

int ap_start_target(ap_target_command *command) {
    char start = 'x';

    if (command->start_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (write(command->start_fd, &start, 1) != 1) {
        return -1;
    }

    close(command->start_fd);
    command->start_fd = -1;
    return 0;
}

int ap_wait_target_exec(pid_t pid) {
    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
        return 0;
    }

    if (WIFEXITED(status)) {
        errno = ENOENT;
        return -1;
    }

    errno = EPROTO;
    return -1;
}

int ap_resume_target(pid_t pid) {
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) != 0) {
        return -1;
    }

    return 0;
}

int ap_wait_target_nonblock(pid_t pid, int *status_out, bool *exited_out) {
    int status = 0;
    pid_t rc = waitpid(pid, &status, WNOHANG);
    if (rc == 0) {
        *exited_out = false;
        return 0;
    }

    if (rc < 0) {
        return -1;
    }

    *exited_out = true;
    *status_out = status;
    return 0;
}
