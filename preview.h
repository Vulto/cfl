#ifndef CFL_PREVIEW_H
#define CFL_PREVIEW_H

#include <ctype.h>
int preview_start;     /* column for preview pane start / right scrollbar position */
int preview_scroll = 0; /* current scroll offset for preview pane (dir listings + text/hex) */
long preview_total_lines = 0; /* cached total for current text/hex preview (lines or hex-rows) */

int len_preview = 0;
static pid_t ueberzug_pid = -1;
static int ueberzug_fd = -1;
static int ueberzug_failed = 0;
int clearFlagImg = 0;
static char ueberzug_pending[PATH_MAX * 6 + 512];
static size_t ueberzug_pending_len = 0;
static size_t ueberzug_pending_off = 0;

/* Track last ueberzug image so we can skip remove when next is equal/larger */
static int ueberzug_showing = 0;
static int ueberzug_last_w = 0;
static int ueberzug_last_h = 0;

extern int preview_scroll;       /* shared with functions.h */
extern long preview_total_lines; /* shared with functions.h for text/hex totals */

extern int isSymlink(const char *path);
extern int readSymlinkTarget(const char *path, char *buf, size_t bufsz);

/* Caching for preview target + totals (text line count or hex rows) */
static char preview_target_path[PATH_MAX] = "";
/* preview_total_lines is defined in functions.h and externed above */

static unsigned img_be16(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}
static unsigned img_be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}
static unsigned img_le16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned img_le32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* Fast header-only dimension probe (no external tools). Returns 1 on success. */
static int image_dimensions(const char *path, int *out_w, int *out_h) {
    if (!path || !out_w || !out_h) return 0;
    *out_w = *out_h = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    unsigned char buf[64];
    size_t n = fread(buf, 1, sizeof buf, fp);
    if (n < 24) {
        fclose(fp);
        return 0;
    }

    /* PNG: 8-byte sig + IHDR length/type + 8-byte width/height */
    static const unsigned char png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (n >= 24 && memcmp(buf, png_sig, 8) == 0) {
        unsigned w = img_be32(buf + 16);
        unsigned h = img_be32(buf + 20);
        fclose(fp);
        if (w == 0 || h == 0 || w > 65535 || h > 65535) return 0;
        *out_w = (int)w;
        *out_h = (int)h;
        return 1;
    }

    /* GIF */
    if (n >= 10 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0)) {
        unsigned w = img_le16(buf + 6);
        unsigned h = img_le16(buf + 8);
        fclose(fp);
        if (w == 0 || h == 0) return 0;
        *out_w = (int)w;
        *out_h = (int)h;
        return 1;
    }

    /* BMP */
    if (n >= 26 && buf[0] == 'B' && buf[1] == 'M') {
        unsigned w = img_le32(buf + 18);
        unsigned h = img_le32(buf + 22);
        /* height may be negative (top-down DIB) */
        if ((int)h < 0) h = (unsigned)(-(int)h);
        fclose(fp);
        if (w == 0 || h == 0) return 0;
        *out_w = (int)w;
        *out_h = (int)h;
        return 1;
    }

    /* WebP: RIFF....WEBP */
    if (n >= 30 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WEBP", 4) == 0) {
        if (memcmp(buf + 12, "VP8X", 4) == 0 && n >= 30) {
            /* canvas size is 24-bit little-endian minus 1 */
            unsigned w = 1 + (buf[24] | (buf[25] << 8) | (buf[26] << 16));
            unsigned h = 1 + (buf[27] | (buf[28] << 8) | (buf[29] << 16));
            fclose(fp);
            *out_w = (int)w;
            *out_h = (int)h;
            return 1;
        }
        if (memcmp(buf + 12, "VP8 ", 4) == 0 && n >= 30) {
            /* lossy: start code then 16-bit le width/height in bits */
            unsigned w = img_le16(buf + 26) & 0x3fff;
            unsigned h = img_le16(buf + 28) & 0x3fff;
            fclose(fp);
            if (w == 0 || h == 0) return 0;
            *out_w = (int)w;
            *out_h = (int)h;
            return 1;
        }
        if (memcmp(buf + 12, "VP8L", 4) == 0 && n >= 25) {
            unsigned b0 = buf[21], b1 = buf[22], b2 = buf[23], b3 = buf[24];
            unsigned w = 1 + (((b1 & 0x3f) << 8) | b0);
            unsigned h = 1 + (((b3 & 0x0f) << 10) | (b2 << 2) | ((b1 & 0xc0) >> 6));
            fclose(fp);
            *out_w = (int)w;
            *out_h = (int)h;
            return 1;
        }
        fclose(fp);
        return 0;
    }

    /* JPEG: scan SOF markers */
    if (n >= 2 && buf[0] == 0xff && buf[1] == 0xd8) {
        long pos = 2;
        for (;;) {
            unsigned char marker[4];
            if (fseek(fp, pos, SEEK_SET) != 0) break;
            if (fread(marker, 1, 4, fp) != 4) break;
            if (marker[0] != 0xff) break;
            unsigned char m = marker[1];
            /* standalone markers without length */
            if (m == 0xd8 || m == 0xd9) {
                pos += 2;
                continue;
            }
            if (m == 0x00 || m == 0xff) {
                pos += 1;
                continue;
            }
            unsigned seglen = img_be16(marker + 2);
            if (seglen < 2) break;
            /* SOF0..SOF3, SOF5..SOF7, SOF9..SOF11, SOF13..SOF15 */
            if ((m >= 0xc0 && m <= 0xc3) || (m >= 0xc5 && m <= 0xc7) ||
                (m >= 0xc9 && m <= 0xcb) || (m >= 0xcd && m <= 0xcf)) {
                unsigned char sof[5];
                if (fread(sof, 1, 5, fp) != 5) break;
                unsigned h = img_be16(sof + 1);
                unsigned w = img_be16(sof + 3);
                fclose(fp);
                if (w == 0 || h == 0) return 0;
                *out_w = (int)w;
                *out_h = (int)h;
                return 1;
            }
            pos += 2 + (long)seglen;
        }
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 0;
}

