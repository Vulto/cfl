#ifndef CFL_SEARCH_H
#define CFL_SEARCH_H

/* Panel-based fuzzy search: left list + right preview (main cfl layout).
 * Filter algorithm matches mmenu (case-insensitive substring + refine-on-append). */

#define SEARCH_MAX_INPUT 128
#define SEARCH_FILT_CAP  128

typedef struct {
    int *indices;
    int cap;
    int count;
} search_filt;

static void search_filt_init(search_filt *f) {
    f->cap = SEARCH_FILT_CAP;
    f->indices = malloc((size_t)f->cap * sizeof(int));
    if (!f->indices) {
        displayAlert("Couldn't allocate memory!");
        exit(1);
    }
    f->count = 0;
}

static void search_filt_free(search_filt *f) {
    free(f->indices);
    f->indices = NULL;
    f->count = f->cap = 0;
}

static void search_filt_push(search_filt *f, int idx) {
    if (f->count == f->cap) {
        f->cap *= 2;
        int *grown = realloc(f->indices, (size_t)f->cap * sizeof(int));
        if (!grown) {
            displayAlert("Couldn't allocate memory!");
            exit(1);
        }
        f->indices = grown;
    }
    f->indices[f->count++] = idx;
}

static void search_filt_clear(search_filt *f) { f->count = 0; }

static const char *search_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return p;
        if (!p[i] && i < nlen) break;
    }
    return NULL;
}

static void search_filt_fill_all(search_filt *f, int n) {
    search_filt_clear(f);
    for (int i = 0; i < n; i++)
        search_filt_push(f, i);
}

/* Rank matches: shorter path first, then fewer '/', then earlier match, then index. */
typedef struct {
    int idx;
    int path_len;
    int depth;
    int match_pos;
} search_rank;

static int search_rank_cmp(const void *a, const void *b) {
    const search_rank *ra = a;
    const search_rank *rb = b;
    if (ra->path_len != rb->path_len)
        return ra->path_len - rb->path_len;
    if (ra->depth != rb->depth)
        return ra->depth - rb->depth;
    if (ra->match_pos != rb->match_pos)
        return ra->match_pos - rb->match_pos;
    return ra->idx - rb->idx;
}

static int search_path_depth(const char *p) {
    int d = 0;
    if (!p) return 0;
    for (; *p; p++)
        if (*p == '/') d++;
    return d;
}

static void search_filt_sort_short(search_filt *f, char **files, const char *q) {
    if (!f || f->count <= 1 || !files) return;

    search_rank *ranks = malloc((size_t)f->count * sizeof(search_rank));
    if (!ranks) return;

    for (int i = 0; i < f->count; i++) {
        int oidx = f->indices[i];
        const char *p = files[oidx];
        ranks[i].idx = oidx;
        ranks[i].path_len = p ? (int)strlen(p) : 0;
        ranks[i].depth = search_path_depth(p);
        ranks[i].match_pos = 0;
        if (q && q[0] && p) {
            const char *m = search_strcasestr(p, q);
            ranks[i].match_pos = m ? (int)(m - p) : 0x7fffffff;
        }
    }

    qsort(ranks, (size_t)f->count, sizeof(search_rank), search_rank_cmp);

    for (int i = 0; i < f->count; i++)
        f->indices[i] = ranks[i].idx;

    free(ranks);
}

static void search_filt_apply(search_filt *f, char **files, int n, const char *q) {
    search_filt_clear(f);
    if (!q || q[0] == '\0') {
        search_filt_fill_all(f, n);
        search_filt_sort_short(f, files, NULL);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (files[i] && search_strcasestr(files[i], q))
            search_filt_push(f, i);
    }
    search_filt_sort_short(f, files, q);
}

