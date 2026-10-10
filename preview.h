#ifndef CFL_PREVIEW_H
#define CFL_PREVIEW_H

#include <ctype.h>
#include "cfl_core.h"
int preview_start;     /* column for preview pane start / right scrollbar position */
int preview_scroll = 0; /* current scroll offset for preview pane (dir listings + text/hex) */
long preview_total_lines = 0; /* cached total for current text/hex preview (lines or hex-rows) */

int len_preview = 0;
static pid_t ueberzug_pid = -1;
static FILE *ueberzug_in = NULL;
static int ueberzug_failed = 0;
int clearFlagImg = 0;

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
        kill(ueberzug_pid, SIGTERM);
        while (waitpid(ueberzug_pid, NULL, 0) < 0 && errno == EINTR) {}
        ueberzug_failed = 1;
        ueberzug_pid = -1;
        return;
    }
    setvbuf(ueberzug_in, NULL, _IOLBF, 0);
}

static void ueberzugpp_remove(void) {
    if (!ueberzug_in) return;

    char escapedIdentifier[512];
    if (CflJsonEscape(UEBERZUGPP_IDENTIFIER, escapedIdentifier, sizeof escapedIdentifier) == 0) {
        fprintf(ueberzug_in, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", escapedIdentifier);
        fflush(ueberzug_in);
    }
    ueberzug_showing = 0;
    ueberzug_last_w = 0;
    ueberzug_last_h = 0;
}
/* Add/replace image. Clear only when needed:
 *  - not currently showing an image (nothing to skip)
 *  - new image smaller in width OR height than previous (ghosting)
 *  - dimensions unknown (safe clear)
 * Equal or larger: splash on top without remove (fast scrolling). */
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

    char escapedPath[PATH_MAX * 6 + 1];
    char escapedIdentifier[512];
    if (CflJsonEscape(img_path, escapedPath, sizeof escapedPath) != 0 ||
        CflJsonEscape(UEBERZUGPP_IDENTIFIER, escapedIdentifier, sizeof escapedIdentifier) != 0)
        return;

    int iw = 0, ih = 0;
    int got_dims = image_dimensions(img_path, &iw, &ih);
    int need_clear = 0;
    if (ueberzug_showing &&
        (!got_dims || iw < ueberzug_last_w || ih < ueberzug_last_h))
        need_clear = 1;

    if (need_clear) {
        fprintf(ueberzug_in, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", escapedIdentifier);
        fflush(ueberzug_in);
        ueberzug_showing = 0;
        ueberzug_last_w = 0;
        ueberzug_last_h = 0;
    }

    fprintf(ueberzug_in,
            "{\"action\":\"add\",\"identifier\":\"%s\",\"path\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}\n",
            escapedIdentifier, escapedPath, x, y, w, h);
    fflush(ueberzug_in);

    ueberzug_showing = 1;
    if (got_dims) {
        ueberzug_last_w = iw;
        ueberzug_last_h = ih;
    } else {
        ueberzug_last_w = 0;
        ueberzug_last_h = 0;
    }
}

static void ueberzugpp_stop(void) {
    if (ueberzug_in) {
        ueberzugpp_remove();
        fclose(ueberzug_in);
        ueberzug_in = NULL;
    }
    if (ueberzug_pid > 0) {
        kill(ueberzug_pid, SIGTERM);
        while (waitpid(ueberzug_pid, NULL, 0) < 0 && errno == EINTR) {}
        ueberzug_pid = -1;
    }
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
	int r, c;
	getmaxyx(preview_win, r, c);
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

static void getPreview(char *filepath, int maxy, int maxx) {
    (void)maxy;
    struct stat st;
    char mime[128] = {0};

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

    if (getMIME(filepath, mime) != 0) {
        ueberzugpp_remove();
        preview_print_message("Preview unavailable: MIME detection failed", maxx);
        return;
    }

    if (strncmp(mime, "image/", 6) == 0) {
        /* Do not set clearFlagImg — that used to remove the image every next frame */
        ueberzugpp_add(filepath);
        return;
    }

    /* Non-image preview: must clear any ueberzug layer */
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
    /* Directory listing is not an image — clear ueberzug if still up */
    ueberzugpp_remove();

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int visible = rows > 0 ? rows : 1;
    wclear(preview_win);

    if (preview_scroll < 0) preview_scroll = 0;
    if (preview_scroll > len_preview) preview_scroll = len_preview;

    int start = preview_scroll;
    for (int i = start; i < len_preview && (i - start) < visible; i++) {
        int y = i - start + 1;
        wmove(preview_win, y, 2);
        snprintf(temp_dir, sizeof(temp_dir), "%s/%s", next_dir_arg, next_directories[i]);
        if (isDirectory(temp_dir)) {
            wattron(preview_win, A_BOLD);
        } else {
            wattroff(preview_win, A_BOLD);
        }

        /* Symlink display in right-pane directory listing */
        char entry_display[PATH_MAX * 2 + 32];
        if (isSymlink(temp_dir)) {
            char target[PATH_MAX];
            if (readSymlinkTarget(temp_dir, target, sizeof target) == 0) {
                snprintf(entry_display, sizeof entry_display, "%s → %s", next_directories[i], target);
            } else {
                snprintf(entry_display, sizeof entry_display, "%s → (unreadable)", next_directories[i]);
            }
        } else {
            snprintf(entry_display, sizeof entry_display, "%s", next_directories[i]);
        }
        {
            int rows, pw;
            getmaxyx(preview_win, rows, pw);
            (void)rows;
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
#endif