static void ueberzug_mark_dead(void) {
    if (ueberzug_fd >= 0) {
        close(ueberzug_fd);
        ueberzug_fd = -1;
    }
    if (ueberzug_pid > 0) {
        kill(ueberzug_pid, SIGTERM);
        waitpid(ueberzug_pid, NULL, WNOHANG);
        ueberzug_pid = -1;
    }
    ueberzug_failed = 1;
    ueberzug_showing = 0;
    ueberzug_last_w = 0;
    ueberzug_last_h = 0;
    ueberzug_pending_len = 0;
    ueberzug_pending_off = 0;
}

/* Drop SIGPIPE for this write. A dead ueberzug must not kill the file manager. */
static ssize_t write_nosigpipe(int fd, const void *buf, size_t len) {
    sigset_t block, old, pending;
    ssize_t n;
    int err;

    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    sigprocmask(SIG_BLOCK, &block, &old);
    n = write(fd, buf, len);
    err = errno;
    sigpending(&pending);
    if (sigismember(&pending, SIGPIPE)) {
        int sig;
        sigwait(&block, &sig);
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    errno = err;
    return n;
}

/* Queue one JSON line. Non-blocking: a stalled ueberzug must not freeze navigation.
 * Returns 1 if this line was queued, 0 if a previous line is still draining. */
static int ueberzug_send(const char *line) {
    size_t n;
    if (ueberzug_fd < 0 || !line)
        return 0;
    if (ueberzug_pending_off > 0) {
        while (ueberzug_pending_off < ueberzug_pending_len) {
            ssize_t w = write_nosigpipe(ueberzug_fd,
                                        ueberzug_pending + ueberzug_pending_off,
                                        ueberzug_pending_len - ueberzug_pending_off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return 0;
                ueberzug_mark_dead();
                return 0;
            }
            ueberzug_pending_off += (size_t)w;
        }
        ueberzug_pending_len = 0;
        ueberzug_pending_off = 0;
    }
    n = strlen(line);
    if (n == 0 || n >= sizeof ueberzug_pending)
        return 0;
    memcpy(ueberzug_pending, line, n);
    ueberzug_pending_len = n;
    ueberzug_pending_off = 0;
    while (ueberzug_pending_off < ueberzug_pending_len) {
        ssize_t w = write_nosigpipe(ueberzug_fd,
                                    ueberzug_pending + ueberzug_pending_off,
                                    ueberzug_pending_len - ueberzug_pending_off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 1; /* queued; flushed on the next send */
            ueberzug_mark_dead();
            return 0;
        }
        ueberzug_pending_off += (size_t)w;
    }
    ueberzug_pending_len = 0;
    ueberzug_pending_off = 0;
    return 1;
}

static int json_escape(const char *src, char *dst, size_t dstsz) {
    size_t j = 0;
    size_t i;
    if (!src || !dst || dstsz == 0)
        return -1;
    for (i = 0; src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dstsz)
                return -1;
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c < 0x20) {
            int n;
            if (j + 6 >= dstsz)
                return -1;
            n = snprintf(dst + j, dstsz - j, "\\u%04x", c);
            if (n != 6)
                return -1;
            j += 6;
        } else {
            if (j + 1 >= dstsz)
                return -1;
            dst[j++] = (char)c;
        }
    }
    if (j >= dstsz)
        return -1;
    dst[j] = '\0';
    return 0;
}

