#ifndef CFL_DELETE_SEARCH_H
#define CFL_DELETE_SEARCH_H

/* Panel mode like search, for multi-select permanent delete.
 * Left: filterable list (Tab marks rows with dark red background).
 * Right: detailed deletion info for the focused entry + mark summary.
 * Relies on search.h filter helpers (included before this header). */

#define DELETE_SEARCH_MAX_INPUT 128

static const char *delete_human_size(off_t bytes, char *buf, size_t bufsz) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    double size = (double)bytes;
    int u = 0;
    while (size >= 1024.0 && u < 4) {
        size /= 1024.0;
        u++;
    }
    snprintf(buf, bufsz, "%.1f%s", size, units[u]);
    return buf;
}

static int delete_count_marked(const unsigned char *marked, int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        if (marked[i]) c++;
    return c;
}

static off_t delete_marked_total_size(char **files, int n, const unsigned char *marked) {
    off_t total = 0;
    for (int i = 0; i < n; i++) {
        if (!marked[i] || !files[i]) continue;
        char path[PATH_MAX];
        if (build_path(path, sizeof path, dir, files[i]) != 0) continue;
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISREG(st.st_mode))
            total += st.st_size;
    }
    return total;
}

static void delete_search_draw_status(const char *query_mb, int sel, int matches,
                                      int total, int nmarked) {
    if (!status_win) return;
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    wprintw(status_win, "Delete > ");
    wattroff(status_win, A_BOLD);
    wprintw(status_win, "%s", query_mb ? query_mb : "");

    int rows, cols;
    getmaxyx(status_win, rows, cols);
    (void)rows;

    char right[96];
    if (matches > 0)
        snprintf(right, sizeof right, "(%d/%d) marked=%d  Tab  Enter=Del  Esc",
                 sel + 1, matches, nmarked);
    else
        snprintf(right, sizeof right, "(0/%d) marked=%d  Esc", total, nmarked);

    int rlen = (int)strlen(right);
    if (rlen + 2 < cols)
        mvwprintw(status_win, 0, cols - rlen - 1, "%s", right);

    int qlen = query_mb ? (int)strlen(query_mb) : 0;
    int cursor_col = 1 + (int)strlen("Delete > ") + qlen;
    if (cursor_col >= cols - rlen - 2)
        cursor_col = cols - rlen - 3;
    if (cursor_col < 1) cursor_col = 1;
    wmove(status_win, 0, cursor_col);
    curs_set(1);
    wrefresh(status_win);
}

static void delete_search_draw_list(char **files, search_filt *filt, int sel, int top,
                                    const unsigned char *marked) {
    if (!current_win) return;

    wclear(current_win);

    int rows, cols;
    getmaxyx(current_win, rows, cols);
    int visible = rows > 0 ? rows : 1;
    int trunc = cols - 6;
    if (trunc < 1) trunc = 1;

    if (filt->count == 0) {
        wmove(current_win, 0, 2);
        wprintw(current_win, "No matches.");
    } else {
        for (int v = 0; v < visible && top + v < filt->count; v++) {
            int fidx = top + v;
            int oidx = filt->indices[fidx];
            const char *name = files[oidx] ? files[oidx] : "";
            int is_marked = marked[oidx] != 0;

            char full[PATH_MAX];
            int path_ok = (build_path(full, sizeof full, dir, name) == 0);
            int is_dir = path_ok && (isRegularFile(full) == 0);

            wattrset(current_win, A_NORMAL);

            if (is_marked && has_colors())
                wattron(current_win, COLOR_PAIR(3));
            if (fidx == sel) {
                if (has_colors() && !is_marked)
                    wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            }
            if (is_dir)
                wattron(current_win, A_BOLD);

            wmove(current_win, v, 2);

            char entry_display[PATH_MAX * 2 + 32];
            const char *prefix = is_marked ? "× " : "  ";
            if (path_ok && isSymlink(full)) {
                char target[PATH_MAX];
                if (readSymlinkTarget(full, target, sizeof target) == 0)
                    snprintf(entry_display, sizeof entry_display, "%s%s → %s", prefix, name, target);
                else
                    snprintf(entry_display, sizeof entry_display, "%s%s → (unreadable)", prefix, name);
            } else {
                snprintf(entry_display, sizeof entry_display, "%s%s", prefix, name);
            }
            if ((int)strlen(entry_display) > trunc)
                entry_display[trunc] = '\0';

            wprintw(current_win, "%s", entry_display);

            /* Pad row so background/reverse fills the line */
            if (fidx == sel || is_marked) {
                int used = (int)strlen(entry_display);
                for (int c = 2 + used; c < cols - 1; c++)
                    mvwaddch(current_win, v, c, ' ');
            }

            wattrset(current_win, A_NORMAL);
        }
    }

    wattrset(current_win, A_NORMAL);
    drawScrollbar(current_win, filt->count > 0 ? filt->count : 1, top, false);
    wrefresh(current_win);
}

