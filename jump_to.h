#ifndef CFL_JUMP_TO_H
#define CFL_JUMP_TO_H

/* Directory picker used by:
 *  - "Jump to" (J): open selected directory
 *  - "Move to" (t): move clipboard selection into selected directory
 *  - "Copy to" (C): copy clipboard selection into selected directory
 * Scan roots /home, /etc, /usr; JUMP_OMIT_DIRS skipped; errors omitted. */

#define JUMP_MAX_INPUT   128
#define JUMP_MAX_DEPTH   6
#define JUMP_INIT_CAP    4096

typedef enum {
    DIR_PICK_JUMP = 0,
    DIR_PICK_MOVE = 1,
    DIR_PICK_COPY = 2,
} dir_pick_mode;

typedef struct {
    char **paths;
    int count;
    int cap;
} jump_list;

static void jump_list_init(jump_list *jl) {
    jl->count = 0;
    jl->cap = JUMP_INIT_CAP;
    jl->paths = malloc((size_t)jl->cap * sizeof(char *));
    if (!jl->paths) {
        jl->cap = 0;
        displayAlert("Couldn't allocate memory!");
    }
}

static void jump_list_free(jump_list *jl) {
    if (!jl || !jl->paths) return;
    for (int i = 0; i < jl->count; i++)
        free(jl->paths[i]);
    free(jl->paths);
    jl->paths = NULL;
    jl->count = jl->cap = 0;
}

static int jump_list_push(jump_list *jl, const char *path) {
    if (!jl || !jl->paths || !path) return -1;
    if (jl->count >= jl->cap) {
        int ncap = jl->cap * 2;
        if (ncap < jl->cap) return -1;
        char **grown = realloc(jl->paths, (size_t)ncap * sizeof(char *));
        if (!grown) return -1;
        jl->paths = grown;
        jl->cap = ncap;
    }
    jl->paths[jl->count] = strdup(path);
    if (!jl->paths[jl->count]) return -1;
    jl->count++;
    return 0;
}

/* True if basename is listed in JUMP_OMIT_DIRS (config.h). */
static int jump_is_omitted(const char *name) {
    if (!name || !name[0])
        return 0;
    for (int i = 0; JUMP_OMIT_DIRS[i] != NULL; i++) {
        if (strcmp(name, JUMP_OMIT_DIRS[i]) == 0)
            return 1;
    }
    return 0;
}

static const char *jump_basename(const char *path) {
    if (!path || !path[0])
        return path;
    const char *slash = strrchr(path, '/');
    if (!slash)
        return path;
    /* path is "/" */
    if (slash[1] == '\0')
        return path;
    return slash + 1;
}

/* Recursively collect directories. Silently skip anything we cannot open.
 * Directory basenames in JUMP_OMIT_DIRS are not recorded or descended into. */
static void jump_collect(jump_list *jl, const char *path, int depth) {
    if (!jl || !jl->paths || !path || depth > JUMP_MAX_DEPTH)
        return;

    if (jump_is_omitted(jump_basename(path)))
        return;

    /* Record this directory (including roots) */
    if (jump_list_push(jl, path) != 0)
        return;

    if (depth >= JUMP_MAX_DEPTH)
        return;

    DIR *d = opendir(path);
    if (!d)
        return; /* omit errors */

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (jump_is_omitted(ent->d_name))
            continue;

        char child[PATH_MAX];
        if (build_path(child, sizeof child, path, ent->d_name) != 0)
            continue;

        struct stat st;
        /* lstat: do not follow symlinks (avoids cycles; omit link targets) */
        if (lstat(child, &st) != 0)
            continue;
        if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
            continue;

        jump_collect(jl, child, depth + 1);
    }
    closedir(d);
}

/* Shorter paths first, then alphabetical (used for initial jump list order). */
static int jump_path_cmp(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    if (!sa) sa = "";
    if (!sb) sb = "";
    int la = (int)strlen(sa);
    int lb = (int)strlen(sb);
    if (la != lb)
        return la - lb;
    return strcasecmp(sa, sb);
}