/* Refine when the user appended a character. Returns 1 on success. */
static int search_filt_refine(search_filt *f, char **files, const char *q) {
    search_filt newf;
    newf.cap = f->count < SEARCH_FILT_CAP ? SEARCH_FILT_CAP : f->count;
    newf.indices = malloc((size_t)newf.cap * sizeof(int));
    if (!newf.indices) return 0;
    newf.count = 0;
    for (int k = 0; k < f->count; k++) {
        int oidx = f->indices[k];
        if (files[oidx] && search_strcasestr(files[oidx], q))
            search_filt_push(&newf, oidx);
    }
    free(f->indices);
    *f = newf;
    search_filt_sort_short(f, files, q);
    return 1;
}

static char *search_wc_to_mb(const wchar_t *w) {
    if (!w || !*w) return strdup("");
    size_t n = wcstombs(NULL, w, 0);
    if (n == (size_t)-1) return NULL;
    char *m = malloc(n + 1);
    if (m) wcstombs(m, w, n + 1);
    return m;
}

static void search_free_listing(char ***listing, int *n) {
    if (!listing || !*listing) {
        if (n) *n = 0;
        return;
    }
    int count = n ? *n : 0;
    for (int i = 0; i < count; i++)
        free((*listing)[i]);
    free(*listing);
    *listing = NULL;
    if (n) *n = 0;
}

static void search_draw_status(const char *query_mb, int sel, int matches, int total) {
    if (!status_win) return;
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    wprintw(status_win, "Search > ");
    wattroff(status_win, A_BOLD);
    wprintw(status_win, "%s", query_mb ? query_mb : "");

    int rows, cols;
    getmaxyx(status_win, rows, cols);
    (void)rows;

    char right[64];
    if (matches > 0)
        snprintf(right, sizeof right, "(%d/%d)  Enter=Jump  Esc=Cancel", sel + 1, matches);
    else
        snprintf(right, sizeof right, "(0/%d)  Esc=Cancel", total);

    int rlen = (int)strlen(right);
    if (rlen + 2 < cols)
        mvwprintw(status_win, 0, cols - rlen - 1, "%s", right);

    /* Place cursor after query for typing feedback */
    int qlen = query_mb ? (int)strlen(query_mb) : 0;
    int cursor_col = 1 + (int)strlen("Search > ") + qlen;
    if (cursor_col >= cols - rlen - 2)
        cursor_col = cols - rlen - 3;
    if (cursor_col < 1) cursor_col = 1;
    wmove(status_win, 0, cursor_col);
    curs_set(1);
    wrefresh(status_win);
}

static void search_draw_list(char **files, search_filt *filt, int sel, int top) {
    if (!current_win) return;

    wclear(current_win);

    int rows, cols;
    getmaxyx(current_win, rows, cols);
    int visible = rows > 0 ? rows : 1;
    int trunc = cols - 5;
    if (trunc < 1) trunc = 1;

    if (filt->count == 0) {
        wmove(current_win, 0, 2);
        wprintw(current_win, "No matches.");
    } else {
        for (int v = 0; v < visible && top + v < filt->count; v++) {
            int fidx = top + v;
            int oidx = filt->indices[fidx];
            const char *name = files[oidx] ? files[oidx] : "";

            char full[PATH_MAX];
            int path_ok = (build_path(full, sizeof full, dir, name) == 0);
            int is_dir = path_ok && (isDirectory(full));

            if (fidx == sel) {
                if (has_colors()) wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            } else {
                if (has_colors()) wattroff(current_win, A_REVERSE);
                wattroff(current_win, A_BOLD);
            }

            if (is_dir)
                wattron(current_win, A_BOLD);
            else if (fidx != sel)
                wattroff(current_win, A_BOLD);

            wmove(current_win, v, 2);

            char entry_display[PATH_MAX * 2 + 32];
            if (path_ok && isSymlink(full)) {
                char target[PATH_MAX];
                if (readSymlinkTarget(full, target, sizeof target) == 0)
                    snprintf(entry_display, sizeof entry_display, "%s → %s", name, target);
                else
                    snprintf(entry_display, sizeof entry_display, "%s → (unreadable)", name);
            } else {
                snprintf(entry_display, sizeof entry_display, "%s", name);
            }
            if ((int)strlen(entry_display) > trunc)
                entry_display[trunc] = '\0';

            wprintw(current_win, "%s", entry_display);

            /* Pad selected row so reverse fills the line */
            if (fidx == sel) {
                int used = (int)strlen(entry_display);
                for (int c = 2 + used; c < cols - 1; c++)
                    mvwaddch(current_win, v, c, ' ');
                if (has_colors()) wattroff(current_win, A_REVERSE);
                wattroff(current_win, A_BOLD);
            }
        }
    }

    wattrset(current_win, A_NORMAL);
    drawScrollbar(current_win, filt->count > 0 ? filt->count : 1, top, false);
    wrefresh(current_win);
}