static void delete_search_draw_info(char **files, search_filt *filt, int sel,
                                    const unsigned char *marked, int n) {
    if (!preview_win) return;

    wclear(preview_win);
    ueberzugpp_remove();
    preview_scroll = 0;
    len_preview = -1;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    (void)cols;

    int line = 0;
    wattron(preview_win, A_BOLD);
    mvwprintw(preview_win, line++, 2, " Deletion info ");
    wattroff(preview_win, A_BOLD);
    line++;

    int nmarked = delete_count_marked(marked, n);
    off_t mtotal = delete_marked_total_size(files, n, marked);
    char msize[32];
    delete_human_size(mtotal, msize, sizeof msize);

    mvwprintw(preview_win, line++, 2, "Marked: %d file(s)", nmarked);
    mvwprintw(preview_win, line++, 2, "Marked size: %s (files only)", msize);
    line++;

    if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count) {
        mvwprintw(preview_win, line++, 2, "No focused entry");
        mvwprintw(preview_win, rows > 4 ? rows - 3 : line + 1, 2, "Tab=toggle  Enter=delete");
        mvwprintw(preview_win, rows > 3 ? rows - 2 : line + 2, 2, "Esc=cancel");
        wrefresh(preview_win);
        return;
    }

    int oidx = filt->indices[sel];
    char path[PATH_MAX];
    if (build_path(path, sizeof path, dir, files[oidx]) != 0) {
        mvwprintw(preview_win, line++, 2, "Path too long");
        wrefresh(preview_win);
        return;
    }

    wattron(preview_win, A_BOLD);
    mvwprintw(preview_win, line++, 2, "Focused entry");
    wattroff(preview_win, A_BOLD);

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    mvwprintw(preview_win, line++, 2, "Name: %s", base);

    /* Truncate long paths for display */
    if ((int)strlen(path) > cols - 10) {
        mvwprintw(preview_win, line++, 2, "Path: ...%s", path + strlen(path) - (cols - 14));
    } else {
        mvwprintw(preview_win, line++, 2, "Path: %s", path);
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        mvwprintw(preview_win, line++, 2, "Type: (unreadable)");
    } else {
        if (S_ISLNK(st.st_mode)) {
            char target[PATH_MAX];
            if (readSymlinkTarget(path, target, sizeof target) == 0)
                mvwprintw(preview_win, line++, 2, "Type: Symlink → %s", target);
            else
                mvwprintw(preview_win, line++, 2, "Type: Symlink (broken)");
        } else if (S_ISDIR(st.st_mode)) {
            mvwprintw(preview_win, line++, 2, "Type: Directory");
        } else if (S_ISREG(st.st_mode)) {
            mvwprintw(preview_win, line++, 2, "Type: Regular file");
        } else {
            mvwprintw(preview_win, line++, 2, "Type: Other");
        }

        char hsize[32];
        delete_human_size(st.st_size, hsize, sizeof hsize);
        mvwprintw(preview_win, line++, 2, "Size: %s (%lld bytes)", hsize, (long long)st.st_size);

        char perms[11];
        perms[0] = S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-');
        perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
        perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
        perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
        perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
        perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
        perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
        perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
        perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
        perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
        perms[10] = '\0';
        mvwprintw(preview_win, line++, 2, "Perms: %s  (0%o)", perms, st.st_mode & 07777);

        char mtime_str[64];
        struct tm *tm = localtime(&st.st_mtime);
        if (tm)
            strftime(mtime_str, sizeof mtime_str, "%Y-%m-%d %H:%M:%S", tm);
        else
            snprintf(mtime_str, sizeof mtime_str, "?");
        mvwprintw(preview_win, line++, 2, "Modified: %s", mtime_str);

        if (marked[oidx]) {
            wattron(preview_win, A_BOLD);
            if (has_colors()) wattron(preview_win, COLOR_PAIR(3));
            mvwprintw(preview_win, line++, 2, "Status: MARKED for deletion");
            if (has_colors()) wattroff(preview_win, COLOR_PAIR(3));
            wattroff(preview_win, A_BOLD);
        } else {
            mvwprintw(preview_win, line++, 2, "Status: not marked");
        }
    }

    line++;
    wattron(preview_win, A_BOLD);
    mvwprintw(preview_win, line++, 2, "WARNING: permanent delete (rm -rf)");
    wattroff(preview_win, A_BOLD);

    if (rows - 3 > line) {
        mvwprintw(preview_win, rows - 3, 2, "Tab=toggle mark");
        mvwprintw(preview_win, rows - 2, 2, "Enter/d=delete  Esc=cancel");
    }

    wrefresh(preview_win);
}