static void ueberzugpp_start_once(void) {
    int pipefd[2];
    int flags;
    pid_t pid;

    if (ueberzug_fd >= 0 || ueberzug_failed)
        return;

    if (pipe(pipefd) != 0) {
        ueberzug_failed = 1;
        return;
    }

    pid = fork();
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

        /* --no-cache: ueberzugpp's own cache keeps every scaled bitmap in RAM.
         * cfl caches small jpegs on disk instead and only sends the file you stop on. */
        execlp(UEBERZUGPP_BIN, UEBERZUGPP_BIN, "layer", "--no-cache", "-o", UEBERZUGPP_OUTPUT, (char *)NULL);
        _exit(127);
    }

    close(pipefd[0]);
    flags = fcntl(pipefd[1], F_GETFL, 0);
    if (flags >= 0)
        fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    ueberzug_pid = pid;
    ueberzug_fd = pipefd[1];
    ueberzug_pending_len = 0;
    ueberzug_pending_off = 0;
}

static char ueberzug_last_path[PATH_MAX];
static int ueberzug_geom_x, ueberzug_geom_y, ueberzug_geom_w, ueberzug_geom_h;

static void ueberzugpp_remove(void) {
    char line[256];
    if (ueberzug_fd < 0) {
        ueberzug_showing = 0;
        return;
    }
    snprintf(line, sizeof line, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", UEBERZUGPP_IDENTIFIER);
    ueberzug_send(line);
    ueberzug_showing = 0;
    ueberzug_last_w = 0;
    ueberzug_last_h = 0;
    ueberzug_last_path[0] = '\0';
}

/* Add/replace image. have_dims: 1 known, 0 unknown, -1 probe the file.
 * Clear only when needed:
 *  - not currently showing an image (nothing to skip)
 *  - new image smaller in width OR height than previous (ghosting)
 *  - dimensions unknown (safe clear)
 * Equal or larger: splash on top without remove (fast scrolling).
 * The same path and geometry is not sent again. */
static void ueberzugpp_add(const char *img_path, int have_dims, int iw, int ih) {
    int win_y, win_x, win_h, win_w;
    int x, y, w, h;
    int got_dims;
    int need_clear;
    char escaped[PATH_MAX * 6];
    char line[PATH_MAX * 6 + 128];

    if (!img_path || !preview_win)
        return;

    ueberzugpp_start_once();
    if (ueberzug_fd < 0)
        return;

    getbegyx(preview_win, win_y, win_x);
    getmaxyx(preview_win, win_h, win_w);

    x = win_x + 1;
    y = win_y + 1;
    w = win_w - 2;
    h = win_h - 2;
    if (w <= 0 || h <= 0)
        return;

    if (have_dims < 0) {
        got_dims = image_dimensions(img_path, &iw, &ih);
    } else if (have_dims == 0) {
        got_dims = 0;
        iw = ih = 0;
    } else {
        got_dims = 1;
    }

    if (ueberzug_showing && strcmp(ueberzug_last_path, img_path) == 0 &&
        x == ueberzug_geom_x && y == ueberzug_geom_y &&
        w == ueberzug_geom_w && h == ueberzug_geom_h)
        return;

    need_clear = 0;
    if (ueberzug_showing) {
        if (!got_dims)
            need_clear = 1;
        else if (iw < ueberzug_last_w || ih < ueberzug_last_h)
            need_clear = 1;
    }

    if (need_clear) {
        snprintf(line, sizeof line, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", UEBERZUGPP_IDENTIFIER);
        ueberzug_send(line);
        ueberzug_showing = 0;
        ueberzug_last_w = 0;
        ueberzug_last_h = 0;
    }

    if (json_escape(img_path, escaped, sizeof escaped) != 0)
        return;
    snprintf(line, sizeof line,
             "{\"action\":\"add\",\"identifier\":\"%s\",\"path\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}\n",
             UEBERZUGPP_IDENTIFIER, escaped, x, y, w, h);
    if (!ueberzug_send(line))
        return;

    ueberzug_showing = 1;
    snprintf(ueberzug_last_path, sizeof ueberzug_last_path, "%s", img_path);
    ueberzug_geom_x = x;
    ueberzug_geom_y = y;
    ueberzug_geom_w = w;
    ueberzug_geom_h = h;
    if (got_dims) {
        ueberzug_last_w = iw;
        ueberzug_last_h = ih;
    } else {
        ueberzug_last_w = 0;
        ueberzug_last_h = 0;
    }
}

static void ueberzugpp_stop(void) {
    if (ueberzug_fd >= 0) {
        ueberzugpp_remove();
        close(ueberzug_fd);
        ueberzug_fd = -1;
    }
    if (ueberzug_pid > 0) {
        kill(ueberzug_pid, SIGTERM);
        waitpid(ueberzug_pid, NULL, WNOHANG);
        ueberzug_pid = -1;
    }
    ueberzug_pending_len = 0;
    ueberzug_pending_off = 0;
}

static void preview_print_message(const char *message, int maxx) {
    (void)maxx;
    int rows, pw;
    getmaxyx(preview_win, rows, pw);
    (void)rows;
    int safe_width = pw - 3; // left pad (2) + right bar reservation

    wclear(preview_win);
    wmove(preview_win, 1, 2);
    wprintw(preview_win, "%.*s", safe_width, message);
    wrefresh(preview_win);
}

static magic_t mime_cookie = NULL;
static char mime_cache_path[PATH_MAX];
static off_t mime_cache_size = -1;
static time_t mime_cache_mtime = 0;
static long mime_cache_nsec = 0;
static char mime_cache_val[128];
static int mime_cache_set = 0;

static int mime_ensure(void) {
    if (mime_cookie)
        return 0;
    mime_cookie = magic_open(MAGIC_MIME_TYPE);
    if (mime_cookie == NULL)
        return -1;
    if (magic_load(mime_cookie, NULL) != 0) {
        magic_close(mime_cookie);
        mime_cookie = NULL;
        return -1;
    }
    return 0;
}

static void mime_shutdown(void) {
    if (mime_cookie) {
        magic_close(mime_cookie);
        mime_cookie = NULL;
    }
}

/* magic_load parses the whole database. Do it once, and reuse the answer
 * while path, size, and mtime are unchanged. */
static int getMIME(const char *filepath, char mime[128]) {
    const char *mime_type;
    if (mime_ensure() != 0)
        return -1;
    mime_type = magic_file(mime_cookie, filepath);
    if (mime_type == NULL)
        return -1;
    snprintf(mime, 128, "%s", mime_type);
    return 0;
}

static int getMIME_cached(const char *filepath, const struct stat *st, char mime[128]) {
    if (mime_cache_set && mime_cache_size == st->st_size &&
        mime_cache_mtime == st->st_mtime && mime_cache_nsec == st->st_mtim.tv_nsec &&
        strcmp(mime_cache_path, filepath) == 0) {
        snprintf(mime, 128, "%s", mime_cache_val);
        return 0;
    }
    if (getMIME(filepath, mime) != 0)
        return -1;
    snprintf(mime_cache_path, sizeof mime_cache_path, "%s", filepath);
    mime_cache_size = st->st_size;
    mime_cache_mtime = st->st_mtime;
    mime_cache_nsec = st->st_mtim.tv_nsec;
    snprintf(mime_cache_val, sizeof mime_cache_val, "%s", mime);
    mime_cache_set = 1;
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

    int safe_width = cols - 3; // reserve right column for scrollbar (inside preview_win)

    /* Enforce last-line stop: max scroll so last line sits at bottom */
    long max_scroll = (total > visible) ? (total - visible) : 0;
    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > max_scroll) preview_scroll = (int)max_scroll;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, false);
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
        wprintw(preview_win, "%.*s", safe_width, line);
    }
    fclose(fp);

    drawScrollbar(preview_win, (int)total, preview_scroll, false);
    wrefresh(preview_win);
}

