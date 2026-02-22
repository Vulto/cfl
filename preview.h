#ifndef CFL_PREVIEW_H
#define CFL_PREVIEW_H

#include <ctype.h>

static pid_t ueberzug_pid = -1;
static FILE *ueberzug_in = NULL;
static int ueberzug_failed = 0;

static void ueberzugpp_start_once(void) {
    if (ueberzug_in != NULL || ueberzug_failed) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        ueberzug_failed = 1;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        ueberzug_failed = 1;
        return;
    }

    if (pid == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp(UEBERZUGPP_BIN, UEBERZUGPP_BIN, "layer", "--no-cache", "-o", UEBERZUGPP_OUTPUT, (char *)NULL);
        _exit(127);
    }

    close(pipefd[0]);
    ueberzug_pid = pid;
    ueberzug_in = fdopen(pipefd[1], "w");
    if (!ueberzug_in) {
        close(pipefd[1]);
        ueberzug_failed = 1;
        ueberzug_pid = -1;
        return;
    }
    setvbuf(ueberzug_in, NULL, _IOLBF, 0);
}

static void ueberzugpp_remove(void) {
    if (!ueberzug_in) return;
    fprintf(ueberzug_in, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", UEBERZUGPP_IDENTIFIER);
    fflush(ueberzug_in);
}

static void ueberzugpp_add(const char *img_path) {
    if (!img_path) return;

    ueberzugpp_start_once();
    if (!ueberzug_in) return;

    int win_y, win_x, win_h, win_w;
    getbegyx(preview_win, win_y, win_x);
    getmaxyx(preview_win, win_h, win_w);

    int x = win_x + 1;
    int y = win_y + 1;
    int w = win_w - 2;
    int h = win_h - 2;

    if (w <= 0 || h <= 0) return;

    ueberzugpp_remove();
    fprintf(ueberzug_in,
            "{\"action\":\"add\",\"identifier\":\"%s\",\"path\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}\n",
            UEBERZUGPP_IDENTIFIER, img_path, x, y, w, h);
    fflush(ueberzug_in);
}

static void ueberzugpp_stop(void) {
    if (ueberzug_in) {
        ueberzugpp_remove();
        fclose(ueberzug_in);
        ueberzug_in = NULL;
    }
    if (ueberzug_pid > 0) {
        kill(ueberzug_pid, SIGTERM);
        waitpid(ueberzug_pid, NULL, WNOHANG);
        ueberzug_pid = -1;
    }
}

static void preview_print_message(const char *message, int maxx) {
    wclear(preview_win);
    wmove(preview_win, 1, 2);
    wprintw(preview_win, "%.*s", maxx - 4, message);
    wrefresh(preview_win);
}

static int getMIME(const char *filepath, char mime[128]) {
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic == NULL) {
        return -1;
    }

    if (magic_load(magic, NULL) != 0) {
        magic_close(magic);
        return -1;
    }

    const char *mime_type = magic_file(magic, filepath);
    if (mime_type == NULL) {
        magic_close(magic);
        return -1;
    }

    snprintf(mime, 128, "%s", mime_type);
    magic_close(magic);
    return 0;
}

static void render_text_preview(const char *path, int maxx) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        return;
    }

    char line[4096];
    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int max_lines = TEXT_PREVIEW_LINES;
    int y = 1;
    wclear(preview_win);
    for (int i = 0; i < max_lines && fgets(line, sizeof line, fp); ++i) {
        wmove(preview_win, y++, 2);
        wprintw(preview_win, "%.*s", maxx - 4, line);
    }
    fclose(fp);
    wrefresh(preview_win);
}

static void render_hex_preview(const char *path, int maxx) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        return;
    }

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int avail_lines = rows - 2;
    if (avail_lines < 1) avail_lines = 1;
    size_t bytes_per_line = 16;
    size_t max_bytes = (size_t)avail_lines * bytes_per_line;
    unsigned char *buf = (unsigned char *)malloc(max_bytes);
    if (!buf) {
        fclose(fp);
        preview_print_message("Preview unavailable: out of memory", maxx);
        return;
    }

    size_t n = fread(buf, 1, max_bytes, fp);
    fclose(fp);

    wclear(preview_win);
    size_t offset = 0;
    int y = 1;
    while (offset < n && y <= avail_lines) {
        char ascii[17];
        ascii[16] = '\0';
        wmove(preview_win, y, 2);
        wprintw(preview_win, "%08zx  ", offset);
        for (size_t i = 0; i < bytes_per_line; i++) {
            if (offset + i < n) {
                unsigned char c = buf[offset + i];
                wprintw(preview_win, "%02x ", (unsigned)c);
                ascii[i] = (c >= 32 && c <= 126) ? c : '.';
            } else {
                wprintw(preview_win, "   ");
                ascii[i] = ' ';
            }
            if (i == 7) wprintw(preview_win, " ");
        }
        wprintw(preview_win, " %s", ascii);
        y++;
        offset += bytes_per_line;
    }

    free(buf);
    wrefresh(preview_win);
}

static void getPreview(char *filepath, int maxy, int maxx) {
    (void)maxy;
    struct stat st;
    char mime[128] = {0};

    if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
        preview_print_message("Preview unavailable: not a regular file", maxx);
        return;
    }

    if ((size_t)st.st_size > MAX_PREVIEW_BYTES) {
        preview_print_message("Preview unavailable: file too large", maxx);
        return;
    }

    if (getMIME(filepath, mime) != 0) {
        preview_print_message("Preview unavailable: MIME detection failed", maxx);
        return;
    }

    if (strncmp(mime, "image/", 6) == 0) {
        ueberzugpp_add(filepath);
        clearFlagImg = 1;
        return;
    }

    ueberzugpp_remove();
    if (strncmp(mime, "text/", 5) == 0 ||
        strstr(mime, "json") != NULL ||
        strstr(mime, "xml") != NULL ||
        strstr(mime, "javascript") != NULL ||
        strstr(mime, "x-shellscript") != NULL) {
        render_text_preview(filepath, maxx);
    } else {
        render_hex_preview(filepath, maxx);
    }
}

static void clearImg(void) {
    ueberzugpp_remove();
}

#endif
