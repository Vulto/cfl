/* mmenu.h - single-header fuzzy menu using ncursesw (stb-style) */
/* Public domain or MIT */

#ifndef MMENU_H
#define MMENU_H

/* Enable wide-character functions in ncurses */
#define _XOPEN_SOURCE_EXTENDED
#define _GNU_SOURCE   /* for strcasestr on glibc */

#include <ncurses.h>
#include <ctype.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int mmenu(const char *const *options, int n_options, const char *prompt);

/* Fuzzy picker inside an existing WINDOW (does not init/teardown ncurses).
 * Draws bookmark-style chrome: box, title, prompt, list, footer.
 * Returns selected option index, or -1 on cancel. */
int mmenu_in_window(WINDOW *win,
                    const char *const *options, int n_options,
                    const char *title, const char *prompt);

#endif /* MMENU_H */

#ifdef MMENU_IMPLEMENTATION

#define MAX_INPUT_LEN 128
#define INITIAL_CAP 128

static volatile sig_atomic_t resize_flag = 0;

static void handle_resize(int sig) { (void)sig; resize_flag = 1; }

static wchar_t *mb_to_wc(const char *s) {
    if (!s) return NULL;
    size_t n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) return NULL;
    wchar_t *w = malloc((n + 1) * sizeof(wchar_t));
    if (w) mbstowcs(w, s, n + 1);
    return w;
}

/* Convert the (small) current search input from wchar to multibyte for fast byte matching.
   Only called once per keystroke, not per candidate. */
static char *wc_to_mb(const wchar_t *w) {
    if (!w || !*w) return strdup("");
    size_t n = wcstombs(NULL, w, 0);
    if (n == (size_t)-1) return NULL;
    char *m = malloc(n + 1);
    if (m) wcstombs(m, w, n + 1);
    return m;
}

typedef struct { int *indices; int cap; int count; } filt;

static void filt_init(filt *f) {
    f->cap = INITIAL_CAP;
    f->indices = malloc(f->cap * sizeof(int));
    if (!f->indices) { perror("malloc"); exit(EXIT_FAILURE); }
    f->count = 0;
}

static void filt_push(filt *f, int idx) {
    if (f->count == f->cap) {
        f->cap *= 2;
        f->indices = realloc(f->indices, f->cap * sizeof(int));
        if (!f->indices) { perror("realloc"); exit(EXIT_FAILURE); }
    }
    f->indices[f->count++] = idx;
}

static void filt_clear(filt *f) { f->count = 0; }

/* Case-insensitive substring (portable; no strcasestr dependency). */
static const char *mmenu_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return p;
        if (!p[i] && i < nlen) break; /* remaining hay too short */
    }
    return NULL;
}

static void filt_fill_all(filt *f, int n_options) {
    filt_clear(f);
    for (int i = 0; i < n_options; i++) filt_push(f, i);
}

/* Sort key for ranking matches: shorter paths first, then earlier match, then index. */
typedef struct {
    int idx;
    int path_len;
    int depth;
    int match_pos;
} mmenu_rank_full;

static int mmenu_path_depth(const char *p) {
    int d = 0;
    if (!p) return 0;
    for (; *p; p++)
        if (*p == '/') d++;
    return d;
}

static int mmenu_rank_cmp(const void *a, const void *b) {
    const mmenu_rank_full *ra = a;
    const mmenu_rank_full *rb = b;
    if (ra->path_len != rb->path_len)
        return ra->path_len - rb->path_len; /* shorter paths first */
    if (ra->depth != rb->depth)
        return ra->depth - rb->depth;
    if (ra->match_pos != rb->match_pos)
        return ra->match_pos - rb->match_pos;
    return ra->idx - rb->idx;
}

/* Reorder filtered indices: prioritize short paths (and earlier substring matches). */
static void filt_sort_short_paths(filt *f, const char *const *options, const char *q) {
    if (!f || f->count <= 1 || !options) return;

    mmenu_rank_full *ranks = malloc((size_t)f->count * sizeof(mmenu_rank_full));
    if (!ranks) return;

    for (int i = 0; i < f->count; i++) {
        int oidx = f->indices[i];
        const char *p = options[oidx];
        ranks[i].idx = oidx;
        ranks[i].path_len = p ? (int)strlen(p) : 0;
        ranks[i].depth = mmenu_path_depth(p);
        ranks[i].match_pos = 0;
        if (q && q[0] && p) {
            const char *m = mmenu_strcasestr(p, q);
            ranks[i].match_pos = m ? (int)(m - p) : 0x7fffffff;
        }
    }

    qsort(ranks, (size_t)f->count, sizeof(mmenu_rank_full), mmenu_rank_cmp);

    for (int i = 0; i < f->count; i++)
        f->indices[i] = ranks[i].idx;

    free(ranks);
}