/* Small helper for preview visible height (used by clamping) */
static inline int get_preview_visible_rows(void) {
	if (!preview_win) return 20;
	int r = getmaxy(preview_win);
	return (r > 0) ? r : 1;
}

/* Preview scrolling helpers with proper last-line clamping (total - visible) */
void previewScrollUp(void) {
	if (preview_scroll > 0) {
		preview_scroll--;
	}
}

void previewScrollDown(void) {
	int visible = get_preview_visible_rows();
	long total = (len_preview > 0) ? len_preview : preview_total_lines;

	if (total > 0) {
		long max_start = total - visible;
		if (max_start < 0) max_start = 0;

		preview_scroll++;
		if (preview_scroll > max_start) {
			preview_scroll = (int)max_start;
		}
	} else {
		preview_scroll++;
	}
}

void previewPageUp(void) {
	preview_scroll -= 10;
	if (preview_scroll < 0) preview_scroll = 0;
}

void previewPageDown(void) {
	int visible = get_preview_visible_rows();
	long total = (len_preview > 0) ? len_preview : preview_total_lines;

	if (total > 0) {
		long max_start = total - visible;
		if (max_start < 0) max_start = 0;

		preview_scroll += visible;
		if (preview_scroll > max_start) {
			preview_scroll = (int)max_start;
		}
	} else {
		preview_scroll += 10;
	}
}