static void search_draw_preview(char **files, search_filt *filt, int sel,
                                char ***preview_listing, int *preview_n)
{
    if (!preview_win) return;

    search_free_listing(preview_listing, preview_n);
    wclear(preview_win);
    preview_scroll = 0;
    len_preview = -1;

    if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count) {
        wmove(preview_win, 1, 2);
        wprintw(preview_win, "No preview");
        wrefresh(preview_win);
        ueberzugpp_remove();
        return;
    }

    int oidx = filt->indices[sel];
    char path[PATH_MAX];
    if (build_path(path, sizeof path, dir, files[oidx]) != 0) {
        wmove(preview_win, 1, 2);
        wprintw(preview_win, "Path too long");
        wrefresh(preview_win);
        return;
    }

    /* Directory → list contents; file → text/hex/image preview */
    if (isDirectory(path)) {
        ueberzugpp_remove();
        int filled = getFiles(path, preview_listing);
        if (filled < 0) {
            *preview_n = 0;
            wmove(preview_win, 1, 2);
            wprintw(preview_win, "Cannot open directory");
            wrefresh(preview_win);
            return;
        }
        *preview_n = filled;
        len_preview = filled;
        if (filled > 0) {
            char saved_sort[PATH_MAX];
            snprintf(saved_sort, sizeof saved_sort, "%s", sort_dir);
            if (snprintf(sort_dir, sizeof sort_dir, "%s", path) < (int)sizeof sort_dir)
                qsort(*preview_listing, (size_t)filled, sizeof(char *), compare);
            snprintf(sort_dir, sizeof sort_dir, "%s", saved_sort);
            PreviewNextDir(path, *preview_listing);
        } else {
            wmove(preview_win, 1, 2);
            wprintw(preview_win, "Empty directory");
            wrefresh(preview_win);
        }
    } else {
        getPreview(path, maxy, maxx / 2 + 2);
    }
}

/* Fuzzy-find in the current directory listing using main panels.
 * On confirm, updates global selection/start so the main list jumps to the match. */
