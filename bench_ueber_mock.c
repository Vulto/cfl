/* Stand-in for ueberzugpp. Compile and put it on PATH in front of any real binary:
 *
 *   cc -O2 -o ueberzugpp bench_ueber_mock.c
 *
 * Reads JSON lines from stdin until EOF. Each add can sleep UEBER_DELAY_US
 * to imitate a slow image decode. Every line is appended to UEBER_LOG when set.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char buf[8192];
    int delay_us = 0;
    const char *d = getenv("UEBER_DELAY_US");
    const char *logp = getenv("UEBER_LOG");

    if (d)
        delay_us = atoi(d);

    while (fgets(buf, sizeof buf, stdin)) {
        if (logp && logp[0]) {
            int fd = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                size_t n = strlen(buf);
                if (n)
                    write(fd, buf, n);
                close(fd);
            }
        }
        if (delay_us > 0 && strstr(buf, "\"add\""))
            usleep((useconds_t)delay_us);
    }
    return 0;
}