static void render_hex_preview(const char *path, int maxx) {
    long total = preview_total_lines;
    if (total < 0) total = 0;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    (void)rows;
    int safe_width = cols - 3; // reserve right column for scrollbar (layout consistency)
    (void)safe_width;
    int visible = cols > 10 ? (cols / 4) : 1;   /* rough number of hex lines that fit */

    long max_scroll = (total > visible) ? (total - visible) : 0;
    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > max_scroll) preview_scroll = (int)max_scroll;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        preview_print_message("Preview unavailable: can't open file", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, false);
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
        drawScrollbar(preview_win, (int)total, preview_scroll, false);
        wrefresh(preview_win);
        return;
    }

    size_t max_bytes = (size_t)avail_lines * bytes_per_line;
    unsigned char *buf = (unsigned char *)malloc(max_bytes);
    if (!buf) {
        fclose(fp);
        preview_print_message("Preview unavailable: out of memory", maxx);
        drawScrollbar(preview_win, (int)total, preview_scroll, false);
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
    drawScrollbar(preview_win, (int)total, preview_scroll, false);
    wrefresh(preview_win);
}

static int ext_is_raster(const char *path) {
    const char *base = strrchr(path, '/');
    const char *dot;
    static const char *exts[] = {
        "jpg", "jpeg", "png", "gif", "webp", "bmp", "tif", "tiff",
        "pnm", "pbm", "pgm", "ppm", "avif", "heic", "heif", "jxl", "ico", NULL
    };
    int i;
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    if (!dot || !dot[1])
        return 0;
    for (i = 0; exts[i]; i++) {
        if (strcasecmp(dot + 1, exts[i]) == 0)
            return 1;
    }
    return 0;
}

static int is_text_mime(const char *mime) {
    return strncmp(mime, "text/", 5) == 0 ||
           strstr(mime, "json") != NULL ||
           strstr(mime, "xml") != NULL ||
           strstr(mime, "javascript") != NULL ||
           strstr(mime, "x-shellscript") != NULL;
}

static char dim_cache_path[PATH_MAX];
static off_t dim_cache_size = -1;
static time_t dim_cache_mtime = 0;
static long dim_cache_nsec = 0;
static int dim_cache_w, dim_cache_h, dim_cache_set, dim_cache_ok;

static int image_dimensions_cached(const char *path, const struct stat *st, int *w, int *h) {
    int ok;
    if (dim_cache_set && dim_cache_size == st->st_size &&
        dim_cache_mtime == st->st_mtime && dim_cache_nsec == st->st_mtim.tv_nsec &&
        strcmp(dim_cache_path, path) == 0) {
        *w = dim_cache_w;
        *h = dim_cache_h;
        return dim_cache_ok;
    }
    ok = image_dimensions(path, w, h);
    snprintf(dim_cache_path, sizeof dim_cache_path, "%s", path);
    dim_cache_size = st->st_size;
    dim_cache_mtime = st->st_mtime;
    dim_cache_nsec = st->st_mtim.tv_nsec;
    dim_cache_w = *w;
    dim_cache_h = *h;
    dim_cache_set = 1;
    dim_cache_ok = ok;
    return ok;
}

