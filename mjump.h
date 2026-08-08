#ifndef CFL_JUMP_TO_H
#define CFL_JUMP_TO_H

/* mcfl:
 *  - Start in Browse mmenu of cwd; Ctrl+J = recursive Jump under $HOME
 *  - Tab multi-mark → clipboard → Ctrl+V copy / Ctrl+X move (dest under $HOME)
 *  - Enter = KEY_ENTER or CR; Ctrl+J is LF (10). q never quits; Ctrl+Q quits
 * JUMP_OMIT_DIRS skipped under home scan. */

#define JUMP_MAX_INPUT   128
#define JUMP_MAX_DEPTH   6
#define JUMP_INIT_CAP    4096

/* --- clipboard (was clipboard.h) --- */
void setSelectionCount(void) {
	FILE *fp = fopen(clipboard_path, "r");
	if( fp == NULL ) {
		selectedFiles = 0;
		return;
	}
	char buf[PATH_MAX];
	int num = 0;
	while(fgets(buf, PATH_MAX, (FILE*) fp)) {
		num++;
	}
	selectedFiles = num;
	fclose(fp);
}
void writeClipboard(char *filepath) {
	FILE *f = fopen(clipboard_path,"a+");
	if (f == NULL) {
		endwin();
		printf("Error accessing clipboard!\n");
		exit(1);
	}
	fprintf(f, "%s\n", filepath);
	fclose(f);
}
void clearClipboard(void) {
	/* Remove the session clipboard file so selection is fully gone */
	if (clipboard_path)
		remove(clipboard_path);
	selectedFiles = 0;
}


/* --- copy/move (was fs_ops.h) --- */
void copyFiles(char *present_dir) {
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	int flag;
	if(f == NULL) {
		return;
	}

	/* Check write access on destination (not necessarily the browsed dir) */
	if(getWritePermissions(present_dir) == 1) {
		flag = 0;
	} else {
		flag = 1;
	}
	endwin();
	while(fgets(buf, PATH_MAX, (FILE*) f)) {
		buf[strlen(buf)-1] = '\0';
		replace(buf,"//","\n");
		pid_t pid = fork();
		if(pid == 0) {
			if(flag == 0) { 
				execlp("cp", "cp", "-dirv", buf, present_dir, (char *)0);
			} else {
				execlp("sudo", "sudo", "cp", "-dirv", buf, present_dir, (char *)0);
			}
			exit(1);
		} else {
			int status;
			waitpid(pid, &status, 0);
		}
	}
	refresh();
	fclose(f);
	clearClipboard();
}
void moveFiles(char *present_dir) {
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	char dest_path[PATH_MAX];

	if (f == NULL) {
		perror("Failed to open clipboard file");
		return;
	}

	endwin();
	while (fgets(buf, PATH_MAX, f)) {
		// Remove the newline character
		buf[strlen(buf) - 1] = '\0';
		replace(buf, "//", "\n");

		// Construct the destination path
		const char *base = strrchr(buf, '/');
		base = base ? base + 1 : buf;
		snprintf(dest_path, PATH_MAX, "%s/%s", present_dir, base);

		// Move the file
		if (rename(buf, dest_path) != 0) {
			perror("Error moving file");
		} else {
			printf("Moved: %s -> %s\n", buf, dest_path);
		}
	}

	fclose(f);
	refresh();
	clearClipboard();
}


/* --- trash (was trash.h) --- */