static void delete_one_path(const char *path) {
    int flag = (getWritePermissions((char *)path) == 1) ? 0 : 1;
    pid_t pid = fork();
    if (pid == 0) {
        if (flag == 0)
            execlp("rm", "rm", "-rfv", path, (char *)0);
        else
            execlp("sudo", "sudo", "rm", "-rfv", path, (char *)0);
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* Confirmation dialog: no full-screen backdrop wipe; lists every path to delete.
 * Returns 1 to proceed, 0 to cancel. */
static int delete_confirm_dialog(char **files, int n, const int *targets, int ntargets) {
    if (ntargets <= 0 || !targets || !files)
        return 0;

    int win_w = COLS - 4;
    if (win_w > 72) win_w = 72;
    if (win_w < 40) win_w = COLS > 4 ? COLS - 2 : COLS;
    if (win_w < 20) win_w = 20;

    /* header(3) + footer(3) + list room */
    int max_h = LINES - 2;
    if (max_h < 8) max_h = LINES > 0 ? LINES : 8;
    int list_cap = max_h - 6;
    if (list_cap < 1) list_cap = 1;
    int list_h = ntargets < list_cap ? ntargets : list_cap;
    int win_h = list_h + 6;
    if (win_h > max_h) win_h = max_h;

    /* Floating modal only — do not call ui_overlay_backdrop() */
    WINDOW *win = ui_modal_create(win_h, win_w);
    if (!win) return 0;

    int top = 0;
    int done = 0;
    int confirmed = 0;
    int inner_w = win_w - 4;
    if (inner_w < 8) inner_w = 8;

    while (!done) {
        werase(win);
        box(win, 0, 0);

        wattron(win, A_BOLD);
        if (has_colors())
            wattron(win, COLOR_PAIR(3));
        mvwprintw(win, 0, 2, " Confirm permanent delete ");
        if (has_colors())
            wattroff(win, COLOR_PAIR(3));
        wattroff(win, A_BOLD);

        mvwprintw(win, 1, 2, "%d item(s) will be permanently deleted:", ntargets);

        int max_top = ntargets - list_h;
        if (max_top < 0) max_top = 0;
        if (top > max_top) top = max_top;
        if (top < 0) top = 0;

        for (int i = 0; i < list_h; i++) {
            int t = top + i;
            if (t >= ntargets) break;
            int oidx = targets[t];
            char line[PATH_MAX + 16];
            const char *shown = "?";
            char path[PATH_MAX];
            char typec = '?';

            if (oidx >= 0 && oidx < n && files[oidx]) {
                if (build_path(path, sizeof path, dir, files[oidx]) == 0) {
                    shown = path;
                    struct stat st;
                    if (lstat(path, &st) == 0) {
                        if (S_ISDIR(st.st_mode)) typec = 'd';
                        else if (S_ISLNK(st.st_mode)) typec = 'l';
                        else if (S_ISREG(st.st_mode)) typec = 'f';
                        else typec = 'o';
                    }
                } else {
                    shown = files[oidx];
                }
            }

            /* "  N. [t] path..." */
            char num[16];
            snprintf(num, sizeof num, "%d.", t + 1);
            snprintf(line, sizeof line, "%s [%c] %s", num, typec, shown);
            if ((int)strlen(line) > inner_w)
                line[inner_w] = '\0';

            if (has_colors())
                wattron(win, COLOR_PAIR(3));
            mvwprintw(win, 2 + i, 2, "%s", line);
            if (has_colors())
                wattroff(win, COLOR_PAIR(3));
        }

        if (ntargets > list_h) {
            mvwprintw(win, win_h - 3, 2, "(%d–%d of %d)  ↑/↓ scroll",
                      top + 1, top + list_h < ntargets ? top + list_h : ntargets, ntargets);
        } else {
            mvwprintw(win, win_h - 3, 2, "rm -rf  — cannot be undone");
        }

        wattron(win, A_BOLD);
        mvwprintw(win, win_h - 2, 2, "y = Delete permanently    n/Esc = Cancel");
        wattroff(win, A_BOLD);

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 'y' || ch == 'Y') {
            confirmed = 1;
            done = 1;
        } else if (ch == 'n' || ch == 'N' || ch == 27 || ch == 'q' || ch == 3) {
            confirmed = 0;
            done = 1;
        } else if (ch == KEY_UP || ch == 'k') {
            if (top > 0) top--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (top < max_top) top++;
        } else if (ch == KEY_PPAGE) {
            top -= list_h;
            if (top < 0) top = 0;
        } else if (ch == KEY_NPAGE) {
            top += list_h;
            if (top > max_top) top = max_top;
        }
    }

    ui_modal_destroy(win);
    /* Restore panels under the modal without a full UI rebuild */
    if (current_win) touchwin(current_win);
    if (preview_win) touchwin(preview_win);
    if (status_win) touchwin(status_win);
    if (current_win) wnoutrefresh(current_win);
    if (preview_win) wnoutrefresh(preview_win);
    if (status_win) wnoutrefresh(status_win);
    doupdate();

    return confirmed;
}

/* Returns 1 if files were deleted, 0 if cancelled / nothing done. */
static int delete_search_perform(char **files, int n, const unsigned char *marked,
                                 search_filt *filt, int sel) {
    int *targets = malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    if (!targets) {
        displayAlert("Couldn't allocate memory!");
        return 0;
    }
    int ntargets = 0;

    int any_marked = delete_count_marked(marked, n) > 0;
    if (any_marked) {
        for (int i = 0; i < n; i++) {
            if (marked[i])
                targets[ntargets++] = i;
        }
    } else if (filt && filt->count > 0 && sel >= 0 && sel < filt->count) {
        targets[ntargets++] = filt->indices[sel];
    }

    if (ntargets == 0) {
        free(targets);
        displayAlert("Nothing to delete");
        return 0;
    }

    if (!delete_confirm_dialog(files, n, targets, ntargets)) {
        free(targets);
        return 0;
    }

    endwin();
    for (int t = 0; t < ntargets; t++) {
        int oidx = targets[t];
        if (oidx < 0 || oidx >= n || !files[oidx]) continue;
        char path[PATH_MAX];
        if (build_path(path, sizeof path, dir, files[oidx]) != 0) continue;
        delete_one_path(path);
    }
    free(targets);
    refresh();
    return 1;
}

/* Search-like bulk delete for the current directory listing. */
static void show_delete_search(char **files, int n) {
    if (!files || n <= 0) {
        displayAlert("No files to delete");
        return;
    }
    if (!current_win || !preview_win || !status_win) {
        displayAlert("UI not ready");
        return;
    }

    unsigned char *marked = calloc((size_t)n, 1);
    if (!marked) {
        displayAlert("Couldn't allocate memory!");
        return;
    }

    wchar_t input[DELETE_SEARCH_MAX_INPUT + 1] = {0};
    int input_len = 0;
    int prev_input_len = 0;

    search_filt filt;
    search_filt_init(&filt);
    search_filt_fill_all(&filt, n);

    int sel = 0;
    int top = 0;
    int done = 0;
    int last_info_oidx = -2;
    int last_nmarked = -1;

    keypad(status_win, TRUE);
    meta(status_win, TRUE);
    leaveok(current_win, TRUE);
    leaveok(preview_win, TRUE);

    ueberzugpp_remove();

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

        int nmarked = delete_count_marked(marked, n);
        delete_search_draw_list(files, &filt, sel, top, marked);

        int cur_oidx = (filt.count > 0) ? filt.indices[sel] : -1;
        if (cur_oidx != last_info_oidx || nmarked != last_nmarked) {
            delete_search_draw_info(files, &filt, sel, marked, n);
            last_info_oidx = cur_oidx;
            last_nmarked = nmarked;
        }

        char *qmb = search_wc_to_mb(input);
        delete_search_draw_status(qmb ? qmb : "", sel, filt.count, n, nmarked);
        free(qmb);

        wint_t ch;
        int kc = wget_wch(status_win, &ch);
        if (kc == ERR) continue;

        int need_filter = 0;
        int need_info = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                handleResize();
                last_info_oidx = -2;
                continue;
            }
            if (ch == KEY_UP && sel > 0) {
                sel--;
                need_info = 1;
            } else if (ch == KEY_DOWN && sel < filt.count - 1) {
                sel++;
                need_info = 1;
            } else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127 || ch == 8) {
                if (input_len > 0) {
                    input[--input_len] = L'\0';
                    need_filter = 1;
                }
            } else if (ch == KEY_ENTER) {
                if (delete_search_perform(files, n, marked, &filt, sel))
                    done = 1;
                else
                    last_info_oidx = -2; /* redraw panels after cancelled dialog */
            } else if (ch == KEY_PPAGE) {
                sel -= visible;
                if (sel < 0) sel = 0;
                need_info = 1;
            } else if (ch == KEY_NPAGE) {
                sel += visible;
                if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                need_info = 1;
            } else if (ch == KEY_BTAB) {
                /* ignore shift-tab */
            }
        } else {
            if (ch == 27 || ch == 3 || ch == 4) {
                done = 1;
            } else if (ch == 'q' && input_len == 0) {
                done = 1;
            } else if (ch == '\t') {
                if (filt.count > 0 && sel >= 0 && sel < filt.count) {
                    int oidx = filt.indices[sel];
                    if (oidx >= 0 && oidx < n) {
                        marked[oidx] = marked[oidx] ? 0 : 1;
                        /* advance to next match after toggle (like multi-select UX) */
                        if (sel < filt.count - 1)
                            sel++;
                        need_info = 1;
                    }
                }
            } else if (ch == '\n' || ch == '\r' || ch == 'd') {
                if (delete_search_perform(files, n, marked, &filt, sel))
                    done = 1;
                else
                    last_info_oidx = -2; /* redraw panels after cancelled dialog */
            } else if (iswprint(ch) && ch != L'\t' && input_len < DELETE_SEARCH_MAX_INPUT) {
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
            last_info_oidx = -2;
        } else if (need_info) {
            last_info_oidx = -2;
        }
    }

    curs_set(0);
    leaveok(current_win, FALSE);
    leaveok(preview_win, FALSE);
    ueberzugpp_remove();
    clearFlagImg = 1;

    search_filt_free(&filt);
    free(marked);
}

#endif /* CFL_DELETE_SEARCH_H */