static void show_file_search(char **files, int n) {
    if (!files || n <= 0) {
        displayAlert("No files to search");
        return;
    }
    if (!current_win || !preview_win || !status_win) {
        displayAlert("UI not ready");
        return;
    }

    wchar_t input[SEARCH_MAX_INPUT + 1] = {0};
    int input_len = 0;
    int prev_input_len = 0;

    search_filt filt;
    search_filt_init(&filt);
    search_filt_fill_all(&filt, n);

    int sel = 0;
    int top = 0;
    int done = 0;
    int confirmed = 0;

    char **preview_listing = NULL;
    int preview_n = 0;
    int last_preview_oidx = -1;

    /* Input + caret live on status_win so the hardware cursor is not a stray
     * square on the left list (which happens if we wget_wch(current_win)). */
    keypad(status_win, TRUE);
    meta(status_win, TRUE);
    leaveok(current_win, TRUE);
    leaveok(preview_win, TRUE);

    while (!done) {
        int rows, cols;
        getmaxyx(current_win, rows, cols);
        (void)cols;
        int visible = rows > 0 ? rows : 1;

        if (filt.count > 0) {
            if (sel < 0) sel = 0;
            if (sel >= filt.count) sel = filt.count - 1;
            if (sel < top) top = sel;
            else if (sel >= top + visible) top = sel - visible + 1;
            if (top < 0) top = 0;
            int max_top = filt.count - visible;
            if (max_top < 0) max_top = 0;
            if (top > max_top) top = max_top;
        } else {
            sel = 0;
            top = 0;
        }

        /* Panels first, status last so the caret stays on the query line */
        search_draw_list(files, &filt, sel, top);

        int cur_oidx = (filt.count > 0) ? filt.indices[sel] : -1;
        if (cur_oidx != last_preview_oidx) {
            search_draw_preview(files, &filt, sel, &preview_listing, &preview_n);
            last_preview_oidx = cur_oidx;
        }

        char *qmb = search_wc_to_mb(input);
        search_draw_status(qmb ? qmb : "", sel, filt.count, n);
        free(qmb);

        wint_t ch;
        int kc = wget_wch(status_win, &ch);
        if (kc == ERR) continue;

        int need_filter = 0;
        int need_preview = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                handleResize();
                last_preview_oidx = -1; /* force preview redraw after layout change */
                continue;
            }
            if (ch == KEY_UP && sel > 0) {
                sel--;
                need_preview = 1;
            } else if (ch == KEY_DOWN && sel < filt.count - 1) {
                sel++;
                need_preview = 1;
            } else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127 || ch == 8) {
                if (input_len > 0) {
                    input[--input_len] = L'\0';
                    need_filter = 1;
                }
            } else if (ch == KEY_ENTER) {
                if (filt.count > 0) {
                    confirmed = 1;
                    done = 1;
                }
            } else if (ch == KEY_PPAGE) {
                sel -= visible;
                if (sel < 0) sel = 0;
                need_preview = 1;
            } else if (ch == KEY_NPAGE) {
                sel += visible;
                if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                need_preview = 1;
            }
        } else {
            if (ch == 27 || ch == 3 || ch == 4) {
                done = 1;
            } else if (ch == 'q' && input_len == 0) {
                done = 1;
            } else if (ch == '\n' || ch == '\r') {
                if (filt.count > 0) {
                    confirmed = 1;
                    done = 1;
                }
            } else if (iswprint(ch) && input_len < SEARCH_MAX_INPUT) {
                input[input_len++] = (wchar_t)ch;
                input[input_len] = L'\0';
                need_filter = 1;
            }
        }

        if (need_filter) {
            char *q = search_wc_to_mb(input);
            int do_full = 1;
            if (q && input_len > prev_input_len && filt.count > 0) {
                if (search_filt_refine(&filt, files, q))
                    do_full = 0;
            }
            if (do_full)
                search_filt_apply(&filt, files, n, q);
            free(q);
            prev_input_len = input_len;
            sel = 0;
            top = 0;
            last_preview_oidx = -1;
        } else if (need_preview) {
            last_preview_oidx = -1;
        }
    }

    curs_set(0);
    leaveok(current_win, FALSE);
    leaveok(preview_win, FALSE);
    search_free_listing(&preview_listing, &preview_n);
    ueberzugpp_remove();
    clearFlagImg = 1;

    if (confirmed && filt.count > 0) {
        int idx = filt.indices[sel];
        if (idx >= 0 && idx < n) {
            selection = idx;
            if (selection < start)
                start = selection;
            else if (maxy > 1 && selection >= start + maxy - 1)
                start = selection - (maxy - 2);
            if (start < 0)
                start = 0;
            if (start > selection)
                start = selection;
        }
    }

    search_filt_free(&filt);
}

#endif /* CFL_SEARCH_H */