static unsigned long long thumb_fnv(const char *s) {
    unsigned long long h = 14695981039346656037ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static int exe_on_path(const char *name) {
    const char *path = getenv("PATH");
    const char *p;
    if (!name || !name[0])
        return 0;
    if (!path || !path[0])
        path = "/usr/bin:/bin:/usr/local/bin";
    p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t len = colon ? (size_t)(colon - p) : strlen(p);
        char buf[PATH_MAX];
        if (len > 0 && len + 1 + strlen(name) + 1 < sizeof buf) {
            memcpy(buf, p, len);
            buf[len] = '/';
            memcpy(buf + len + 1, name, strlen(name) + 1);
            if (access(buf, X_OK) == 0)
                return 1;
        }
        if (!colon)
            break;
        p = colon + 1;
    }
    return 0;
}

static const char *thumb_tool(void) {
    static int checked = 0;
    static const char *tool = NULL;
    if (checked)
        return tool;
    checked = 1;
    if (getenv("CFL_NO_THUMB") != NULL)
        return NULL;
    if (exe_on_path("ffmpeg"))
        tool = "ffmpeg";
    else if (exe_on_path("magick"))
        tool = "magick";
    else if (exe_on_path("convert"))
        tool = "convert";
    return tool;
}

static pid_t thumb_pid = -1;
static int thumb_waiting = 0;
static char thumb_wait_dst[PATH_MAX];
static char thumb_fail[PATH_MAX];
static off_t thumb_fail_size = -1;
static time_t thumb_fail_mtime = 0;

static void thumb_reap(void) {
    int st;
    pid_t r;
    if (thumb_pid <= 0)
        return;
    r = waitpid(thumb_pid, &st, WNOHANG);
    if (r == thumb_pid || (r < 0 && errno == ECHILD))
        thumb_pid = -1;
}

static int thumb_dir_ready(char *dir, size_t dirsz) {
    static int ready = 0;
    const char *base = getenv("XDG_CACHE_HOME");
    const char *home;
    char tmp[PATH_MAX];
    char *p;
    int n;

    if (base && base[0])
        n = snprintf(dir, dirsz, "%s/cfl/thumbs", base);
    else {
        home = (info && info->pw_dir && info->pw_dir[0]) ? info->pw_dir : getenv("HOME");
        if (!home || !home[0])
            return -1;
        n = snprintf(dir, dirsz, "%s/.cache/cfl/thumbs", home);
    }
    if (n < 0 || (size_t)n >= dirsz)
        return -1;
    if (ready)
        return 0;
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
        return -1;
    ready = 1;
    return 0;
}

static void thumb_spawn(const char *src, const char *dst) {
    const char *tool = thumb_tool();
    char tmp[PATH_MAX];
    char vf[80];
    char geo[32];
    pid_t pid;

    if (!tool || thumb_pid > 0)
        return;
    /* Keep a .jpg suffix. ffmpeg picks the muxer from the extension, and
     * ".part" is not a format it will write. */
    if (snprintf(tmp, sizeof tmp, "%s.tmp.jpg", dst) >= (int)sizeof tmp)
        return;
    snprintf(vf, sizeof vf, "scale=%d:%d:force_original_aspect_ratio=decrease", THUMB_EDGE, THUMB_EDGE);
    snprintf(geo, sizeof geo, "%dx%d>", THUMB_EDGE, THUMB_EDGE);

    pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        pid_t g;
        int st = 1;
        int dn;
        /* Stay out of the terminal's way, and finish even if cfl quits
         * (the session leader's exit delivers SIGHUP to this group). */
        signal(SIGHUP, SIG_IGN);
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        if (ueberzug_fd >= 0)
            close(ueberzug_fd);
        dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, STDIN_FILENO);
            dup2(dn, STDOUT_FILENO);
            dup2(dn, STDERR_FILENO);
            if (dn > 2)
                close(dn);
        }
        g = fork();
        if (g == 0) {
            if (strcmp(tool, "ffmpeg") == 0) {
                execlp("ffmpeg", "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                       "-y", "-i", src, "-vf", vf, "-frames:v", "1", "-f", "image2",
                       tmp, (char *)NULL);
            } else if (strcmp(tool, "magick") == 0) {
                execlp("magick", "magick", src, "-thumbnail", geo, tmp, (char *)NULL);
            } else {
                execlp("convert", "convert", src, "-thumbnail", geo, tmp, (char *)NULL);
            }
            _exit(127);
        }
        if (g > 0)
            waitpid(g, &st, 0);
        if (g > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0)
            rename(tmp, dst);
        else
            unlink(tmp);
        _exit(0);
    }
    thumb_pid = pid;
}