static void filt_apply_query(filt *f, const char *const *options, int n_options, const char *q) {
    filt_clear(f);
    if (!q || q[0] == '\0') {
        filt_fill_all(f, n_options);
        filt_sort_short_paths(f, options, NULL);
        return;
    }
    for (int i = 0; i < n_options; i++) {
        if (mmenu_strcasestr(options[i], q)) filt_push(f, i);
    }
    filt_sort_short_paths(f, options, q);
}

/* Refine existing matches when the user appended a character (common path). */
static int filt_refine(filt *f, const char *const *options, const char *q) {
    filt newf;
    newf.cap = f->count < INITIAL_CAP ? INITIAL_CAP : f->count;
    newf.indices = malloc(newf.cap * sizeof(int));
    if (!newf.indices) return 0;
    newf.count = 0;
    for (int k = 0; k < f->count; k++) {
        int oidx = f->indices[k];
        if (mmenu_strcasestr(options[oidx], q)) {
            if (newf.count == newf.cap) {
                newf.cap *= 2;
                int *grown = realloc(newf.indices, newf.cap * sizeof(int));
                if (!grown) { free(newf.indices); return 0; }
                newf.indices = grown;
            }
            newf.indices[newf.count++] = oidx;
        }
    }
    free(f->indices);
    *f = newf;
    filt_sort_short_paths(f, options, q);
    return 1;
}

static void mmenu_draw_modal(WINDOW *win,
                             const char *const *options,
                             filt *filtered,
                             const wchar_t *input, int input_len,
                             int selection, int top,
                             const char *title, const char *prompt_str)
{
    int win_h, win_w;
    getmaxyx(win, win_h, win_w);

    werase(win);
    box(win, 0, 0);

    const char *t = title ? title : " Search ";
    int title_len = (int)strlen(t);
    if (title_len < win_w - 2)
        mvwprintw(win, 0, (win_w - title_len) / 2, "%s", t);

    const char *ps = prompt_str ? prompt_str : "> ";
    int prompt_y = 1;
    mvwprintw(win, prompt_y, 2, "%s", ps);
    /* Print query after prompt (truncate to fit) */
    int q_col = 2 + (int)strlen(ps);
    int q_max = win_w - q_col - 2;
    if (q_max < 1) q_max = 1;
    char *qmb = wc_to_mb(input);
    if (qmb) {
        int qlen = (int)strlen(qmb);
        if (qlen > q_max) {
            mvwprintw(win, prompt_y, q_col, "%.*s", q_max, qmb + (qlen - q_max));
        } else {
            mvwprintw(win, prompt_y, q_col, "%s", qmb);
        }
        free(qmb);
    }

    /* Separator under prompt */
    int sep_y = 2;
    mvwaddch(win, sep_y, 1, ACS_HLINE);
    for (int i = 2; i < win_w - 1; i++) mvwaddch(win, sep_y, i, ACS_HLINE);

    int list_start_y = 3;
    int footer_y = win_h - 2;
    int visible = footer_y - 1 - list_start_y;
    if (visible < 1) visible = 1;

    int max_name_cols = win_w - 4;
    if (max_name_cols < 1) max_name_cols = 1;

    if (filtered->count == 0) {
        mvwprintw(win, list_start_y + 1, 2, "No matches.");
    } else {
        for (int v = 0; v < visible && top + v < filtered->count; v++) {
            int fidx = top + v;
            int oidx = filtered->indices[fidx];
            int row_y = list_start_y + v;
            int is_sel = (fidx == selection);

            if (is_sel) wattron(win, A_REVERSE | A_BOLD);

            const char *name = options[oidx] ? options[oidx] : "";
            int nlen = (int)strlen(name);
            if (nlen > max_name_cols)
                mvwprintw(win, row_y, 2, "%.*s", max_name_cols, name);
            else
                mvwprintw(win, row_y, 2, "%s", name);

            /* Clear rest of row under reverse attr when selected */
            if (is_sel) {
                int used = nlen > max_name_cols ? max_name_cols : nlen;
                for (int c = 2 + used; c < win_w - 1; c++)
                    mvwaddch(win, row_y, c, ' ');
                wattroff(win, A_REVERSE | A_BOLD);
            }
        }
    }

    /* Footer separator + help */
    mvwaddch(win, footer_y - 1, 1, ACS_HLINE);
    for (int i = 2; i < win_w - 1; i++) mvwaddch(win, footer_y - 1, i, ACS_HLINE);
    mvwprintw(win, footer_y, 2, "Enter=Jump  q=Close  \u2191\u2193=Navigate");

    /* Cursor on the query line for typing feedback */
    int cursor_col = q_col + (input_len < q_max ? input_len : q_max);
    if (cursor_col >= win_w - 1) cursor_col = win_w - 2;
    wmove(win, prompt_y, cursor_col);
    curs_set(1);
    wrefresh(win);
    (void)input_len;
}