static void jump_draw_status(dir_pick_mode mode, const char *query_mb,
                             int sel, int matches, int total) {
    if (!status_win) return;
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    const char *prompt =
        (mode == DIR_PICK_MOVE) ? "Move to > " :
        (mode == DIR_PICK_COPY) ? "Copy to > " : "Jump to > ";
    wprintw(status_win, "%s", prompt);
    wattroff(status_win, A_BOLD);
    wprintw(status_win, "%s", query_mb ? query_mb : "");

    int rows, cols;
    getmaxyx(status_win, rows, cols);
    (void)rows;

    char right[96];
    if (mode == DIR_PICK_MOVE || mode == DIR_PICK_COPY) {
        const char *act = (mode == DIR_PICK_COPY) ? "Copy" : "Move";
        if (matches > 0)
            snprintf(right, sizeof right, "(%d/%d) [%d sel] Enter=%s  Esc",
                     sel + 1, matches, selectedFiles, act);
        else
            snprintf(right, sizeof right, "(0/%d) [%d sel] Esc", total, selectedFiles);
    } else {
        if (matches > 0)
            snprintf(right, sizeof right, "(%d/%d)  Enter=Go  Esc=Cancel", sel + 1, matches);
        else
            snprintf(right, sizeof right, "(0/%d)  Esc=Cancel", total);
    }

    int rlen = (int)strlen(right);
    if (rlen + 2 < cols)
        mvwprintw(status_win, 0, cols - rlen - 1, "%s", right);

    int qlen = query_mb ? (int)strlen(query_mb) : 0;
    int cursor_col = 1 + (int)strlen(prompt) + qlen;
    if (cursor_col >= cols - rlen - 2)
        cursor_col = cols - rlen - 3;
    if (cursor_col < 1) cursor_col = 1;
    wmove(status_win, 0, cursor_col);
    curs_set(1);
    wrefresh(status_win);
}

static void jump_draw_list(char **paths, search_filt *filt, int sel, int top) {
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
            const char *name = paths[oidx] ? paths[oidx] : "";

            wattrset(current_win, A_NORMAL);
            if (fidx == sel) {
                if (has_colors()) wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            }

            char entry_display[PATH_MAX + 8];
            snprintf(entry_display, sizeof entry_display, "%s", name);
            if ((int)strlen(entry_display) > trunc) {
                /* Prefer showing the end of long paths */
                int len = (int)strlen(entry_display);
                int keep = trunc - 3;
                if (keep < 1) keep = trunc;
                if (keep < len) {
                    snprintf(entry_display, sizeof entry_display, "...%s",
                             name + len - keep);
                    if ((int)strlen(entry_display) > trunc)
                        entry_display[trunc] = '\0';
                }
            }

            wmove(current_win, v, 2);
            wprintw(current_win, "%s", entry_display);

            if (fidx == sel) {
                int used = (int)strlen(entry_display);
                for (int c = 2 + used; c < cols - 1; c++)
                    mvwaddch(current_win, v, c, ' ');
            }
            wattrset(current_win, A_NORMAL);
        }
    }

    drawScrollbar(current_win, filt->count > 0 ? filt->count : 1, top, false);
    wrefresh(current_win);
}