/* Ready thumbnail path, or NULL to display the original. Generation is
 * asynchronous: the first visit is not delayed, the next one uses the thumb. */
static const char *thumb_for(const char *src, const struct stat *st, int w, int h) {
    static char dst[PATH_MAX];
    char dir[PATH_MAX];
    int big;
    int n;

    if (getenv("CFL_NO_THUMB") != NULL || thumb_tool() == NULL)
        return NULL;
    big = (st->st_size >= (off_t)THUMB_MIN_BYTES) ||
          (w > 0 && h > 0 && (long)w * (long)h >= (long)THUMB_MIN_PIXELS);
    if (!big)
        return NULL;
    if (thumb_dir_ready(dir, sizeof dir) != 0)
        return NULL;
    n = snprintf(dst, sizeof dst, "%s/%016llx_%lld_%ld_%lld.jpg", dir, thumb_fnv(src),
                 (long long)st->st_mtime, st->st_mtim.tv_nsec, (long long)st->st_size);
    if (n < 0 || (size_t)n >= sizeof dst)
        return NULL;
    if (access(dst, R_OK) == 0)
        return dst;

    thumb_reap();
    if (thumb_waiting && thumb_pid < 0 && access(thumb_wait_dst, R_OK) != 0) {
        snprintf(thumb_fail, sizeof thumb_fail, "%s", src);
        thumb_fail_size = st->st_size;
        thumb_fail_mtime = st->st_mtime;
        thumb_waiting = 0;
    }
    if (access(dst, R_OK) == 0)
        return dst;
    if (thumb_fail[0] && thumb_fail_size == st->st_size &&
        thumb_fail_mtime == st->st_mtime && strcmp(thumb_fail, src) == 0)
        return NULL;
    if (thumb_pid > 0)
        return NULL;
    thumb_spawn(src, dst);
    snprintf(thumb_wait_dst, sizeof thumb_wait_dst, "%s", dst);
    thumb_waiting = 1;
    return NULL;
}

static void show_image_preview(const char *filepath, const struct stat *st, int got_dims, int iw, int ih) {
    const char *thumb = thumb_for(filepath, st, got_dims ? iw : 0, got_dims ? ih : 0);
    const char *shown = thumb ? thumb : filepath;
    int dw = iw, dh = ih, have = got_dims ? 1 : 0;
    if (thumb) {
        int tw = 0, th = 0;
        have = image_dimensions(thumb, &tw, &th) ? 1 : 0;
        dw = tw;
        dh = th;
    }
    ueberzugpp_add(shown, have, dw, dh);
}

static void getPreview(char *filepath, int maxy, int maxx) {
    (void)maxy;
    struct stat st;
    char mime[128] = {0};
    int iw = 0, ih = 0;
    int got_dims;

    if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
        ueberzugpp_remove();
        preview_print_message("Preview unavailable: not a regular file", maxx);
        return;
    }

    if ((size_t)st.st_size > MAX_PREVIEW_BYTES) {
        ueberzugpp_remove();
        preview_print_message("Preview unavailable: file too large", maxx);
        return;
    }

    /* Header probe is enough for the formats ueberzug actually draws, and it
     * avoids libmagic (database load + a second read of the file). */
    got_dims = image_dimensions_cached(filepath, &st, &iw, &ih);
    if (got_dims || ext_is_raster(filepath)) {
        show_image_preview(filepath, &st, got_dims, iw, ih);
        return;
    }

    if (getMIME_cached(filepath, &st, mime) != 0) {
        ueberzugpp_remove();
        preview_print_message("Preview unavailable: MIME detection failed", maxx);
        return;
    }

    if (strncmp(mime, "image/", 6) == 0) {
        show_image_preview(filepath, &st, 0, 0, 0);
        return;
    }

    /* Non-image preview: must clear any ueberzug layer */
    ueberzugpp_remove();

    /* Cache total for text/hex scrollbar (only on target change) */
    if (strcmp(preview_target_path, filepath) != 0) {
        strncpy(preview_target_path, filepath, sizeof(preview_target_path)-1);
        preview_target_path[sizeof(preview_target_path)-1] = '\0';

        if (is_text_mime(mime)) {
            preview_total_lines = count_text_lines(filepath);
        } else {
            size_t bpl = 16;
            preview_total_lines = (st.st_size + bpl - 1) / bpl;
        }
    }

    if (is_text_mime(mime))
        render_text_preview(filepath, maxx);
    else
        render_hex_preview(filepath, maxx);
}