static int trash_mkdir_p(const char *path, mode_t mode) {
	char tmp[PATH_MAX];
	if (snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp)
		return -1;
	size_t len = strlen(tmp);
	if (len == 0) return -1;
	if (tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
			*p = '/';
			return -1;
		}
		*p = '/';
	}
	if (mkdir(tmp, mode) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

/* Ensure $trash, $trash/files, $trash/info exist (0700). Returns 0 on success. */
static int trash_ensure_home(void) {
	if (!trash_path) return -1;
	if (trash_mkdir_p(trash_path, 0700) != 0)
		return -1;

	char files_dir[PATH_MAX], info_dir[PATH_MAX];
	if (snprintf(files_dir, sizeof files_dir, "%s/files", trash_path) >= (int)sizeof files_dir)
		return -1;
	if (snprintf(info_dir, sizeof info_dir, "%s/info", trash_path) >= (int)sizeof info_dir)
		return -1;
	if (mkdir(files_dir, 0700) != 0 && errno != EEXIST)
		return -1;
	if (mkdir(info_dir, 0700) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

/* URL-style percent-encode for trashinfo Path= (RFC 2396 unreserved kept). */
static int trash_url_encode(const char *in, char *out, size_t outsz) {
	static const char *hex = "0123456789ABCDEF";
	if (!in || !out || outsz == 0) return -1;
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
		int unreserved =
			(*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
			(*p >= '0' && *p <= '9') ||
			*p == '-' || *p == '_' || *p == '.' || *p == '~' || *p == '/';
		if (unreserved) {
			if (o + 1 >= outsz) return -1;
			out[o++] = (char)*p;
		} else {
			if (o + 3 >= outsz) return -1;
			out[o++] = '%';
			out[o++] = hex[(*p >> 4) & 0xf];
			out[o++] = hex[*p & 0xf];
		}
	}
	out[o] = '\0';
	return 0;
}

/* Pick a unique basename under trash/files and open trash/info/name.trashinfo with O_EXCL. */
static int trash_alloc_name(const char *orig_base, char *out_name, size_t namesz,
                            char *info_path, size_t infosz, int *info_fd) {
	if (!orig_base || !orig_base[0])
		orig_base = "unknown";

	/* Sanitize: no slashes in trash basename */
	char base[NAME_MAX + 1];
	snprintf(base, sizeof base, "%s", orig_base);
	for (char *p = base; *p; p++) {
		if (*p == '/') *p = '_';
	}

	for (int n = 0; n < 10000; n++) {
		if (n == 0)
			snprintf(out_name, namesz, "%s", base);
		else
			snprintf(out_name, namesz, "%s.%d", base, n);

		char files_path[PATH_MAX];
		if (snprintf(files_path, sizeof files_path, "%s/files/%s", trash_path, out_name)
		    >= (int)sizeof files_path)
			return -1;
		if (snprintf(info_path, infosz, "%s/info/%s.trashinfo", trash_path, out_name)
		    >= (int)infosz)
			return -1;

		/* Atomic info create first (per spec) */
		int fd = open(info_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd < 0) {
			if (errno == EEXIST)
				continue;
			return -1;
		}
		/* Ensure files/ name is free too */
		if (access(files_path, F_OK) == 0) {
			close(fd);
			unlink(info_path);
			continue;
		}
		*info_fd = fd;
		return 0;
	}
	return -1;
}

static int trash_write_info(int fd, const char *orig_path) {
	char encoded[PATH_MAX * 3];
	if (trash_url_encode(orig_path, encoded, sizeof encoded) != 0)
		return -1;

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char date[32];
	if (!tm || strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", tm) == 0)
		snprintf(date, sizeof date, "1970-01-01T00:00:00");

	char body[PATH_MAX * 3 + 128];
	int n = snprintf(body, sizeof body,
	                 "[Trash Info]\nPath=%s\nDeletionDate=%s\n",
	                 encoded, date);
	if (n < 0 || (size_t)n >= sizeof body)
		return -1;
	if (write(fd, body, (size_t)n) != (ssize_t)n)
		return -1;
	return 0;
}

/* Move (or copy+delete on EXDEV) src into dest path. */
static int trash_place_file(const char *src, const char *dest) {
	if (rename(src, dest) == 0)
		return 0;
	if (errno != EXDEV)
		return -1;

	/* Cross-device: copy then remove */
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		execlp("cp", "cp", "-a", src, dest, (char *)NULL);
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		/* Leave the copy in trash rather than lose both */
		return 0;
	}
	if (pid == 0) {
		execlp("rm", "rm", "-rf", src, (char *)NULL);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	return 0;
}

/* Trash one absolute path into the FreeDesktop home trash. Returns 0 on success. */
static int trash_one(const char *orig_path) {
	if (!orig_path || !orig_path[0])
		return -1;

	struct stat st;
	if (lstat(orig_path, &st) != 0)
		return -1;

	const char *base = strrchr(orig_path, '/');
	base = base ? base + 1 : orig_path;
	if (!base[0])
		return -1;

	char trash_name[NAME_MAX + 16];
	char info_path[PATH_MAX];
	int info_fd = -1;
	if (trash_alloc_name(base, trash_name, sizeof trash_name,
	                     info_path, sizeof info_path, &info_fd) != 0)
		return -1;

	if (trash_write_info(info_fd, orig_path) != 0) {
		close(info_fd);
		unlink(info_path);
		return -1;
	}
	close(info_fd);

	char dest[PATH_MAX];
	if (snprintf(dest, sizeof dest, "%s/files/%s", trash_path, trash_name) >= (int)sizeof dest) {
		unlink(info_path);
		return -1;
	}

	if (trash_place_file(orig_path, dest) != 0) {
		unlink(info_path);
		return -1;
	}
	return 0;
}

/* Move clipboard selection into FreeDesktop Trash (not a raw dump folder). */
void trashFiles(void) {
	FILE *f = fopen(clipboard_path, "r");
	if (f == NULL) {
		displayAlert("Select some files first!");
		return;
	}

	if (trash_ensure_home() != 0) {
		fclose(f);
		displayAlert("Cannot create Trash directory");
		return;
	}

	endwin();
	int ok = 0, fail = 0;
	char buf[PATH_MAX];
	while (fgets(buf, PATH_MAX, f)) {
		size_t L = strlen(buf);
		if (L > 0 && buf[L - 1] == '\n')
			buf[L - 1] = '\0';
		replace(buf, "//", "\n");
		if (trash_one(buf) == 0) {
			printf("Trashed: %s\n", buf);
			ok++;
		} else {
			perror(buf);
			fail++;
		}
	}
	fclose(f);
	refresh();
	clearClipboard();

	if (fail && ok)
		displayAlert("Some items failed to trash");
	else if (fail && !ok)
		displayAlert("Trash failed");
}

/* Full mouse handler for both scrollbars + wheel.
 * Hit tests using actual window geometry so it survives layout changes.
 * Wheel routes by X position relative to preview_start.
 */


/* --- openfile (was openfile.h) --- */

void openFile(char *filepath) {
    sigprocmask(SIG_BLOCK, &x, NULL);
    pid_t pid;
    pid = fork();
    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(null_fd,2);
        execlp(FILE_OPENER, FILE_OPENER, filepath);
        exit(1);
    } else {
        endwin();
        int status;
        waitpid(pid, &status, 0);
        return;
    }
}


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

#define MCFL_CTRL_V 22  /* copy-to */
#define MCFL_CTRL_X 24  /* move-to */
#define MCFL_CTRL_Q 17  /* quit */
#define MCFL_CTRL_J 10  /* jump-to ($HOME) = Ctrl+J = LF; Enter is KEY_ENTER or CR after nonl() */

/* Same idea as suckless/cfl mmenu.h: KEY_BACKSPACE/KEY_DC as keycodes,
 * or ASCII BS/DEL (8/127) when raw() delivers them with kc==OK. */
static int mcfl_is_backspace(int kc, wint_t ch)
{
	if (kc == KEY_CODE_YES)
		return (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127 || ch == 8);
	return (ch == 8 || ch == 127 || ch == KEY_BACKSPACE);
}

/* Confirm / hop: never treat LF as Enter (that is Ctrl+J). */
static int mcfl_is_enter(int kc, wint_t ch)
{
	if (kc == KEY_CODE_YES)
		return (ch == KEY_ENTER);
	return (ch == '\r' || ch == KEY_ENTER);
}

static void mcfl_input_backspace(wchar_t *input, int *input_len, int *need_filter)
{
	if (*input_len > 0) {
		input[--(*input_len)] = L'\0';
		if (need_filter)
			*need_filter = 1;
	}
}

/* Search prompt only — no key-hint chrome on the right. */
static void jump_draw_status(dir_pick_mode mode, const char *query_mb,
                             int sel, int matches, int total, int nmarked) {
    (void)sel;
    (void)matches;
    (void)total;
    (void)nmarked;
    if (!status_win) return;
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    const char *prompt =
        (mode == DIR_PICK_MOVE) ? "Move to > " :
        (mode == DIR_PICK_COPY) ? "Copy to > " : "Jump > ";
    wprintw(status_win, "%s", prompt);
    wattroff(status_win, A_BOLD);
    wprintw(status_win, "%s", query_mb ? query_mb : "");

    int cols = getmaxx(status_win);
    int qlen = query_mb ? (int)strlen(query_mb) : 0;
    int cursor_col = 1 + (int)strlen(prompt) + qlen;
    if (cursor_col >= cols - 1)
        cursor_col = cols - 2;
    if (cursor_col < 1) cursor_col = 1;
    wmove(status_win, 0, cursor_col);
    curs_set(1);
    wrefresh(status_win);
}

/* marks may be NULL (no multi-mark chrome). */
static void jump_draw_list(char **paths, search_filt *filt, int sel, int top,
                           const unsigned char *marks, int npaths) {
    if (!current_win) return;
    wclear(current_win);

    int rows, cols;
    getmaxyx(current_win, rows, cols);
    int visible = rows > 0 ? rows : 1;
    /*
     * List layout: content | 1 blank | bar | 1 blank
     * (bar inset; trailing blank on the right of the list scrollbar).
     * 1-col mark gutter remains in the entry string ("×" / " ").
     */
    int trunc = cols - 3;
    if (trunc < 1) trunc = 1;

    if (filt->count == 0) {
        wmove(current_win, 0, 0);
        wprintw(current_win, "No matches.");
    } else {
        for (int v = 0; v < visible && top + v < filt->count; v++) {
            int fidx = top + v;
            int oidx = filt->indices[fidx];
            const char *name = paths[oidx] ? paths[oidx] : "";
            int is_marked = (marks && oidx >= 0 && oidx < npaths && marks[oidx]);

            wattrset(current_win, A_NORMAL);
            if (is_marked && has_colors())
                wattron(current_win, COLOR_PAIR(3));
            if (fidx == sel) {
                if (has_colors() && !is_marked)
                    wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            }

            char entry_display[PATH_MAX + 8];
            const char *prefix = is_marked ? "×" : " ";
            snprintf(entry_display, sizeof entry_display, "%s%s", prefix, name);
            if ((int)strlen(entry_display) > trunc) {
                int elen = (int)strlen(entry_display);
                int keep = trunc - 3;
                if (keep < 1) keep = trunc;
                if (keep < elen) {
                    snprintf(entry_display, sizeof entry_display, "...%s",
                             name + (int)strlen(name) - (keep > 2 ? keep - 2 : keep));
                    if ((int)strlen(entry_display) > trunc)
                        entry_display[trunc] = '\0';
                }
            }

            wmove(current_win, v, 0);
            wprintw(current_win, "%s", entry_display);

            if (fidx == sel || is_marked) {
                int used = (int)strlen(entry_display);
                for (int c = used; c < cols - 3; c++)
                    mvwaddch(current_win, v, c, ' ');
            }
            wattrset(current_win, A_NORMAL);
        }
    }

    drawScrollbar(current_win, filt->count > 0 ? filt->count : 1, top, false);
    wrefresh(current_win);
}

/* Fill session clipboard from absolute path marks (or focus). marks → clipboard. */
static int clipboard_from_abs_marks(char **paths, int n, const unsigned char *marked,
                                    search_filt *filt, int sel)
{
    clearClipboard();
    int nsrc = 0;
    int any = 0;
    if (marked) {
        for (int i = 0; i < n; i++) {
            if (marked[i]) {
                any = 1;
                break;
            }
        }
    }
    if (any) {
        for (int i = 0; i < n; i++) {
            if (!marked[i] || !paths[i]) continue;
            char *temp = strdup(paths[i]);
            if (!temp) continue;
            writeClipboard(replace(temp, "\n", "//"));
            free(temp);
            nsrc++;
        }
    } else if (filt && filt->count > 0 && sel >= 0 && sel < filt->count) {
        int oidx = filt->indices[sel];
        if (oidx >= 0 && oidx < n && paths[oidx]) {
            char *temp = strdup(paths[oidx]);
            if (temp) {
                writeClipboard(replace(temp, "\n", "//"));
                free(temp);
                nsrc = 1;
            }
        }
    }
    setSelectionCount();
    return nsrc;
}

/* Relative basenames in global dir → absolute clipboard. */
static int clipboard_from_rel_marks(char **files, int n, const unsigned char *marked,
                                    search_filt *filt, int sel)
{
    clearClipboard();
    int nsrc = 0;
    int any = search_count_marked(marked, n) > 0;
    if (any) {
        for (int i = 0; i < n; i++) {
            if (!marked[i] || !files[i]) continue;
            char path[PATH_MAX];
            if (build_path(path, sizeof path, dir, files[i]) != 0) continue;
            char *temp = strdup(path);
            if (!temp) continue;
            writeClipboard(replace(temp, "\n", "//"));
            free(temp);
            nsrc++;
        }
    } else if (filt && filt->count > 0 && sel >= 0 && sel < filt->count) {
        int oidx = filt->indices[sel];
        if (oidx >= 0 && oidx < n && files[oidx]) {
            char path[PATH_MAX];
            if (build_path(path, sizeof path, dir, files[oidx]) == 0) {
                char *temp = strdup(path);
                if (temp) {
                    writeClipboard(replace(temp, "\n", "//"));
                    free(temp);
                    nsrc = 1;
                }
            }
        }
    }
    setSelectionCount();
    return nsrc;
}

static int jump_scan_home(jump_list *jl)
{
    jump_list_init(jl);
    if (!jl->paths)
        return -1;
    const char *home = getenv("HOME");
    if (!home || !home[0])
        home = (info && info->pw_dir) ? info->pw_dir : NULL;
    if (!home || !home[0]) {
        jump_list_free(jl);
        return -1;
    }
    struct stat st;
    if (stat(home, &st) != 0 || !S_ISDIR(st.st_mode)) {
        jump_list_free(jl);
        return -1;
    }
    jump_collect(jl, home, 0);
    if (jl->count <= 0) {
        jump_list_free(jl);
        return -1;
    }
    qsort(jl->paths, (size_t)jl->count, sizeof(char *), jump_path_cmp);
    return 0;
}

/* Dest-only picker: clipboard already filled (marks → clipboard → dest).
 * Returns 1 if move/copy ran, 0 if cancelled / failed to start. */
static int show_dir_pick_dest(dir_pick_mode mode)
{
    if (mode != DIR_PICK_MOVE && mode != DIR_PICK_COPY)
        return 0;
    if (!current_win || !preview_win || !status_win) {
        displayAlert("UI not ready");
        return 0;
    }
    if (access(clipboard_path, F_OK) != 0 || selectedFiles <= 0) {
        displayAlert("Nothing to transfer");
        return 0;
    }

    ueberzugpp_remove();
    wclear(status_win);
    wmove(status_win, 0, 1);
    wattron(status_win, A_BOLD);
    wprintw(status_win, "%s scanning $HOME ...",
            mode == DIR_PICK_MOVE ? "Move to >" : "Copy to >");
    wattroff(status_win, A_BOLD);
    wrefresh(status_win);

    jump_list jl;
    if (jump_scan_home(&jl) != 0) {
        displayAlert("No directories under $HOME");
        return 0;
    }

    wchar_t input[JUMP_MAX_INPUT + 1] = {0};
    int input_len = 0;
    int prev_input_len = 0;
    search_filt filt;
    search_filt_init(&filt);
    search_filt_fill_all(&filt, jl.count);
    search_filt_sort_short(&filt, jl.paths, NULL);

    int sel = 0, top = 0, done = 0, confirmed = 0;
    int last_preview_oidx = -2;
    char **preview_listing = NULL;
    int preview_n = 0;

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
            sel = top = 0;
        }

        jump_draw_list(jl.paths, &filt, sel, top, NULL, 0);
        int cur_oidx = (filt.count > 0) ? filt.indices[sel] : -1;
        if (cur_oidx != last_preview_oidx) {
            search_draw_preview_paths(jl.paths, &filt, sel, &preview_listing, &preview_n);
            last_preview_oidx = cur_oidx;
        }
        char *qmb = search_wc_to_mb(input);
        jump_draw_status(mode, qmb ? qmb : "", sel, filt.count, jl.count, selectedFiles);
        free(qmb);

        wint_t ch;
        int kc = wget_wch(status_win, &ch);
        if (kc == ERR) continue;
        int need_filter = 0, need_preview = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                handleResize();
                last_preview_oidx = -2;
                continue;
            }
            if (ch == KEY_UP && sel > 0) { sel--; need_preview = 1; }
            else if (ch == KEY_DOWN && sel < filt.count - 1) { sel++; need_preview = 1; }
            else if (mcfl_is_backspace(kc, ch)) {
                mcfl_input_backspace(input, &input_len, &need_filter);
            } else if (mcfl_is_enter(kc, ch)) {
                if (filt.count > 0) { confirmed = 1; done = 1; }
            } else if (ch == KEY_PPAGE) {
                sel -= visible; if (sel < 0) sel = 0; need_preview = 1;
            } else if (ch == KEY_NPAGE) {
                sel += visible;
                if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                need_preview = 1;
            }
        } else {
            if (mcfl_is_backspace(kc, ch)) {
                mcfl_input_backspace(input, &input_len, &need_filter);
            } else if (mcfl_is_enter(kc, ch)) {
                if (filt.count > 0) { confirmed = 1; done = 1; }
            } else if (ch == MCFL_CTRL_Q) {
                done = 1;
                confirmed = 0;
            } else if (ch == 27) {
                done = 1;
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
            sel = top = 0;
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

    int did_transfer = 0;
    if (confirmed && filt.count > 0) {
        int oidx = filt.indices[sel];
        if (oidx >= 0 && oidx < jl.count && jl.paths[oidx]) {
            struct stat st;
            if (stat(jl.paths[oidx], &st) == 0 && S_ISDIR(st.st_mode)) {
                if (mode == DIR_PICK_MOVE)
                    moveFiles(jl.paths[oidx]);
                else
                    copyFiles(jl.paths[oidx]);
                did_transfer = 1;
            } else {
                displayAlert(mode == DIR_PICK_MOVE ? "Cannot move there" : "Cannot copy there");
            }
        }
    }

    search_free_listing(&preview_listing, &preview_n);
    search_filt_free(&filt);
    jump_list_free(&jl);

    if (did_transfer) {
        /* move/copy used endwin(); rebuild panes before browse reloads */
        handleResize();
    }
    return did_transfer;
}

enum { MCFL_OK = 0, MCFL_QUIT = 1, MCFL_BACK = 2 };

static int mcfl_jump_loop(void); /* Ctrl+J from browse */

/* Directory contents mmenu — primary UI (cwd / navigated dirs). */
static int mcfl_dir_browser(void)
{
    for (;;) {
        char **files = NULL;
        int n = getFiles(dir, &files);
        if (n < 0) {
            displayAlert("Couldn't open directory");
            /* try parent so we don't hard-quit the session */
            char prev[PATH_MAX];
            snprintf(prev, sizeof prev, "%s", dir);
            getParentPath(dir);
            if (strcmp(prev, dir) == 0)
                return MCFL_QUIT;
            continue;
        }
        if (n > 0) {
            if (snprintf(sort_dir, sizeof sort_dir, "%s", dir) < (int)sizeof sort_dir)
                qsort(files, (size_t)n, sizeof(char *), compare);
        }

        unsigned char *marked = calloc((size_t)(n > 0 ? n : 1), 1);
        if (!marked) {
            for (int i = 0; i < n; i++) free(files[i]);
            free(files);
            displayAlert("Couldn't allocate memory!");
            return MCFL_BACK;
        }

        wchar_t input[JUMP_MAX_INPUT + 1] = {0};
        int input_len = 0, prev_input_len = 0;
        search_filt filt;
        search_filt_init(&filt);
        search_filt_fill_all(&filt, n);
        /* Browse display order: shorter names first (same as empty-query filter) */
        search_filt_sort_short(&filt, files, NULL);

        int sel = 0, top = 0, leave = 0, result = MCFL_OK;
        int last_info = -2, last_nm = -1;
        char **preview_listing = NULL;
        int preview_n = 0;

        ueberzugpp_remove();
        keypad(status_win, TRUE);
        meta(status_win, TRUE);
        leaveok(current_win, TRUE);
        leaveok(preview_win, TRUE);

        while (!leave) {
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
            } else {
                sel = top = 0;
            }

            int nmarked = search_count_marked(marked, n);
            search_draw_marked_list(files, &filt, sel, top, marked);
            int cur = filt.count > 0 ? filt.indices[sel] : -1;
            if (cur != last_info || nmarked != last_nm) {
                search_draw_preview(files, &filt, sel, &preview_listing, &preview_n);
                last_info = cur;
                last_nm = nmarked;
            }

            char *qmb = search_wc_to_mb(input);
            if (status_win) {
                wclear(status_win);
                wmove(status_win, 0, 1);
                wattron(status_win, A_BOLD);
                wprintw(status_win, "Browse > ");
                wattroff(status_win, A_BOLD);
                wprintw(status_win, "%s", qmb ? qmb : "");
                int sc = getmaxx(status_win);
                int qlen = qmb ? (int)strlen(qmb) : 0;
                int cc = 1 + 9 + qlen;
                if (cc >= sc - 1) cc = sc - 2;
                if (cc < 1) cc = 1;
                wmove(status_win, 0, cc);
                curs_set(1);
                wrefresh(status_win);
            }
            free(qmb);

            wint_t ch;
            int kc = wget_wch(status_win, &ch);
            if (kc == ERR) continue;
            int need_filter = 0, need_prev = 0;

            if (kc == KEY_CODE_YES) {
                if (ch == KEY_RESIZE) { handleResize(); last_info = -2; continue; }
                if (ch == KEY_UP && sel > 0) { sel--; need_prev = 1; }
                else if (ch == KEY_DOWN && sel < filt.count - 1) { sel++; need_prev = 1; }
                else if (mcfl_is_backspace(kc, ch)) {
                    mcfl_input_backspace(input, &input_len, &need_filter);
                } else if (mcfl_is_enter(kc, ch)) {
                    if (filt.count > 0) {
                        int oidx = filt.indices[sel];
                        if (oidx >= 0 && oidx < n && files[oidx]) {
                            char path[PATH_MAX];
                            if (build_path(path, sizeof path, dir, files[oidx]) == 0) {
                                if (isRegularFile(path) == 0) {
                                    snprintf(dir, sizeof dir, "%s", path);
                                    leave = 1;
                                    result = MCFL_OK; /* reload same browser */
                                } else {
                                    openFile(path);
                                }
                            }
                        }
                    }
                } else if (ch == KEY_PPAGE) {
                    sel -= visible; if (sel < 0) sel = 0; need_prev = 1;
                } else if (ch == KEY_NPAGE) {
                    sel += visible;
                    if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                    need_prev = 1;
                }
            } else {
                if (mcfl_is_backspace(kc, ch)) {
                    mcfl_input_backspace(input, &input_len, &need_filter);
                } else if (mcfl_is_enter(kc, ch)) {
                    if (filt.count > 0) {
                        int oidx = filt.indices[sel];
                        if (oidx >= 0 && oidx < n && files[oidx]) {
                            char path[PATH_MAX];
                            if (build_path(path, sizeof path, dir, files[oidx]) == 0) {
                                if (isRegularFile(path) == 0) {
                                    snprintf(dir, sizeof dir, "%s", path);
                                    leave = 1;
                                    result = MCFL_OK;
                                } else {
                                    openFile(path);
                                }
                            }
                        }
                    }
                } else if (ch == MCFL_CTRL_Q) {
                    leave = 1;
                    result = MCFL_QUIT;
                } else if (ch == MCFL_CTRL_J) {
                    /* recursive jump under $HOME; on OK reload browse at new dir */
                    int jr = mcfl_jump_loop();
                    if (jr == MCFL_QUIT) {
                        leave = 1;
                        result = MCFL_QUIT;
                    } else if (jr == MCFL_OK) {
                        leave = 1;
                        result = MCFL_OK; /* reload listing for new dir */
                    }
                    last_info = -2;
                } else if (ch == 27) {
                    /* parent directory */
                    char prev[PATH_MAX];
                    snprintf(prev, sizeof prev, "%s", dir);
                    getParentPath(dir);
                    if (strcmp(prev, dir) != 0) {
                        leave = 1;
                        result = MCFL_OK;
                    }
                } else if (ch == MCFL_CTRL_V || ch == MCFL_CTRL_X) {
                    if (clipboard_from_rel_marks(files, n, marked, &filt, sel) > 0) {
                        if (show_dir_pick_dest(ch == MCFL_CTRL_V ? DIR_PICK_COPY : DIR_PICK_MOVE)) {
                            /* Rescan cwd so moved/copied-away items disappear / order resets */
                            leave = 1;
                            result = MCFL_OK;
                        } else {
                            last_info = -2;
                        }
                    } else {
                        displayAlert("Nothing to transfer");
                        last_info = -2;
                    }
                } else if (ch == '\t') {
                    if (filt.count > 0) {
                        int oidx = filt.indices[sel];
                        if (oidx >= 0 && oidx < n) {
                            marked[oidx] = marked[oidx] ? 0 : 1;
                            if (sel < filt.count - 1) sel++;
                            need_prev = 1;
                        }
                    }
                } else if (iswprint(ch) && ch != L'\t' && input_len < JUMP_MAX_INPUT) {
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
                sel = top = 0;
                last_info = -2;
            } else if (need_prev) {
                last_info = -2;
            }
        }

        curs_set(0);
        leaveok(current_win, FALSE);
        leaveok(preview_win, FALSE);
        search_free_listing(&preview_listing, &preview_n);
        search_filt_free(&filt);
        free(marked);
        for (int i = 0; i < n; i++) free(files[i]);
        free(files);
        ueberzugpp_remove();
        clearFlagImg = 1;

        if (result == MCFL_QUIT)
            return MCFL_QUIT;
        /* MCFL_OK: subdir/parent/jump — reload getFiles */
    }
}

/* Recursive jump under $HOME (Ctrl+J). Returns MCFL_OK if dir changed, BACK cancel, QUIT. */
static int mcfl_jump_loop(void)
{
    if (status_win) {
        wclear(status_win);
        wmove(status_win, 0, 1);
        wattron(status_win, A_BOLD);
        wprintw(status_win, "Jump > scanning $HOME ...");
        wattroff(status_win, A_BOLD);
        wrefresh(status_win);
    }

    jump_list jl;
    if (jump_scan_home(&jl) != 0) {
        displayAlert("No directories under $HOME");
        return MCFL_BACK;
    }

    unsigned char *marked = calloc((size_t)jl.count, 1);
    if (!marked) {
        jump_list_free(&jl);
        displayAlert("Couldn't allocate memory!");
        return MCFL_BACK;
    }

    wchar_t input[JUMP_MAX_INPUT + 1] = {0};
    int input_len = 0, prev_input_len = 0;
    search_filt filt;
    search_filt_init(&filt);
    search_filt_fill_all(&filt, jl.count);
    search_filt_sort_short(&filt, jl.paths, NULL);

    int sel = 0, top = 0, done = 0, result = MCFL_OK;
    int last_preview_oidx = -2;
    char **preview_listing = NULL;
    int preview_n = 0;

    ueberzugpp_remove();
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
            sel = top = 0;
        }

        int nmarked = 0;
        for (int i = 0; i < jl.count; i++)
            if (marked[i]) nmarked++;

        jump_draw_list(jl.paths, &filt, sel, top, marked, jl.count);
        int cur_oidx = filt.count > 0 ? filt.indices[sel] : -1;
        if (cur_oidx != last_preview_oidx) {
            search_draw_preview_paths(jl.paths, &filt, sel, &preview_listing, &preview_n);
            last_preview_oidx = cur_oidx;
        }
        char *qmb = search_wc_to_mb(input);
        jump_draw_status(DIR_PICK_JUMP, qmb ? qmb : "", sel, filt.count, jl.count, nmarked);
        free(qmb);

        wint_t ch;
        int kc = wget_wch(status_win, &ch);
        if (kc == ERR) continue;
        int need_filter = 0, need_preview = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) { handleResize(); last_preview_oidx = -2; continue; }
            if (ch == KEY_UP && sel > 0) { sel--; need_preview = 1; }
            else if (ch == KEY_DOWN && sel < filt.count - 1) { sel++; need_preview = 1; }
            else if (mcfl_is_backspace(kc, ch)) {
                mcfl_input_backspace(input, &input_len, &need_filter);
            } else if (mcfl_is_enter(kc, ch)) {
                if (filt.count > 0) {
                    int oidx = filt.indices[sel];
                    if (oidx >= 0 && oidx < jl.count && jl.paths[oidx]) {
                        struct stat st;
                        if (stat(jl.paths[oidx], &st) == 0 && S_ISDIR(st.st_mode)) {
                            snprintf(dir, sizeof dir, "%s", jl.paths[oidx]);
                            done = 1;
                            result = MCFL_OK; /* return to browse at this dir */
                        }
                    }
                }
            } else if (ch == KEY_PPAGE) {
                sel -= visible; if (sel < 0) sel = 0; need_preview = 1;
            } else if (ch == KEY_NPAGE) {
                sel += visible;
                if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                need_preview = 1;
            }
        } else {
            if (mcfl_is_backspace(kc, ch)) {
                mcfl_input_backspace(input, &input_len, &need_filter);
            } else if (mcfl_is_enter(kc, ch)) {
                if (filt.count > 0) {
                    int oidx = filt.indices[sel];
                    if (oidx >= 0 && oidx < jl.count && jl.paths[oidx]) {
                        struct stat st;
                        if (stat(jl.paths[oidx], &st) == 0 && S_ISDIR(st.st_mode)) {
                            snprintf(dir, sizeof dir, "%s", jl.paths[oidx]);
                            done = 1;
                            result = MCFL_OK;
                        }
                    }
                }
            } else if (ch == MCFL_CTRL_Q) {
                done = 1;
                result = MCFL_QUIT;
            } else if (ch == 27) {
                done = 1;
                result = MCFL_BACK; /* cancel jump, keep previous dir */
            } else if (ch == MCFL_CTRL_V || ch == MCFL_CTRL_X) {
                /* marks → clipboard → dest */
                if (clipboard_from_abs_marks(jl.paths, jl.count, marked, &filt, sel) > 0)
                    show_dir_pick_dest(ch == MCFL_CTRL_V ? DIR_PICK_COPY : DIR_PICK_MOVE);
                else
                    displayAlert("Nothing to transfer");
                last_preview_oidx = -2;
            } else if (ch == '\t') {
                if (filt.count > 0) {
                    int oidx = filt.indices[sel];
                    if (oidx >= 0 && oidx < jl.count) {
                        marked[oidx] = marked[oidx] ? 0 : 1;
                        if (sel < filt.count - 1) sel++;
                        need_preview = 1;
                    }
                }
            } else if (iswprint(ch) && ch != L'\t' && input_len < JUMP_MAX_INPUT) {
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
            sel = top = 0;
            last_preview_oidx = -2;
        } else if (need_preview) {
            last_preview_oidx = -2;
        }
    }

    curs_set(0);
    leaveok(current_win, FALSE);
    leaveok(preview_win, FALSE);
    search_free_listing(&preview_listing, &preview_n);
    search_filt_free(&filt);
    free(marked);
    jump_list_free(&jl);
    ueberzugpp_remove();
    clearFlagImg = 1;
    return result;
}

static void run_mcfl_session(void)
{
    /* Primary UI: Browse (cwd). -j or Ctrl+J: recursive Jump under $HOME. */
    if (start_jump_to) {
        start_jump_to = 0;
        int jr = mcfl_jump_loop();
        if (jr == MCFL_QUIT)
            return;
        /* MCFL_OK: dir set from jump; MCFL_BACK: stay on previous dir */
    }
    while (mcfl_dir_browser() != MCFL_QUIT)
        ;
}

#endif /* CFL_JUMP_TO_H */