static void jump_draw_preview(dir_pick_mode mode, char **paths, search_filt *filt, int sel) {
    if (!preview_win) return;
    wclear(preview_win);
    ueberzugpp_remove();
    preview_scroll = 0;
    len_preview = -1;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);

    int line = 0;
    wattron(preview_win, A_BOLD);
    mvwprintw(preview_win, line++, 2,
              mode == DIR_PICK_MOVE ? " Move to " :
              mode == DIR_PICK_COPY ? " Copy to " : " Jump to ");
    wattroff(preview_win, A_BOLD);
    line++;

    if (mode == DIR_PICK_MOVE || mode == DIR_PICK_COPY) {
        mvwprintw(preview_win, line++, 2, "%s %d selected item(s)",
                  mode == DIR_PICK_COPY ? "Copying" : "Moving", selectedFiles);
        line++;
    }

    if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count) {
        mvwprintw(preview_win, line++, 2, "No directory selected");
        wrefresh(preview_win);
        return;
    }

    int oidx = filt->indices[sel];
    const char *path = paths[oidx] ? paths[oidx] : "";

    mvwprintw(preview_win, line++, 2,
              (mode == DIR_PICK_MOVE || mode == DIR_PICK_COPY) ? "Destination:" : "Path:");
    /* wrap/truncate long paths */
    int safe = cols - 4;
    if (safe < 8) safe = 8;
    if ((int)strlen(path) <= safe) {
        mvwprintw(preview_win, line++, 2, "%s", path);
    } else {
        mvwprintw(preview_win, line++, 2, "...%s", path + strlen(path) - (safe - 3));
    }
    line++;

    struct stat st;
    if (stat(path, &st) == 0) {
        mvwprintw(preview_win, line++, 2, "Type: Directory");
        char mtime_str[64];
        struct tm *tm = localtime(&st.st_mtime);
        if (tm)
            strftime(mtime_str, sizeof mtime_str, "%Y-%m-%d %H:%M:%S", tm);
        else
            snprintf(mtime_str, sizeof mtime_str, "?");
        mvwprintw(preview_win, line++, 2, "Modified: %s", mtime_str);
    } else {
        mvwprintw(preview_win, line++, 2, "Type: (unreadable)");
    }

    /* Quick child listing (omit errors) */
    line++;
    wattron(preview_win, A_BOLD);
    mvwprintw(preview_win, line++, 2, "Contents:");
    wattroff(preview_win, A_BOLD);

    DIR *d = opendir(path);
    if (!d) {
        mvwprintw(preview_win, line++, 2, "(cannot list)");
    } else {
        int shown = 0;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && line < rows - 2) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char name[NAME_MAX + 8];
            snprintf(name, sizeof name, "%s", ent->d_name);
            if ((int)strlen(name) > safe)
                name[safe] = '\0';
            mvwprintw(preview_win, line++, 2, "%s", name);
            shown++;
        }
        closedir(d);
        if (shown == 0 && line < rows - 1)
            mvwprintw(preview_win, line++, 2, "(empty)");
    }

    if (rows > 2) {
        wattron(preview_win, A_DIM);
        const char *hint =
            mode == DIR_PICK_MOVE ? "Enter=move  Esc=cancel" :
            mode == DIR_PICK_COPY ? "Enter=copy  Esc=cancel" :
                                    "Enter=go  Esc=cancel";
        mvwprintw(preview_win, rows - 1, 2, "%s", hint);
        wattroff(preview_win, A_DIM);
    }
    wrefresh(preview_win);
}