int mmenu_in_window(WINDOW *win,
                    const char *const *options, int n_options,
                    const char *title, const char *prompt)
{
    if (!win || !options || n_options < 0) return -1;

    keypad(win, TRUE);
    meta(win, TRUE);

    wchar_t input[MAX_INPUT_LEN + 1] = {0};
    int input_len = 0;
    int prev_input_len = 0;

    filt filtered;
    filt_init(&filtered);
    filt_fill_all(&filtered, n_options);
    filt_sort_short_paths(&filtered, options, NULL);

    int selection = 0;
    int top = 0;
    int ret = -1;

    const char *prompt_str = prompt ? prompt : "> ";

    while (1) {
        int win_h, win_w;
        getmaxyx(win, win_h, win_w);
        (void)win_w;
        int list_start_y = 3;
        int footer_y = win_h - 2;
        int visible = footer_y - 1 - list_start_y;
        if (visible < 1) visible = 1;

        if (filtered.count > 0) {
            if (selection < 0) selection = 0;
            if (selection >= filtered.count) selection = filtered.count - 1;
            if (selection < top) top = selection;
            else if (selection >= top + visible) top = selection - visible + 1;
            if (top < 0) top = 0;
            int max_top = filtered.count - visible;
            if (max_top < 0) max_top = 0;
            if (top > max_top) top = max_top;
        } else {
            selection = 0;
            top = 0;
        }

        mmenu_draw_modal(win, options, &filtered, input, input_len,
                         selection, top, title, prompt_str);

        wint_t ch;
        int kc = wget_wch(win, &ch);
        if (kc == ERR) continue;

        int need_filter = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_UP && selection > 0) selection--;
            else if (ch == KEY_DOWN && selection < filtered.count - 1) selection++;
            else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127 || ch == 8) {
                if (input_len > 0) { input[--input_len] = L'\0'; need_filter = 1; }
            }
            else if (ch == KEY_ENTER) {
                if (filtered.count > 0) ret = filtered.indices[selection];
                break;
            }
        } else {
            /* ASCII / wide printable */
            if (ch == 27 || ch == 3 || ch == 4) {
                ret = -1;
                break;
            }
            /* q closes only when the query is empty (so "q" remains searchable) */
            if (ch == 'q' && input_len == 0) {
                ret = -1;
                break;
            }
            if (ch == '\n' || ch == '\r') {
                if (filtered.count > 0) ret = filtered.indices[selection];
                break;
            }
            /* j/k are searchable characters; navigate with arrows only */
            if (iswprint(ch) && input_len < MAX_INPUT_LEN) {
                input[input_len++] = (wchar_t)ch;
                input[input_len] = L'\0';
                need_filter = 1;
            }
        }

        if (need_filter) {
            char *q = wc_to_mb(input);
            int do_full = 1;
            if (q && input_len > prev_input_len && filtered.count > 0) {
                if (filt_refine(&filtered, options, q))
                    do_full = 0;
            }
            if (do_full)
                filt_apply_query(&filtered, options, n_options, q);
            if (q) free(q);
            prev_input_len = input_len;
            selection = 0;
            top = 0;
        }
    }

    curs_set(0);
    free(filtered.indices);
    return ret;
}

