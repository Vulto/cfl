#ifndef CFL_BOOKMARK_H
#define CFL_BOOKMARK_H
char* bookmarks_path = NULL;

static int bookmark_find(char key, char *out_path, size_t out_size) {
    FILE *f = fopen(bookmarks_path, "r");
    if (!f) return 0;

    char line[PATH_MAX + 8];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != key || line[1] != ':') continue;
        char *path_start = line + 2;
        path_start[strcspn(path_start, "\n")] = '\0';
        snprintf(out_path, out_size, "%s", path_start);
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

static void bookmark_set(char key, const char *target_path) {
    FILE *in = fopen(bookmarks_path, "r");
    FILE *out = fopen(temp_clipboard_path, "w");
    if (!out) {
        displayAlert("Failed to update bookmarks");
        if (in) fclose(in);
        return;
    }

    int replaced = 0;
    char line[PATH_MAX + 8];
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (line[0] == key && line[1] == ':') {
                fprintf(out, "%c:%s\n", key, target_path);
                replaced = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%c:%s\n", key, target_path);
    }

    fclose(out);
    rename(temp_clipboard_path, bookmarks_path);
    displayAlert("Bookmark saved");
}

static void bookmark_remove(char key) {
    FILE *in = fopen(bookmarks_path, "r");
    FILE *out = fopen(temp_clipboard_path, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        displayAlert("No bookmarks to remove");
        return;
    }

    int removed = 0;
    char line[PATH_MAX + 8];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == key && line[1] == ':') {
            removed = 1;
            continue;
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);
    rename(temp_clipboard_path, bookmarks_path);

    if (removed) {
        displayAlert("Bookmark removed");
    } else {
        displayAlert("Bookmark key not found");
    }
}

/* Old tiny prompt versions removed — now provided by the rich UI below */

/* ============================================================
 * Nice Bookmark List UI (restored rich version)
 * ============================================================ */

enum {
    BM_BROWSE = 0,
    BM_ADD    = 1,
    BM_REMOVE = 2
};

typedef struct {
    char key;
    char path[PATH_MAX];
} BookmarkEntry;

static int load_all_bookmarks(BookmarkEntry **out_list, int *out_count) {
    *out_list = NULL;
    *out_count = 0;

    FILE *f = fopen(bookmarks_path, "r");
    if (!f) return 0;

    BookmarkEntry *list = NULL;
    int cap = 0;
    int cnt = 0;
    char line[PATH_MAX + 16];

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[1] != ':') continue;

        char key = line[0];
        char *p = line + 2;
        p[strcspn(p, "\n\r")] = '\0';
        if (*p == '\0') continue;

        if (cnt >= cap) {
            cap = cap ? cap * 2 : 16;
            BookmarkEntry *nl = realloc(list, cap * sizeof(BookmarkEntry));
            if (!nl) {
                free(list);
                fclose(f);
                return 0;
            }
            list = nl;
        }

        list[cnt].key = key;
        snprintf(list[cnt].path, sizeof(list[cnt].path), "%s", p);
        cnt++;
    }

    fclose(f);
    *out_list = list;
    *out_count = cnt;
    return cnt;
}

static void format_path_for_display(char *dst, size_t dstsz, const char *path, int max_cols) {
    if (max_cols <= 0 || !path) { dst[0] = '\0'; return; }
    size_t plen = strlen(path);
    if (plen <= (size_t)max_cols) {
        snprintf(dst, dstsz, "%s", path);
        return;
    }
    if (max_cols <= 3) {
        snprintf(dst, dstsz, "%.*s", max_cols, path + plen - max_cols);
        return;
    }
    int tail = max_cols - 3;
    const char *start = path + plen - tail;
    const char *sep = strrchr(start, '/');
    if (sep && sep > start) start = sep;
    snprintf(dst, dstsz, "...%s", start);
}

static void draw_bookmark_window(WINDOW *win, BookmarkEntry *list, int n,
                                 int selected, int view_start, int mode,
                                 int win_h, int win_w)
{
    werase(win);
    box(win, 0, 0);

    const char *title = " Bookmarks ";
    if (mode == BM_ADD)      title = " Bookmarks — Add current dir ";
    else if (mode == BM_REMOVE) title = " Bookmarks — Remove ";

    int title_len = strlen(title);
    mvwprintw(win, 0, (win_w - title_len) / 2, "%s", title);

    int y = 1;
    if (mode == BM_ADD) {
        char cur[win_w - 4];
        format_path_for_display(cur, sizeof(cur), dir, win_w - 12);
        mvwprintw(win, y++, 2, "Current: %s", cur);
    }

    mvwprintw(win, y++, 2, "Key  Path");
    mvwaddch(win, y, 1, ACS_HLINE);
    for (int i = 2; i < win_w-2; i++) mvwaddch(win, y, i, ACS_HLINE);
    y++;

    int list_start_y = y;
    int visible_rows = win_h - list_start_y - 3;
    if (visible_rows < 1) visible_rows = 1;

    if (n == 0) {
        mvwprintw(win, list_start_y + 1, 2, "No bookmarks yet.");
        mvwprintw(win, list_start_y + 2, 2, "Press '+' to bookmark the current directory.");
    } else {
        int max_path_cols = win_w - 8;
        for (int i = 0; i < visible_rows; i++) {
            int idx = view_start + i;
            if (idx >= n) break;

            int row_y = list_start_y + i;
            int is_sel = (idx == selected);

            if (is_sel) wattron(win, A_REVERSE | A_BOLD);

            mvwprintw(win, row_y, 2, "%c", list[idx].key);

            char disp[512];
            format_path_for_display(disp, sizeof(disp), list[idx].path, max_path_cols);
            mvwprintw(win, row_y, 6, "%s", disp);

            if (is_sel) wattroff(win, A_REVERSE | A_BOLD);
        }
    }

    int footer_y = win_h - 2;
    mvwaddch(win, footer_y-1, 1, ACS_HLINE);

    if (mode == BM_BROWSE)
        mvwprintw(win, footer_y, 2, "Enter=Go  + =Add  - =Remove  q=Close  ↑↓=Navigate");
    else if (mode == BM_ADD)
        mvwprintw(win, footer_y, 2, "Enter=Confirm add  q=Cancel");
    else
        mvwprintw(win, footer_y, 2, "Enter=Remove highlighted  q=Cancel");

    wrefresh(win);
}