/* Shared directory picker. JUMP: chdir; MOVE/COPY: transfer selection. */
static void show_dir_pick(dir_pick_mode mode) {
    if (!current_win || !preview_win || !status_win) {
        displayAlert("UI not ready");
        return;
    }

    if (mode == DIR_PICK_MOVE || mode == DIR_PICK_COPY) {
        if (access(clipboard_path, F_OK) != 0 || selectedFiles <= 0) {
            displayAlert("Select some files first!");
            return;
        }
    }

    ueberzugpp_remove();

    /* Progress while scanning */
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    wprintw(status_win, "%s scanning /home /etc /usr ...",
            mode == DIR_PICK_MOVE ? "Move to >" :
            mode == DIR_PICK_COPY ? "Copy to >" : "Jump to >");
    wattroff(status_win, A_BOLD);
    wrefresh(status_win);

    jump_list jl;
    jump_list_init(&jl);
    if (!jl.paths)
        return;

    static const char *roots[] = { "/home", "/etc", "/usr", NULL };
    for (int r = 0; roots[r]; r++) {
        struct stat st;
        if (stat(roots[r], &st) != 0 || !S_ISDIR(st.st_mode))
            continue; /* omit missing roots */
        jump_collect(&jl, roots[r], 0);
    }

    if (jl.count <= 0) {
        jump_list_free(&jl);
        displayAlert("No directories found");
        return;
    }

    qsort(jl.paths, (size_t)jl.count, sizeof(char *), jump_path_cmp);

    wchar_t input[JUMP_MAX_INPUT + 1] = {0};
    int input_len = 0;
    int prev_input_len = 0;

    search_filt filt;
    search_filt_init(&filt);
    search_filt_fill_all(&filt, jl.count);
    search_filt_sort_short(&filt, jl.paths, NULL);

    int sel = 0;
    int top = 0;
    int done = 0;
    int confirmed = 0;
    int last_preview_oidx = -2;

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

        jump_draw_list(jl.paths, &filt, sel, top);

        int cur_oidx = (filt.count > 0) ? filt.indices[sel] : -1;
        if (cur_oidx != last_preview_oidx) {
            jump_draw_preview(mode, jl.paths, &filt, sel);
            last_preview_oidx = cur_oidx;
        }

        char *qmb = search_wc_to_mb(input);
        jump_draw_status(mode, qmb ? qmb : "", sel, filt.count, jl.count);
        free(qmb);

        wint_t ch;
        int kc = wget_wch(status_win, &ch);
        if (kc == ERR) continue;

        int need_filter = 0;
        int need_preview = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                handleResize();
                last_preview_oidx = -2;
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
            } else if (iswprint(ch) && input_len < JUMP_MAX_INPUT) {
                input[input_len++] = (wchar_t)ch;
                input[input_len] = L'\0';
                need_filter = 1;
            }
        }

        if (need_filter) {
            char *q = search_wc_to_mb(input);
            int do_full = 1;
            if (q && input_len > prev_input_len && filt.count > 0) {
                if (search_filt_refine(&filt, jl.paths, q))
                    do_full = 0;
            }
            if (do_full)
                search_filt_apply(&filt, jl.paths, jl.count, q);
            free(q);
            prev_input_len = input_len;
            sel = 0;
            top = 0;
            last_preview_oidx = -2;
        } else if (need_preview) {
            last_preview_oidx = -2;
        }
    }

    curs_set(0);
    leaveok(current_win, FALSE);
    leaveok(preview_win, FALSE);
    ueberzugpp_remove();
    clearFlagImg = 1;

    if (confirmed && filt.count > 0) {
        int oidx = filt.indices[sel];
        if (oidx >= 0 && oidx < jl.count && jl.paths[oidx]) {
            struct stat st;
            if (stat(jl.paths[oidx], &st) == 0 && S_ISDIR(st.st_mode)) {
                if (mode == DIR_PICK_JUMP) {
                    if (snprintf(dir, sizeof dir, "%s", jl.paths[oidx]) < (int)sizeof dir) {
                        selection = 0;
                        start = 0;
                    } else {
                        displayAlert("Path too long");
                    }
                } else if (mode == DIR_PICK_MOVE) {
                    /* Move selection into destination; stay in current dir */
                    moveFiles(jl.paths[oidx]);
                } else {
                    /* Copy selection into destination; stay in current dir */
                    copyFiles(jl.paths[oidx]);
                }
            } else {
                displayAlert(mode == DIR_PICK_MOVE ? "Cannot move there" :
                             mode == DIR_PICK_COPY ? "Cannot copy there" :
                                                     "Cannot open directory");
            }
        }
    }

    search_filt_free(&filt);
    jump_list_free(&jl);
}

static void show_jump_to(void) {
    show_dir_pick(DIR_PICK_JUMP);
}

static void show_move_to(void) {
    show_dir_pick(DIR_PICK_MOVE);
}

static void show_copy_to(void) {
    show_dir_pick(DIR_PICK_COPY);
}

#endif /* CFL_JUMP_TO_H */