/* not_reg / is_link may be NULL; then each row is stat'd (slow path). */
void PreviewNextDir(char *next_dir_arg, char **next_directories,
                    const unsigned char *not_reg, const unsigned char *is_link) {
    /* Directory listing is not an image — clear ueberzug if still up */
    ueberzugpp_remove();

    int rows = getmaxy(preview_win);
    int visible = rows > 0 ? rows : 1;
    wclear(preview_win);

    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > len_preview) preview_scroll = len_preview;

    int start = preview_scroll;
    for (int i = start; i < len_preview && (i - start) < visible; i++) {
        int y = i - start + 1;
        int entry_dir;
        int entry_link;
        int have_path = 0;
        wmove(preview_win, y, 2);
        if (not_reg) {
            entry_dir = not_reg[i];
        } else {
            if (build_path(temp_dir, sizeof temp_dir, next_dir_arg, next_directories[i]) != 0)
                continue;
            have_path = 1;
            entry_dir = (isRegularFile(temp_dir) == 0);
        }
        if (is_link) {
            entry_link = is_link[i];
        } else if (have_path) {
            entry_link = isSymlink(temp_dir);
        } else if (build_path(temp_dir, sizeof temp_dir, next_dir_arg, next_directories[i]) == 0) {
            have_path = 1;
            entry_link = isSymlink(temp_dir);
        } else {
            entry_link = 0;
        }
        if (entry_dir)
            wattron(preview_win, A_BOLD);
        else
            wattroff(preview_win, A_BOLD);

        /* Symlink display in right-pane directory listing */
        char entry_display[PATH_MAX * 2 + 32];
        if (entry_link) {
            char target[PATH_MAX];
            if (!have_path && build_path(temp_dir, sizeof temp_dir, next_dir_arg, next_directories[i]) != 0)
                continue;
            if (readSymlinkTarget(temp_dir, target, sizeof target) == 0)
                snprintf(entry_display, sizeof entry_display, "%s → %s", next_directories[i], target);
            else
                snprintf(entry_display, sizeof entry_display, "%s → (unreadable)", next_directories[i]);
        } else {
            snprintf(entry_display, sizeof entry_display, "%s", next_directories[i]);
        }
        {
            int pw = getmaxx(preview_win);
            int safe_width = pw - 3; // reserve right column for scrollbar
            if ((int)strlen(entry_display) > safe_width) {
                entry_display[safe_width] = '\0';
            }
            wprintw(preview_win, "%s\n", entry_display);
        }
    }

    /* Right scrollbar for preview directory listing (symmetric to file list bar) */
    drawScrollbar(preview_win, len_preview, start, false);
}

static void preview_housekeep(void) {
    int st;
    pid_t r;
    thumb_reap();
    if (ueberzug_pid <= 0)
        return;
    r = waitpid(ueberzug_pid, &st, WNOHANG);
    if (r == ueberzug_pid) {
        if (ueberzug_fd >= 0) {
            close(ueberzug_fd);
            ueberzug_fd = -1;
        }
        ueberzug_pid = -1;
        ueberzug_failed = 1;
        ueberzug_showing = 0;
        ueberzug_pending_len = 0;
        ueberzug_pending_off = 0;
    }
}

static void preview_shutdown(void) {
    /* Do not kill the thumbnail child. It is in its own process group and
     * finishes the .part -> final rename after cfl has quit, so the next
     * visit is a cache hit. Reap only if it has already exited. */
    if (thumb_pid > 0) {
        waitpid(thumb_pid, NULL, WNOHANG);
        thumb_pid = -1;
    }
    ueberzugpp_stop();
    mime_shutdown();
}
#endif