static void show_bookmark_ui(int initial_mode) {
    int mode = initial_mode;

    BookmarkEntry *bookmarks = NULL;
    int n = 0;
    load_all_bookmarks(&bookmarks, &n);

    int selected = 0;
    int view_start = 0;

    int win_h = 18;
    int win_w = 70;
    int starty = (LINES - win_h) / 2;
    int startx = (COLS  - win_w) / 2;

    WINDOW *win = newwin(win_h, win_w, starty, startx);
    if (!win) {
        free(bookmarks);
        return;
    }

    keypad(win, TRUE);

    int running = 1;
    while (running) {
        // reload on every frame in case bookmarks changed
        free(bookmarks);
        load_all_bookmarks(&bookmarks, &n);

        if (selected >= n) selected = n ? n-1 : 0;
        if (view_start > selected) view_start = selected;
        if (view_start + (win_h - 8) <= selected) view_start = selected - (win_h - 9);
        if (view_start < 0) view_start = 0;

        draw_bookmark_window(win, bookmarks, n, selected, view_start, mode, win_h, win_w);

        int ch = wgetch(win);

        if (ch == 'q' || ch == KEY_ESCAPE) {
            running = 0;
        }
        else if (mode == BM_BROWSE) {
            if (ch == KEY_UP || ch == 'k') {
                if (selected > 0) selected--;
            }
            else if (ch == KEY_DOWN || ch == 'j') {
                if (selected < n-1) selected++;
            }
            else if (ch == '\n' || ch == KEY_ENTER) {
                if (n > 0) {
                    char target[PATH_MAX];
                    snprintf(target, sizeof(target), "%s", bookmarks[selected].path);

                    struct stat st;
                    if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
                        snprintf(dir, sizeof(dir), "%s", target);
                        selection = 0;
                        start = 0;
                        running = 0;
                    } else {
                        displayAlert("Bookmark target is not a directory");
                    }
                }
            }
            else if (ch == '+') {
                mode = BM_ADD;
            }
            else if (ch == '-') {
                mode = BM_REMOVE;
            }
        }
        else if (mode == BM_ADD) {
            if (ch == '\n' || ch == KEY_ENTER) {
                // Simple key prompt for adding (kept minimal)
                delwin(win);
                WINDOW *kwin = newwin(3, 48, (LINES - 3) / 2, (COLS - 48) / 2);
                box(kwin, 0, 0);
                mvwprintw(kwin, 1, 1, "Save current dir to key: ");
                wrefresh(kwin);
                int key = wgetch(kwin);
                delwin(kwin);

                if (key != ERR) {
                    bookmark_set((char)key, dir);
                }
                win = newwin(win_h, win_w, starty, startx);
                keypad(win, TRUE);
                mode = BM_BROWSE;
            }
            else if (ch == 'q' || ch == KEY_ESCAPE) {
                mode = BM_BROWSE;
            }
        }
        else if (mode == BM_REMOVE) {
            if (ch == '\n' || ch == KEY_ENTER) {
                if (n > 0) {
                    bookmark_remove(bookmarks[selected].key);
                    // selection will be adjusted on next reload
                }
            }
            else if (ch == 'q' || ch == KEY_ESCAPE) {
                mode = BM_BROWSE;
            }
            else if (ch == KEY_UP || ch == 'k') {
                if (selected > 0) selected--;
            }
            else if (ch == KEY_DOWN || ch == 'j') {
                if (selected < n-1) selected++;
            }
        }
    }

    delwin(win);
    free(bookmarks);
}

// Public entry points used by main.c — now launch the rich UI
static void go_bookmark(void) {
    show_bookmark_ui(BM_BROWSE);
}

static void add_bookmark(void) {
    show_bookmark_ui(BM_ADD);
}

static void remove_bookmark_prompt(void) {
    show_bookmark_ui(BM_REMOVE);
}

#endif
