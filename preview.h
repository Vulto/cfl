#ifndef CFL_PREVIEW_H
#define CFL_PREVIEW_H

#include <ctype.h>

int len_preview = 0;
static pid_t ueberzug_pid = -1;
static FILE *ueberzug_in = NULL;
static int ueberzug_failed = 0;
int clearFlagImg = 0;

extern int preview_scroll;       /* shared with functions.h */
extern long preview_total_lines; /* shared with functions.h for text/hex totals */

/* Caching for preview target + totals (text line count or hex rows) */
static char preview_target_path[PATH_MAX] = "";
/* preview_total_lines is defined in functions.h and externed above */

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

/* Fast line counter (called only on preview target change for perf) */
static long count_text_lines(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    long n = 0;
    char buf[8192];
    while (fgets(buf, sizeof(buf), fp)) n++;
    fclose(fp);
    return n;
}

static void render_text_preview(const char *path, int maxx) {
    long total = preview_total_lines;
    if (total < 0) total = 0;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    (void)cols;
    int visible = rows > 0 ? rows : 1;

    /* Enforce last-line stop: max scroll so last line sits at bottom */
    long max_scroll = (total > visible) ? (total - visible) : 0;
    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > max_scroll) preview_scroll = (int)max_scroll;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, true);
        wrefresh(preview_win);
        return;
    }

    char line[4096];
    int avail_lines = visible;
    if (avail_lines < 1) avail_lines = 1;

    /* Skip scrolled lines */
    long skip = preview_scroll;
    for (long i = 0; i < skip; i++) {
        if (!fgets(line, sizeof(line), fp)) break;
    }

    wclear(preview_win);
    int y = 1;
    for (int i = 0; i < avail_lines && fgets(line, sizeof(line), fp); ++i) {
        wmove(preview_win, y++, 2);
        wprintw(preview_win, "%.*s", maxx - 4, line);
    }
    fclose(fp);

    drawScrollbar(preview_win, (int)total, preview_scroll, true);
    wrefresh(preview_win);
}

static void render_hex_preview(const char *path, int maxx) {
    long total = preview_total_lines;
    if (total < 0) total = 0;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    (void)cols;
    int visible = rows > 0 ? rows : 1;

    long max_scroll = (total > visible) ? (total - visible) : 0;
    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > max_scroll) preview_scroll = (int)max_scroll;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, true);
        wrefresh(preview_win);
        return;
    }

    int avail_lines = visible;
    if (avail_lines < 1) avail_lines = 1;

    size_t bytes_per_line = 16;
    size_t start_offset = (size_t)preview_scroll * bytes_per_line;

    if (fseek(fp, (long)start_offset, SEEK_SET) != 0) {
        fclose(fp);
        preview_print_message("Preview unavailable: seek error", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, true);
        wrefresh(preview_win);
        return;
    }

    size_t max_bytes = (size_t)avail_lines * bytes_per_line;
    unsigned char *buf = (unsigned char *)malloc(max_bytes);
    if (!buf) {
        fclose(fp);
        preview_print_message("Preview unavailable: out of memory", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, true);
        wrefresh(preview_win);
        return;
    }

    size_t n = fread(buf, 1, max_bytes, fp);
    fclose(fp);

    wclear(preview_win);

    size_t offset = start_offset;
    int y = 1;
    while (offset < start_offset + n && y <= avail_lines) {
        char ascii[17];
        ascii[16] = '\0';
        wmove(preview_win, y, 2);
        wprintw(preview_win, "%08zx  ", offset);
        for (size_t i = 0; i < bytes_per_line; i++) {
            if (offset + i < start_offset + n) {
                unsigned char c = buf[(offset + i) - start_offset];
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
    drawScrollbar(preview_win, (int)total, preview_scroll, true);
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

    /* Cache total for text/hex scrollbar (only on target change) */
    if (strcmp(preview_target_path, filepath) != 0) {
        strncpy(preview_target_path, filepath, sizeof(preview_target_path)-1);
        preview_target_path[sizeof(preview_target_path)-1] = '\0';

        if (strncmp(mime, "text/", 5) == 0 ||
            strstr(mime, "json") != NULL ||
            strstr(mime, "xml") != NULL ||
            strstr(mime, "javascript") != NULL ||
            strstr(mime, "x-shellscript") != NULL) {
            preview_total_lines = count_text_lines(filepath);
        } else {
            size_t bpl = 16;
            preview_total_lines = (st.st_size + bpl - 1) / bpl;
        }
    }

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

void PreviewNextDir(char *next_dir_arg, char **next_directories) {
    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int visible = rows > 0 ? rows : 1;

    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > len_preview) preview_scroll = len_preview;

    int start = preview_scroll;
    for (int i = start; i < len_preview && (i - start) < visible; i++) {
        int y = i - start + 1;
        wmove(preview_win, y, 2);
        snprintf(temp_dir, sizeof(temp_dir), "%s/%s", next_dir_arg, next_directories[i]);
        if (isRegularFile(temp_dir) == 0) {
            wattron(preview_win, A_BOLD);
        } else {
            wattroff(preview_win, A_BOLD);
        }
        wprintw(preview_win, "%.*s\n", maxx/2 - 3, next_directories[i]);
    }

    /* Left scrollbar for preview directory listing */
    drawScrollbar(preview_win, len_preview, start, true);
}
#endif