int mmenu(const char *const *options, int n_options, const char *prompt) {
    setlocale(LC_ALL, "");

    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) { perror("fopen /dev/tty"); exit(EXIT_FAILURE); }

    SCREEN *scr = newterm(NULL, tty, tty);
    if (!scr) { fclose(tty); fprintf(stderr, "Failed to init ncurses\n"); return -1; }
    set_term(scr);

    cbreak(); noecho(); keypad(stdscr, TRUE);
    signal(SIGWINCH, handle_resize);

    int rows, cols; getmaxyx(stdscr, rows, cols);

    wchar_t input[MAX_INPUT_LEN + 1] = {0};
    int input_len = 0;

    filt filtered; filt_init(&filtered);

    int selection = 0;
    int top = 0;
    int need_filter = 0;
    int prev_input_len = 0;   /* for incremental filter optimization */

    const char *prompt_str = prompt ? prompt : "> ";
    size_t prompt_len = strlen(prompt_str);

    int ret = -1;

    /* Initial filter (show all for empty query - short paths first) */
    filt_fill_all(&filtered, n_options);
    filt_sort_short_paths(&filtered, options, NULL);
    if (filtered.count > 0) selection = 0;

    /* Initial draw */
    clear();
    printw("%s%ls", prompt_str, input); clrtoeol();
    move(0, (int)(prompt_len + input_len));
    int visible = rows - 1;
    for (int v = 0; v < visible && top + v < filtered.count; v++) {
        int fidx = top + v;
        int oidx = filtered.indices[fidx];
        wchar_t *w = mb_to_wc(options[oidx]);
        if (w) {
            move(v + 1, 0);
            if (fidx == selection) attron(A_STANDOUT);
            printw("%.*ls", cols, w);
            if (fidx == selection) attroff(A_STANDOUT);
            clrtoeol();
            free(w);
        }
    }
    refresh();

    while (1) {
        if (resize_flag) {
            resize_flag = 0;
            endwin();
            refresh();
            getmaxyx(stdscr, rows, cols);
            visible = rows - 1;
        }

        wint_t ch;
        int kc = wget_wch(stdscr, &ch);
        if (kc == ERR) continue;

        need_filter = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_UP && selection > 0) selection--;
            else if (ch == KEY_DOWN && selection < filtered.count - 1) selection++;
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (input_len > 0) { input[--input_len] = L'\0'; need_filter = 1; }
            }
        } else {
            if (ch == 27 || ch == 3 || ch == 4) { ret = -1; goto cleanup; }
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (filtered.count > 0) ret = filtered.indices[selection];
                goto cleanup;
            }
            if (iswprint(ch) && input_len < MAX_INPUT_LEN) {
                input[input_len++] = ch;
                input[input_len] = L'\0';
                need_filter = 1;
            }
        }

        if (need_filter) {
            char *q = wc_to_mb(input);
            int do_full = 1;
            if (q && input_len > prev_input_len && filtered.count > 0) {
                if (filt_refine(&filtered, options, q))
                    do_full = 0;
            }
            if (do_full)
                filt_apply_query(&filtered, options, n_options, q);
            if (q) free(q);
            prev_input_len = input_len;
            selection = 0;
            top = 0;
        }

        /* Scroll */
        visible = rows - 1;
        if (filtered.count > 0) {
            if (selection < top) top = selection;
            else if (selection >= top + visible) top = selection - visible + 1;
            if (top < 0) top = 0;
            int max_top = filtered.count - visible;
            if (max_top < 0) max_top = 0;
            if (top > max_top) top = max_top;
        } else {
            top = 0;
        }

        /* Redraw */
        clear();
        printw("%s%ls", prompt_str, input); clrtoeol();
        move(0, (int)(prompt_len + input_len));
        for (int v = 0; v < visible && top + v < filtered.count; v++) {
            int fidx = top + v;
            int oidx = filtered.indices[fidx];
            wchar_t *w = mb_to_wc(options[oidx]);
            if (w) {
                move(v + 1, 0);
                if (fidx == selection) attron(A_STANDOUT);
                printw("%.*ls", cols, w);
                if (fidx == selection) attroff(A_STANDOUT);
                clrtoeol();
                free(w);
            }
        }
        refresh();
    }

cleanup:
    free(filtered.indices);
    endwin();
    delscreen(scr);
    fclose(tty);
    return ret;
}

#endif /* MMENU_IMPLEMENTATION */
