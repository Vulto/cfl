/* Time cfl while moving through a directory.
 *
 *   cc -O2 -o bench_nav bench_nav.c -lutil
 *   cc -O2 -o ueberzugpp bench_ueber_mock.c
 *   PATH="$(pwd):$PATH" UEBER_LOG=/tmp/ueber.log ./bench_nav ./cfl /path/to/imgs 200
 *
 * The mock (below, compile as ueberzugpp) must be first on PATH. It reads
 * JSON commands until stdin closes. UEBER_DELAY_US sleeps on each add, which
 * simulates a slow decode. UEBER_LOG receives every command line.
 *
 * bench_nav waits until the first frame is idle, then:
 *   - quit: one 'q' (startup is not included)
 *   - burst: STEPS 'j' plus 'q' already queued, so a held key should not
 *     preview every file
 * Prints milliseconds and, when UEBER_LOG is set, how many add commands the
 * burst produced (expect about 0; the initial preview happened before the log
 * was truncated).
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void drain_until_idle(int master) {
    char buf[4096];
    double last = now_ms();
    double start = last;
    struct pollfd p = {.fd = master, .events = POLLIN};
    while (now_ms() - start < 5000) {
        int pr = poll(&p, 1, 20);
        if (pr > 0 && (p.revents & POLLIN)) {
            if (read(master, buf, sizeof buf) <= 0)
                break;
            last = now_ms();
        } else if (now_ms() - last >= 80) {
            return;
        }
    }
}

static int count_adds(const char *path) {
    FILE *fp;
    char buf[8192];
    int n = 0;
    if (!path || !path[0])
        return -1;
    fp = fopen(path, "r");
    if (!fp)
        return 0;
    while (fgets(buf, sizeof buf, fp)) {
        if (strstr(buf, "\"add\""))
            n++;
    }
    fclose(fp);
    return n;
}

static void truncate_log(const char *path) {
    FILE *fp;
    if (!path || !path[0])
        return;
    fp = fopen(path, "w");
    if (fp)
        fclose(fp);
}

/* Returns milliseconds from the key write until the child exits.
 * *status is the raw wait status. */
static double run_keys(const char *bin, const char *dir, const char *keys, int *status) {
    int master = -1;
    struct winsize ws = {.ws_row = 40, .ws_col = 140};
    char buf[4096];
    struct pollfd p;
    pid_t pid;
    double t0;
    size_t len, off;

    pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) {
        perror("forkpty");
        exit(1);
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execl(bin, bin, dir, (char *)NULL);
        perror("execl");
        _exit(127);
    }

    drain_until_idle(master);
    truncate_log(getenv("UEBER_LOG"));

    t0 = now_ms();
    len = strlen(keys);
    off = 0;
    while (off < len) {
        ssize_t w = write(master, keys + off, len - off);
        if (w < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }
        off += (size_t)w;
    }

    p.fd = master;
    p.events = POLLIN;
    *status = 1;
    for (;;) {
        int st;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            *status = st;
            break;
        }
        if (poll(&p, 1, 200) > 0 && (p.revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(master, buf, sizeof buf);
            if (n < 0 && errno != EAGAIN && errno != EINTR)
                break;
        }
        if (now_ms() - t0 > 60000) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
            fprintf(stderr, "timeout\n");
            break;
        }
    }
    close(master);
    return now_ms() - t0;
}

int main(int argc, char **argv) {
    const char *bin;
    const char *dir;
    int steps;
    int status = 0;
    char *keys;
    double quit_ms, burst_ms;
    const char *log;

    if (argc < 3) {
        fprintf(stderr, "usage: %s cfl-binary directory [steps]\n", argv[0]);
        return 2;
    }
    bin = argv[1];
    dir = argv[2];
    steps = argc > 3 ? atoi(argv[3]) : 200;
    if (steps < 1 || steps > 100000) {
        fprintf(stderr, "steps out of range\n");
        return 2;
    }

    keys = malloc((size_t)steps + 2);
    if (!keys)
        return 1;
    memset(keys, 'j', (size_t)steps);
    keys[steps] = 'q';
    keys[steps + 1] = '\0';

    quit_ms = run_keys(bin, dir, "q", &status);
    burst_ms = run_keys(bin, dir, keys, &status);
    log = getenv("UEBER_LOG");

    printf("quit_only_ms %.1f\n", quit_ms);
    printf("burst_steps %d burst_ms %.1f\n", steps, burst_ms);
    printf("per_step_ms %.3f\n", (burst_ms - quit_ms) / (double)steps);
    if (log)
        printf("burst_adds %d\n", count_adds(log));
    free(keys);
    return 0;
}
