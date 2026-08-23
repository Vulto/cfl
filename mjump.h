#ifndef CFL_JUMP_TO_H
#define CFL_JUMP_TO_H

/* mcfl:
 *  - Start in Browse mmenu of cwd; Ctrl+J = recursive Jump under $HOME
 *  - Tab multi-mark → Ctrl+C copy-to / Ctrl+X move-to (dest menu under $HOME)
 *  - Dest menu: Enter confirms only; Esc cancels (dirs only — no file open)
 *  - Del trash; Ctrl+R rename; Ctrl+Z undo; Ctrl+A select all; Alt+A invert
 *  - F1 help (blocks other ops while open)
 *  - Browse/Jump: Enter/→ open; ← parent path
 *  - Esc: close Jump/dest; on Browse quit with lastdir (shell may cd)
 *  - Ctrl+Q: quit immediately from any mode, no lastdir / no shell cd
 *  - Ctrl+J is LF (10). q is filter text (does not quit)
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

/* --- op log: raw command output on preview until cursor moves --- */
#define OP_LOG_MAX (64 * 1024)
static char *op_log_buf = NULL;
static int op_log_active = 0;

static void op_log_clear(void)
{
	free(op_log_buf);
	op_log_buf = NULL;
	op_log_active = 0;
}

static void op_log_set(const char *text)
{
	op_log_clear();
	if (!text || !text[0])
		return;
	size_t n = strlen(text);
	if (n > OP_LOG_MAX)
		n = OP_LOG_MAX;
	op_log_buf = malloc(n + 1);
	if (!op_log_buf)
		return;
	memcpy(op_log_buf, text, n);
	op_log_buf[n] = '\0';
	op_log_active = 1;
}

/* Draw op log into preview; return 1 if drawn. */
static int op_log_draw(void)
{
	if (!op_log_active || !op_log_buf || !preview_win)
		return 0;
	wclear(preview_win);
	int rows, cols;
	getmaxyx(preview_win, rows, cols);
	int maxw = cols > 2 ? cols - 2 : 1;
	int y = 0;
	const char *p = op_log_buf;
	while (*p && y < rows) {
		int len = 0;
		while (p[len] && p[len] != '\n' && len < maxw)
			len++;
		mvwaddnstr(preview_win, y, 0, p, len);
		y++;
		p += len;
		if (*p == '\n')
			p++;
		else if (len >= maxw) {
			while (*p && *p != '\n')
				p++;
			if (*p == '\n')
				p++;
		}
	}
	wrefresh(preview_win);
	return 1;
}

/* Forward decl — defined with trash helpers below. */
static int trash_place_file(const char *src, const char *dest);

/* --- undo (last batch only) --- */
typedef enum {
	UNDO_NONE = 0,
	UNDO_TRASH,
	UNDO_COPY,
	UNDO_MOVE,
	UNDO_RENAME
} undo_kind;

typedef struct {
	undo_kind kind;
	char src[PATH_MAX]; /* original / from */
	char dst[PATH_MAX]; /* trash path, copy/move dest, or new rename path */
	char extra[PATH_MAX]; /* trashinfo path for UNDO_TRASH */
} undo_item;

typedef struct {
	undo_item *items;
	int n;
	int cap;
} undo_batch;

static undo_batch g_undo = {0};

static void undo_clear(void)
{
	free(g_undo.items);
	g_undo.items = NULL;
	g_undo.n = 0;
	g_undo.cap = 0;
}

static void undo_begin_batch(void)
{
	undo_clear();
}

static int undo_push(undo_kind kind, const char *src, const char *dst, const char *extra)
{
	if (g_undo.n >= g_undo.cap) {
		int ncap = g_undo.cap ? g_undo.cap * 2 : 8;
		undo_item *grown = realloc(g_undo.items, (size_t)ncap * sizeof(undo_item));
		if (!grown)
			return -1;
		g_undo.items = grown;
		g_undo.cap = ncap;
	}
	undo_item *it = &g_undo.items[g_undo.n];
	memset(it, 0, sizeof *it);
	it->kind = kind;
	if (src)
		snprintf(it->src, sizeof it->src, "%s", src);
	if (dst)
		snprintf(it->dst, sizeof it->dst, "%s", dst);
	if (extra)
		snprintf(it->extra, sizeof it->extra, "%s", extra);
	g_undo.n++;
	return 0;
}

static int undo_rm_rf(const char *path)
{
	if (!path || !path[0])
		return -1;
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execlp("rm", "rm", "-rf", path, (char *)NULL);
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* Apply last batch in reverse; returns number of items undone. */
static int undo_apply(void)
{
	if (g_undo.n <= 0)
		return 0;
	int ok = 0;
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "undo\n");
	endwin();
	for (int i = g_undo.n - 1; i >= 0; i--) {
		undo_item *it = &g_undo.items[i];
		if (it->kind == UNDO_TRASH) {
			/* dst = trash files path, src = original path */
			if (trash_place_file(it->dst, it->src) == 0) {
				if (it->extra[0])
					unlink(it->extra);
				printf("Restored: %s\n", it->src);
				if (logn < sizeof logbuf - 1)
					logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
					                         "restored: %s\n", it->src);
				ok++;
			} else {
				perror(it->src);
			}
		} else if (it->kind == UNDO_COPY) {
			/* remove destination copy */
			if (undo_rm_rf(it->dst) == 0) {
				printf("Removed copy: %s\n", it->dst);
				if (logn < sizeof logbuf - 1)
					logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
					                         "removed copy: %s\n", it->dst);
				ok++;
			} else {
				perror(it->dst);
			}
		} else if (it->kind == UNDO_MOVE) {
			if (rename(it->dst, it->src) == 0 || trash_place_file(it->dst, it->src) == 0) {
				printf("Moved back: %s -> %s\n", it->dst, it->src);
				if (logn < sizeof logbuf - 1)
					logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
					                         "moved back: %s\n", it->src);
				ok++;
			} else {
				perror(it->src);
			}
		} else if (it->kind == UNDO_RENAME) {
			if (rename(it->dst, it->src) == 0) {
				printf("Renamed back: %s\n", it->src);
				if (logn < sizeof logbuf - 1)
					logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
					                         "renamed back: %s\n", it->src);
				ok++;
			} else {
				perror(it->src);
			}
		}
	}
	refresh();
	undo_clear();
	if (ok)
		op_log_set(logbuf);
	return ok;
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
	undo_begin_batch();
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "copy → %s\n", present_dir);
	endwin();
	while(fgets(buf, PATH_MAX, (FILE*) f)) {
		buf[strlen(buf)-1] = '\0';
		replace(buf,"//","\n");
		const char *base = strrchr(buf, '/');
		base = base ? base + 1 : buf;
		char dest_path[PATH_MAX];
		snprintf(dest_path, sizeof dest_path, "%s/%s", present_dir, base);

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
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
				undo_push(UNDO_COPY, buf, dest_path, NULL);
				if (logn < sizeof logbuf - 1)
					logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
					                         "copied: %s\n", buf);
			} else if (logn < sizeof logbuf - 1) {
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "copy failed: %s\n", buf);
			}
		}
	}
	refresh();
	fclose(f);
	clearClipboard();
	op_log_set(logbuf);
}
void moveFiles(char *present_dir) {
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	char dest_path[PATH_MAX];

	if (f == NULL) {
		perror("Failed to open clipboard file");
		return;
	}

	undo_begin_batch();
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "move → %s\n", present_dir);
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
			if (logn < sizeof logbuf - 1)
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "move failed: %s\n", buf);
		} else {
			printf("Moved: %s -> %s\n", buf, dest_path);
			undo_push(UNDO_MOVE, buf, dest_path, NULL);
			if (logn < sizeof logbuf - 1)
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "moved: %s -> %s\n", buf, dest_path);
		}
	}

	fclose(f);
	refresh();
	clearClipboard();
	op_log_set(logbuf);
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

/* Trash one absolute path into FreeDesktop home trash.
 * On success, optional out_dest/out_info receive trash files path and .trashinfo path. */
static int trash_one(const char *orig_path, char *out_dest, size_t destsz,
                     char *out_info, size_t infosz) {
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
	if (out_dest && destsz)
		snprintf(out_dest, destsz, "%s", dest);
	if (out_info && infosz)
		snprintf(out_info, infosz, "%s", info_path);
	return 0;
}

/* Move clipboard selection into FreeDesktop Trash (not a raw dump folder). */
void trashFiles(void) {
	FILE *f = fopen(clipboard_path, "r");
	if (f == NULL) {
		return;
	}

	if (trash_ensure_home() != 0) {
		fclose(f);
		return;
	}

	undo_begin_batch();
	endwin();
	char buf[PATH_MAX];
	while (fgets(buf, PATH_MAX, f)) {
		size_t L = strlen(buf);
		if (L > 0 && buf[L - 1] == '\n')
			buf[L - 1] = '\0';
		replace(buf, "//", "\n");
		char tdest[PATH_MAX], tinfo[PATH_MAX];
		if (trash_one(buf, tdest, sizeof tdest, tinfo, sizeof tinfo) == 0) {
			printf("Trashed: %s\n", buf);
			undo_push(UNDO_TRASH, buf, tdest, tinfo);
		} else
			perror(buf);
	}
	fclose(f);
	refresh();
	clearClipboard();
}

/* Trash marks or hovered path; records undo. Returns count trashed. */
static int trash_paths_batch(char **paths, int npaths)
{
	if (!paths || npaths <= 0)
		return 0;
	if (trash_ensure_home() != 0)
		return 0;
	undo_begin_batch();
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "trash\n");
	endwin();
	int ok = 0;
	for (int i = 0; i < npaths; i++) {
		if (!paths[i] || !paths[i][0])
			continue;
		char tdest[PATH_MAX], tinfo[PATH_MAX];
		if (trash_one(paths[i], tdest, sizeof tdest, tinfo, sizeof tinfo) == 0) {
			printf("Trashed: %s\n", paths[i]);
			undo_push(UNDO_TRASH, paths[i], tdest, tinfo);
			if (logn < sizeof logbuf - 1)
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "trashed: %s\n", paths[i]);
			ok++;
		} else {
			perror(paths[i]);
			if (logn < sizeof logbuf - 1)
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "trash failed: %s\n", paths[i]);
		}
	}
	refresh();
	if (ok)
		op_log_set(logbuf);
	return ok;
}

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

#define MCFL_CTRL_C 3   /* copy → clipboard + Copy-to dest menu */
#define MCFL_CTRL_X 24  /* move → clipboard + Move-to dest menu */
#define MCFL_CTRL_Q 17  /* quit */
#define MCFL_CTRL_J 10  /* jump-to ($HOME) = Ctrl+J = LF; Enter is KEY_ENTER or CR after nonl() */
#define MCFL_CTRL_A 1   /* select all */
#define MCFL_CTRL_R 18  /* rename */
#define MCFL_CTRL_Z 26  /* undo */
#define MCFL_CTRL_S 19  /* scripts */

/* Synthetic key codes for CSI (st needs config.h ttysend — see README) */
enum {
	MCFL_K_NONE = 0,
	MCFL_K_CTRL_SHIFT_A = 10001,
	MCFL_K_CTRL_DOT = 10002,
};

#define MCFL_ESC_MS 50 /* wait for CSI / Alt+key after Esc */

/* xterm modifyOtherKeys=2: distinct bytes for Ctrl+Shift+A, Ctrl+., etc. */
static void mcfl_term_modkeys_on(void)
{
	if (isatty(STDOUT_FILENO))
		(void)write(STDOUT_FILENO, "\033[>4;2m", 8);
}

void mcfl_term_modkeys_off(void)
{
	if (isatty(STDOUT_FILENO))
		(void)write(STDOUT_FILENO, "\033[>4;0m", 8);
}

/* Backspace only — KEY_DC (Delete) is trash, not filter edit. */
static int mcfl_is_backspace(int kc, wint_t ch)
{
	if (kc == KEY_CODE_YES)
		return (ch == KEY_BACKSPACE || ch == 127 || ch == 8);
	return (ch == 8 || ch == 127 || ch == KEY_BACKSPACE);
}

static int mcfl_is_delete_key(int kc, wint_t ch)
{
	return kc == KEY_CODE_YES && ch == KEY_DC;
}

/*
 * Enter / open:
 *  - KEY_ENTER and → (Browse)
 *  - bare CR (\r): kept for st — Enter and Ctrl+M are the same byte on st
 *    (cannot deaden Ctrl+M without breaking Enter). See README.
 *  - never LF (Ctrl+J).
 */
static int mcfl_is_enter(int kc, wint_t ch)
{
	if (kc == KEY_CODE_YES)
		return (ch == KEY_ENTER || ch == KEY_RIGHT);
	return (ch == '\r');
}

/* Dest menu confirm only — no KEY_RIGHT. */
static int mcfl_is_enter_only(int kc, wint_t ch)
{
	if (kc == KEY_CODE_YES)
		return ch == KEY_ENTER;
	return ch == '\r';
}

static int mcfl_is_escape(int kc, wint_t ch)
{
	(void)kc;
	return ch == 27;
}

static int mcfl_is_key_left(int kc, wint_t ch)
{
	return kc == KEY_CODE_YES && ch == KEY_LEFT;
}

/*
 * Read one logical key from win. Parses xterm-style CSI used when st is
 * configured to ttysend them (stock st folds Ctrl+Shift+A → Ctrl+A).
 *
 * Recognized:
 *   ESC [ 27 ; 6 ; 65 ~  or  97 ~  → Ctrl+Shift+A (xterm / modifyOtherKeys)
 *   ESC [ 27 ; 3 ; 65 ~  or  97 ~  → Alt+A invert (xterm)
 *   ESC [ 27 ; 5 ; 46 ~            → Ctrl+.
 *   ESC [ 65 ; 5 u                 → kitty Ctrl+Shift+A (mod 5 = shift+ctrl)
 *   ESC [ 97 ; 2 u                 → kitty Alt+A (mod 2 = alt)
 *   Esc a / Esc A (no modifyOtherKeys) → Alt+A invert
 * Bare ESC (no following byte within MCFL_ESC_MS) → ch=27.
 *
 * out_syn: MCFL_K_* if synthetic; else 0. out_kc and out_ch are normal keys.
 */
static int mcfl_read_key(WINDOW *win, int *out_kc, wint_t *out_ch, int *out_syn)
{
	*out_syn = MCFL_K_NONE;
	int kc = wget_wch(win, out_ch);
	*out_kc = kc;
	if (kc == ERR)
		return ERR;
	if (kc == KEY_CODE_YES || *out_ch != 27)
		return kc;

	/* ESC — wait briefly for CSI or Alt+key (not nodelay: avoids CSI race) */
	wtimeout(win, MCFL_ESC_MS);
	wint_t ch2 = 0;
	int kc2 = wget_wch(win, &ch2);
	if (kc2 == ERR) {
		wtimeout(win, -1);
		return kc; /* bare Esc */
	}
	if (kc2 == KEY_CODE_YES) {
		wtimeout(win, -1);
		return kc;
	}
	if (ch2 == L'a' || ch2 == L'A') {
		wtimeout(win, -1);
		*out_syn = MCFL_K_CTRL_SHIFT_A; /* Alt+A invert */
		return OK;
	}
	if (ch2 != L'[') {
		wtimeout(win, -1);
		return kc; /* Esc + unknown follower — treat as Esc */
	}

	/* Read CSI parameters until terminator (same short timeout per byte) */
	char csi[64];
	int n = 0;
	for (;;) {
		wint_t c = 0;
		int k = wget_wch(win, &c);
		if (k == ERR)
			break;
		if (k == KEY_CODE_YES)
			break;
		if (n < (int)sizeof(csi) - 1)
			csi[n++] = (char)c;
		if (c >= 0x40 && c <= 0x7E)
			break;
	}
	csi[n] = '\0';
	wtimeout(win, -1);

	/* ESC [ 27 ; <mod> ; <key> ~  (xterm modifyOtherKeys / st ttysend) */
	int p1 = 0, p2 = 0, p3 = 0;
	if (sscanf(csi, "%d;%d;%d~", &p1, &p2, &p3) == 3 && p1 == 27) {
		/* xterm mod: 3=Alt, 5=Ctrl, 6=Ctrl+Shift */
		if ((p2 == 6 || p2 == 3) && (p3 == 65 || p3 == 97)) {
			*out_syn = MCFL_K_CTRL_SHIFT_A;
			return OK;
		}
		if (p2 == 5 && p3 == 46) {
			*out_syn = MCFL_K_CTRL_DOT;
			return OK;
		}
	}
	/* kitty: ESC [ <key> ; <mod> u  (mod: 2=alt, 4=ctrl, 5=ctrl+shift) */
	if (sscanf(csi, "%d;%du", &p1, &p2) == 2) {
		if ((p2 == 5 || p2 == 2) && (p1 == 65 || p1 == 97)) {
			*out_syn = MCFL_K_CTRL_SHIFT_A;
			return OK;
		}
		if (p2 == 4 && p1 == 46) {
			*out_syn = MCFL_K_CTRL_DOT;
			return OK;
		}
	}

	/* Unrecognized CSI after ESC — treat as Esc */
	*out_ch = 27;
	*out_kc = OK;
	return OK;
}

/* Ctrl+Q: exit without shell cd-on-quit (see cfl_cd_on_quit / WrappeUp). */
static void mcfl_quit_fast(void)
{
	cfl_cd_on_quit = 0;
}

static void marks_select_all(unsigned char *marked, int n)
{
	if (!marked || n <= 0)
		return;
	for (int i = 0; i < n; i++)
		marked[i] = 1;
}

static void marks_invert(unsigned char *marked, int n)
{
	if (!marked || n <= 0)
		return;
	for (int i = 0; i < n; i++)
		marked[i] = marked[i] ? 0 : 1;
}

/* Collect abs paths from marks or focus. Caller frees each string and the array.
 * If base_dir is non-NULL, paths[i] are basenames joined with base_dir.
 * If base_dir is NULL, paths[i] are already absolute. */
static int collect_paths(char **paths, int n, const unsigned char *marked,
                         search_filt *filt, int sel, const char *base_dir,
                         char ***out_paths)
{
	if (!out_paths)
		return 0;
	*out_paths = NULL;
	int any = search_count_marked(marked, n) > 0;
	char **list = NULL;
	int count = 0, cap = 0;

	if (any) {
		for (int i = 0; i < n; i++) {
			if (!marked[i] || !paths[i])
				continue;
			if (base_dir && (strcmp(paths[i], ".") == 0 || strcmp(paths[i], "..") == 0))
				continue;
			char path[PATH_MAX];
			if (base_dir) {
				if (build_path(path, sizeof path, base_dir, paths[i]) != 0)
					continue;
			} else {
				snprintf(path, sizeof path, "%s", paths[i]);
			}
			if (count >= cap) {
				int ncap = cap ? cap * 2 : 8;
				char **g = realloc(list, (size_t)ncap * sizeof(char *));
				if (!g)
					break;
				list = g;
				cap = ncap;
			}
			list[count] = strdup(path);
			if (list[count])
				count++;
		}
	} else if (filt && filt->count > 0 && sel >= 0 && sel < filt->count) {
		int oidx = filt->indices[sel];
		if (oidx >= 0 && oidx < n && paths[oidx]) {
			if (base_dir && (strcmp(paths[oidx], ".") == 0 || strcmp(paths[oidx], "..") == 0))
				goto done;
			char path[PATH_MAX];
			if (base_dir) {
				if (build_path(path, sizeof path, base_dir, paths[oidx]) != 0)
					goto done;
			} else {
				snprintf(path, sizeof path, "%s", paths[oidx]);
			}
			list = malloc(sizeof(char *));
			if (list) {
				list[0] = strdup(path);
				if (list[0])
					count = 1;
			}
		}
	}
done:
	*out_paths = list;
	return count;
}

static void free_path_list(char **paths, int n)
{
	if (!paths)
		return;
	for (int i = 0; i < n; i++)
		free(paths[i]);
	free(paths);
}

/* Status-line rename of hovered basename. Returns 1 if renamed. */
static int browse_rename_hover(char **files, int n, search_filt *filt, int sel)
{
	if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count)
		return 0;
	int oidx = filt->indices[sel];
	if (oidx < 0 || oidx >= n || !files[oidx])
		return 0;
	if (strcmp(files[oidx], ".") == 0 || strcmp(files[oidx], "..") == 0)
		return 0;

	char oldpath[PATH_MAX];
	if (build_path(oldpath, sizeof oldpath, dir, files[oidx]) != 0)
		return 0;

	wchar_t input[JUMP_MAX_INPUT + 1];
	/* Prefill basename as wide string */
	{
		const char *mb = files[oidx];
		size_t nconv = mbstowcs(input, mb, JUMP_MAX_INPUT);
		if (nconv == (size_t)-1)
			input[0] = L'\0';
		else
			input[nconv < JUMP_MAX_INPUT ? nconv : JUMP_MAX_INPUT] = L'\0';
	}
	int input_len = (int)wcslen(input);

	for (;;) {
		if (status_win) {
			char *qmb = search_wc_to_mb(input);
			wclear(status_win);
			wmove(status_win, 0, 1);
			wattron(status_win, A_BOLD);
			wprintw(status_win, "Rename > ");
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
			free(qmb);
		}

		wint_t ch;
		int kc = wget_wch(status_win, &ch);
		if (kc == ERR)
			continue;
		if (kc == KEY_CODE_YES && ch == KEY_RESIZE) {
			handleResize();
			continue;
		}
		if (mcfl_is_escape(kc, ch))
			return 0;
		if (mcfl_is_enter_only(kc, ch)) {
			char *newbase = search_wc_to_mb(input);
			if (!newbase || !newbase[0] || strchr(newbase, '/')) {
				free(newbase);
				return 0;
			}
			if (strcmp(newbase, files[oidx]) == 0) {
				free(newbase);
				return 0;
			}
			char newpath[PATH_MAX];
			if (build_path(newpath, sizeof newpath, dir, newbase) != 0) {
				free(newbase);
				return 0;
			}
			free(newbase);
			if (rename(oldpath, newpath) != 0)
				return 0;
			undo_begin_batch();
			undo_push(UNDO_RENAME, oldpath, newpath, NULL);
			{
				char logb[PATH_MAX * 2 + 64];
				snprintf(logb, sizeof logb, "rename\n%s -> %s\n", oldpath, newpath);
				op_log_set(logb);
			}
			return 1;
		}
		if (mcfl_is_backspace(kc, ch)) {
			if (input_len > 0) {
				input[--input_len] = L'\0';
			}
			continue;
		}
		if (kc != KEY_CODE_YES && iswprint(ch) && ch != L'/' && input_len < JUMP_MAX_INPUT) {
			input[input_len++] = (wchar_t)ch;
			input[input_len] = L'\0';
		}
	}
}

/* Multi-mark bulk rename via $EDITOR/vim on a temp name list. Returns # renamed. */
static int browse_bulk_rename(char **files, int n, const unsigned char *marked)
{
	int count = 0;
	for (int i = 0; i < n; i++)
		if (marked && marked[i] && files[i])
			count++;
	if (count < 2)
		return 0;

	char tmp_path[PATH_MAX];
	snprintf(tmp_path, sizeof tmp_path, "%s/cfl-rename.%d",
	         cache_path ? cache_path : "/tmp", (int)getpid());
	FILE *tf = fopen(tmp_path, "w");
	if (!tf)
		return 0;

	/* old basenames, one per line — order = mark scan order */
	char **olds = calloc((size_t)count, sizeof(char *));
	if (!olds) {
		fclose(tf);
		unlink(tmp_path);
		return 0;
	}
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (!marked[i] || !files[i])
			continue;
		olds[k] = files[i];
		fprintf(tf, "%s\n", files[i]);
		k++;
	}
	fclose(tf);

	const char *ed = editor && editor[0] ? editor : "vim";
	endwin();
	pid_t pid = fork();
	if (pid == 0) {
		execlp(ed, ed, tmp_path, (char *)NULL);
		_exit(127);
	}
	int status = 0;
	if (pid > 0)
		waitpid(pid, &status, 0);
	refresh();
	handleResize();

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		unlink(tmp_path);
		free(olds);
		return 0;
	}

	tf = fopen(tmp_path, "r");
	if (!tf) {
		unlink(tmp_path);
		free(olds);
		return 0;
	}

	undo_begin_batch();
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "bulk rename\n");
	char line[NAME_MAX + 8];
	int renamed = 0;
	for (int i = 0; i < count; i++) {
		if (!fgets(line, sizeof line, tf))
			break;
		size_t L = strlen(line);
		if (L > 0 && line[L - 1] == '\n')
			line[L - 1] = '\0';
		if (!line[0] || strchr(line, '/'))
			continue;
		if (strcmp(line, olds[i]) == 0)
			continue;
		char op[PATH_MAX], np[PATH_MAX];
		if (build_path(op, sizeof op, dir, olds[i]) != 0)
			continue;
		if (build_path(np, sizeof np, dir, line) != 0)
			continue;
		if (rename(op, np) == 0) {
			undo_push(UNDO_RENAME, op, np, NULL);
			if (logn < sizeof logbuf - 1)
				logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn,
				                         "%s -> %s\n", olds[i], line);
			renamed++;
		}
	}
	fclose(tf);
	unlink(tmp_path);
	free(olds);
	if (renamed)
		op_log_set(logbuf);
	return renamed;
}

/* Script dir: $XDG_CONFIG_HOME/cfl/scripts or ~/.config/cfl/scripts */
static void scripts_dir(char *out, size_t outsz)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0])
		snprintf(out, outsz, "%s/cfl/scripts", xdg);
	else if (info && info->pw_dir)
		snprintf(out, outsz, "%s/.config/cfl/scripts", info->pw_dir);
	else
		snprintf(out, outsz, "/tmp/cfl-scripts");
}

/* Pick and run a script on marks/hover. Returns 1 if a script ran. */
static int browse_run_script(char **files, int n, const unsigned char *marked,
                             search_filt *filt, int sel)
{
	char sdir[PATH_MAX];
	scripts_dir(sdir, sizeof sdir);
	DIR *d = opendir(sdir);
	if (!d) {
		op_log_set("no scripts dir\n(create ~/.config/cfl/scripts)\n");
		return 0;
	}

	char **names = NULL;
	int nnames = 0, cap = 0;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char full[PATH_MAX];
		if (snprintf(full, sizeof full, "%s/%s", sdir, ent->d_name) >= (int)sizeof full)
			continue;
		struct stat st;
		if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (access(full, X_OK) != 0)
			continue;
		if (nnames >= cap) {
			int ncap = cap ? cap * 2 : 8;
			char **g = realloc(names, (size_t)ncap * sizeof(char *));
			if (!g)
				break;
			names = g;
			cap = ncap;
		}
		names[nnames] = strdup(ent->d_name);
		if (names[nnames])
			nnames++;
	}
	closedir(d);
	if (nnames <= 0) {
		op_log_set("no executable scripts in scripts dir\n");
		free(names);
		return 0;
	}

	/* simple picker: first script for now if only one; else filter list in status */
	int pick = 0;
	if (nnames > 1) {
		/* status-line pick with j/k or arrows, Enter confirm */
		int done = 0;
		while (!done) {
			if (status_win) {
				wclear(status_win);
				wmove(status_win, 0, 1);
				wattron(status_win, A_BOLD);
				wprintw(status_win, "Script > ");
				wattroff(status_win, A_BOLD);
				wprintw(status_win, "%s  (%d/%d)", names[pick], pick + 1, nnames);
				wrefresh(status_win);
			}
			if (preview_win) {
				wclear(preview_win);
				for (int i = 0; i < nnames && i < 20; i++) {
					if (i == pick)
						wattron(preview_win, A_REVERSE);
					mvwprintw(preview_win, i, 0, "%s", names[i]);
					wattroff(preview_win, A_REVERSE);
				}
				wrefresh(preview_win);
			}
			wint_t ch = 0;
			int kc = wget_wch(status_win, &ch);
			if (kc == ERR)
				continue;
			if (mcfl_is_escape(kc, ch)) {
				for (int i = 0; i < nnames; i++)
					free(names[i]);
				free(names);
				return 0;
			}
			if (mcfl_is_enter_only(kc, ch) || (kc != KEY_CODE_YES && ch == '\r')) {
				done = 1;
				break;
			}
			if (kc == KEY_CODE_YES && ch == KEY_UP && pick > 0)
				pick--;
			else if (kc == KEY_CODE_YES && ch == KEY_DOWN && pick < nnames - 1)
				pick++;
		}
	}

	char **plist = NULL;
	int pn = collect_paths(files, n, marked, filt, sel, dir, &plist);
	if (pn <= 0) {
		for (int i = 0; i < nnames; i++)
			free(names[i]);
		free(names);
		op_log_set("script: no files selected\n");
		return 0;
	}

	char script_path[PATH_MAX];
	snprintf(script_path, sizeof script_path, "%s/%s", sdir, names[pick]);

	/* argv: script + paths + NULL */
	char **argv = calloc((size_t)pn + 2, sizeof(char *));
	if (!argv) {
		free_path_list(plist, pn);
		for (int i = 0; i < nnames; i++)
			free(names[i]);
		free(names);
		return 0;
	}
	argv[0] = script_path;
	for (int i = 0; i < pn; i++)
		argv[i + 1] = plist[i];
	argv[pn + 1] = NULL;

	int pipefd[2];
	char logbuf[OP_LOG_MAX];
	size_t logn = 0;
	logn += (size_t)snprintf(logbuf + logn, sizeof logbuf - logn, "script %s\n", names[pick]);
	if (pipe(pipefd) != 0) {
		free(argv);
		free_path_list(plist, pn);
		for (int i = 0; i < nnames; i++)
			free(names[i]);
		free(names);
		return 0;
	}
	endwin();
	pid_t pid = fork();
	if (pid == 0) {
		dup2(pipefd[1], 1);
		dup2(pipefd[1], 2);
		close(pipefd[0]);
		close(pipefd[1]);
		execv(script_path, argv);
		_exit(127);
	}
	close(pipefd[1]);
	char rbuf[512];
	ssize_t nr;
	while ((nr = read(pipefd[0], rbuf, sizeof rbuf)) > 0) {
		if (logn + (size_t)nr < sizeof logbuf) {
			memcpy(logbuf + logn, rbuf, (size_t)nr);
			logn += (size_t)nr;
			logbuf[logn] = '\0';
		}
	}
	close(pipefd[0]);
	int st = 0;
	if (pid > 0)
		waitpid(pid, &st, 0);
	refresh();
	handleResize();
	op_log_set(logbuf);

	free(argv);
	free_path_list(plist, pn);
	for (int i = 0; i < nnames; i++)
		free(names[i]);
	free(names);
	return 1;
}

static void mcfl_draw_help(void)
{
	ui_sticky_draw d;
	if (!ui_sticky_begin(&d))
		return;
	ui_sticky_title(&d, "Help (F1 / Esc close)");
	ui_sticky_puts(&d, "");
	ui_sticky_puts(&d, "Arrows  move / parent / open");
	ui_sticky_puts(&d, "Enter   open file or directory");
	ui_sticky_puts(&d, "Tab     multi-mark");
	ui_sticky_puts(&d, "type    filter list");
	ui_sticky_puts(&d, "Del     trash marks or hover");
	ui_sticky_puts(&d, "Ctrl+C  copy-to dest menu");
	ui_sticky_puts(&d, "Ctrl+X  move-to dest menu");
	ui_sticky_puts(&d, "Ctrl+R  rename (bulk=vim if multi-mark)");
	ui_sticky_puts(&d, "Ctrl+Z  undo last trash/copy/move/rename");
	ui_sticky_puts(&d, "Ctrl+A  select all");
	ui_sticky_puts(&d, "C-S-A   invert selection (Alt+A fallback)");
	ui_sticky_puts(&d, "Ctrl+.  toggle hidden (st: README)");
	ui_sticky_puts(&d, "Ctrl+S  run script on marks/hover");
	ui_sticky_puts(&d, "Ctrl+J  jump under $HOME");
	ui_sticky_puts(&d, "Esc     close mode; Browse: quit (+cd)");
	ui_sticky_puts(&d, "Ctrl+Q  quit immediately (no cd)");
	ui_sticky_puts(&d, "st: Ctrl+M=Enter, Ctrl+[=Esc");
	ui_sticky_footer(&d, "Esc/F1 close", "Esc/F1 close");
	ui_sticky_end(&d);
}

/* Move selection to parent of paths[filt.indices[sel]] if that parent is listed. */
static int mcfl_sel_to_parent_path(char **paths, int npaths, search_filt *filt, int *sel)
{
	if (!paths || !filt || !sel || filt->count <= 0)
		return 0;
	if (*sel < 0 || *sel >= filt->count)
		return 0;
	int oidx = filt->indices[*sel];
	if (oidx < 0 || oidx >= npaths || !paths[oidx])
		return 0;

	char parent[PATH_MAX];
	snprintf(parent, sizeof parent, "%s", paths[oidx]);
	getParentPath(parent);
	if (strcmp(parent, paths[oidx]) == 0)
		return 0;

	for (int i = 0; i < filt->count; i++) {
		int j = filt->indices[i];
		if (j >= 0 && j < npaths && paths[j] && strcmp(paths[j], parent) == 0) {
			if (i == *sel)
				return 0;
			*sel = i;
			return 1;
		}
	}
	return 0;
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
                wattron(current_win, COLOR_PAIR(4));
            if (fidx == sel) {
                if (has_colors()) {
                    if (!is_marked)
                        wattron(current_win, COLOR_PAIR(4));
                    wattron(current_win, A_BOLD);
                } else {
                    wattron(current_win, A_REVERSE | A_BOLD);
                }
            }

            char entry_display[PATH_MAX + 8];
            snprintf(entry_display, sizeof entry_display, "%s", name);
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
 * Returns 1 if move/copy ran, 0 if cancelled / failed to start, -1 if Ctrl+Q quit. */
static int show_dir_pick_dest(dir_pick_mode mode)
{
    if (mode != DIR_PICK_MOVE && mode != DIR_PICK_COPY)
        return 0;
    if (!current_win || !preview_win || !status_win) {
        return 0;
    }
    if (access(clipboard_path, F_OK) != 0 || selectedFiles <= 0) {
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
        if (ui_sticky_is(UI_STICKY_HELP)) {
            mcfl_draw_help();
        } else if (cur_oidx != last_preview_oidx) {
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

        int want_quit = 0;

        if (kc == KEY_CODE_YES && ch == KEY_RESIZE) {
            handleResize();
            last_preview_oidx = -2;
            continue;
        }

        if (kc == KEY_CODE_YES && ch == KEY_F(1)) {
            if (ui_sticky_is(UI_STICKY_HELP)) {
                ui_sticky_close();
                last_preview_oidx = -2;
            } else {
                ui_sticky_open(UI_STICKY_HELP, 1);
            }
            continue;
        }
        if (ui_sticky_is(UI_STICKY_HELP)) {
            if (mcfl_is_escape(kc, ch)) {
                ui_sticky_close();
                last_preview_oidx = -2;
            }
            continue;
        }

        if (kc == KEY_CODE_YES && ch == KEY_UP && sel > 0) {
            sel--;
            need_preview = 1;
        } else if (kc == KEY_CODE_YES && ch == KEY_DOWN && sel < filt.count - 1) {
            sel++;
            need_preview = 1;
        } else if (mcfl_is_enter_only(kc, ch)) {
            /* Enter only finishes transfer; → does nothing here */
            if (filt.count > 0) { confirmed = 1; done = 1; }
        } else if (mcfl_is_key_left(kc, ch)) {
            if (mcfl_sel_to_parent_path(jl.paths, jl.count, &filt, &sel))
                need_preview = 1;
        } else if (mcfl_is_escape(kc, ch)) {
            /* close dest mode; keep cwd */
            done = 1;
            confirmed = 0;
        } else if (mcfl_is_backspace(kc, ch)) {
            mcfl_input_backspace(input, &input_len, &need_filter);
        } else if (kc == KEY_CODE_YES && ch == KEY_PPAGE) {
            sel -= visible; if (sel < 0) sel = 0; need_preview = 1;
        } else if (kc == KEY_CODE_YES && ch == KEY_NPAGE) {
            sel += visible;
            if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
            need_preview = 1;
        } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_Q) {
            mcfl_quit_fast();
            want_quit = 1;
            done = 1;
            confirmed = 0;
        } else if (kc != KEY_CODE_YES && iswprint(ch) && input_len < JUMP_MAX_INPUT) {
            input[input_len++] = (wchar_t)ch;
            input[input_len] = L'\0';
            need_filter = 1;
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

        if (want_quit) {
            curs_set(0);
            leaveok(current_win, FALSE);
            leaveok(preview_win, FALSE);
            search_free_listing(&preview_listing, &preview_n);
            search_filt_free(&filt);
            jump_list_free(&jl);
            ueberzugpp_remove();
            clearFlagImg = 1;
            return -1;
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
            if (ui_sticky_is(UI_STICKY_HELP)) {
                mcfl_draw_help();
            } else if (op_log_active) {
                op_log_draw();
            } else if (cur != last_info || nmarked != last_nm) {
                search_draw_preview(files, &filt, sel, &preview_listing, &preview_n);
                last_info = cur;
                last_nm = nmarked;
            }

            char *qmb = search_wc_to_mb(input);
            if (status_win) {
                wclear(status_win);
                wmove(status_win, 0, 1);
                wattron(status_win, A_BOLD);
                if (ui_sticky_is(UI_STICKY_HELP))
                    wprintw(status_win, "Help > ");
                else
                    wprintw(status_win, "Browse > ");
                wattroff(status_win, A_BOLD);
                if (!ui_sticky_is(UI_STICKY_HELP))
                    wprintw(status_win, "%s", qmb ? qmb : "");
                else
                    wprintw(status_win, "F1/Esc close");
                int sc = getmaxx(status_win);
                int qlen = qmb ? (int)strlen(qmb) : 0;
                int cc = 1 + 9 + (ui_sticky_is(UI_STICKY_HELP) ? 12 : qlen);
                if (cc >= sc - 1) cc = sc - 2;
                if (cc < 1) cc = 1;
                wmove(status_win, 0, cc);
                curs_set(ui_sticky_is(UI_STICKY_HELP) ? 0 : 1);
                wrefresh(status_win);
            }
            free(qmb);

            wint_t ch = 0;
            int kc = 0, syn = MCFL_K_NONE;
            if (mcfl_read_key(status_win, &kc, &ch, &syn) == ERR)
                continue;
            int need_filter = 0, need_prev = 0;

            if (kc == KEY_CODE_YES && ch == KEY_RESIZE) {
                handleResize();
                last_info = -2;
                continue;
            }

            /* F1: toggle help (overrides other ops while open) */
            if (kc == KEY_CODE_YES && ch == KEY_F(1)) {
                if (ui_sticky_is(UI_STICKY_HELP)) {
                    ui_sticky_close();
                    last_info = -2;
                } else {
                    ui_sticky_open(UI_STICKY_HELP, 1);
                }
                continue;
            }

            if (ui_sticky_is(UI_STICKY_HELP)) {
                if (mcfl_is_escape(kc, ch) && syn == MCFL_K_NONE) {
                    ui_sticky_close();
                    last_info = -2;
                }
                /* ignore all other keys while help is open */
                continue;
            }

            if (syn == MCFL_K_CTRL_SHIFT_A) {
                marks_invert(marked, n);
                need_prev = 1;
            } else if (syn == MCFL_K_CTRL_DOT) {
                hiddenFlag = hiddenFlag ? 0 : 1;
                leave = 1;
                result = MCFL_OK;
                continue;
            } else if (kc == KEY_CODE_YES && ch == KEY_UP && sel > 0) {
                sel--;
                need_prev = 1;
                op_log_clear();
            } else if (kc == KEY_CODE_YES && ch == KEY_DOWN && sel < filt.count - 1) {
                sel++;
                need_prev = 1;
                op_log_clear();
            } else if (mcfl_is_enter(kc, ch)) {
                /* Enter / → : open dir or file */
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
            } else if (mcfl_is_key_left(kc, ch)) {
                /* ← only: parent directory */
                char prev[PATH_MAX];
                snprintf(prev, sizeof prev, "%s", dir);
                getParentPath(dir);
                if (strcmp(prev, dir) != 0) {
                    leave = 1;
                    result = MCFL_OK;
                }
            } else if (mcfl_is_escape(kc, ch)) {
                /* Esc on Browse: quit cfl with cd-on-quit (save lastdir) */
                leave = 1;
                result = MCFL_QUIT;
            } else if (mcfl_is_delete_key(kc, ch)) {
                char **plist = NULL;
                int pn = collect_paths(files, n, marked, &filt, sel, dir, &plist);
                if (pn > 0 && trash_paths_batch(plist, pn) > 0) {
                    leave = 1;
                    result = MCFL_OK;
                }
                free_path_list(plist, pn);
                handleResize();
                last_info = -2;
            } else if (mcfl_is_backspace(kc, ch)) {
                mcfl_input_backspace(input, &input_len, &need_filter);
            } else if (kc == KEY_CODE_YES && ch == KEY_PPAGE) {
                sel -= visible; if (sel < 0) sel = 0; need_prev = 1; op_log_clear();
            } else if (kc == KEY_CODE_YES && ch == KEY_NPAGE) {
                sel += visible;
                if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
                need_prev = 1;
                op_log_clear();
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_Q) {
                mcfl_quit_fast();
                leave = 1;
                result = MCFL_QUIT;
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_Z) {
                if (undo_apply() > 0) {
                    leave = 1;
                    result = MCFL_OK;
                }
                handleResize();
                last_info = -2;
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_R) {
                int nm = search_count_marked(marked, n);
                if (nm >= 2) {
                    if (browse_bulk_rename(files, n, marked) > 0) {
                        leave = 1;
                        result = MCFL_OK;
                    }
                } else if (browse_rename_hover(files, n, &filt, sel)) {
                    leave = 1;
                    result = MCFL_OK;
                }
                last_info = -2;
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_A) {
                marks_select_all(marked, n);
                need_prev = 1;
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_S) {
                if (browse_run_script(files, n, marked, &filt, sel)) {
                    leave = 1;
                    result = MCFL_OK;
                }
                last_info = -2;
            } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_J) {
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
            } else if (kc != KEY_CODE_YES && (ch == MCFL_CTRL_C || ch == MCFL_CTRL_X)) {
                if (clipboard_from_rel_marks(files, n, marked, &filt, sel) > 0) {
                    int dr = show_dir_pick_dest(ch == MCFL_CTRL_C ? DIR_PICK_COPY : DIR_PICK_MOVE);
                    if (dr < 0) {
                        leave = 1;
                        result = MCFL_QUIT;
                    } else if (dr > 0) {
                        /* Rescan cwd so moved/copied-away items disappear / order resets */
                        leave = 1;
                        result = MCFL_OK;
                    } else {
                        last_info = -2;
                    }
                } else {
                    last_info = -2;
                }
            } else if (kc != KEY_CODE_YES && ch == '\t') {
                if (filt.count > 0) {
                    int oidx = filt.indices[sel];
                    if (oidx >= 0 && oidx < n) {
                        marked[oidx] = marked[oidx] ? 0 : 1;
                        if (sel < filt.count - 1) sel++;
                        need_prev = 1;
                    }
                }
            } else if (kc != KEY_CODE_YES && iswprint(ch) && ch != L'\t' && input_len < JUMP_MAX_INPUT) {
                input[input_len++] = (wchar_t)ch;
                input[input_len] = L'\0';
                need_filter = 1;
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

        if (ui_sticky_is(UI_STICKY_HELP))
            ui_sticky_close();
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
        return MCFL_BACK;
    }

    unsigned char *marked = calloc((size_t)jl.count, 1);
    if (!marked) {
        jump_list_free(&jl);
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
        if (ui_sticky_is(UI_STICKY_HELP)) {
            mcfl_draw_help();
        } else if (op_log_active) {
            op_log_draw();
        } else if (cur_oidx != last_preview_oidx) {
            search_draw_preview_paths(jl.paths, &filt, sel, &preview_listing, &preview_n);
            last_preview_oidx = cur_oidx;
        }
        char *qmb = search_wc_to_mb(input);
        jump_draw_status(DIR_PICK_JUMP, qmb ? qmb : "", sel, filt.count, jl.count, nmarked);
        free(qmb);

        wint_t ch = 0;
        int kc = 0, syn = MCFL_K_NONE;
        if (mcfl_read_key(status_win, &kc, &ch, &syn) == ERR)
            continue;
        int need_filter = 0, need_preview = 0;

        if (kc == KEY_CODE_YES && ch == KEY_RESIZE) {
            handleResize();
            last_preview_oidx = -2;
            continue;
        }

        if (kc == KEY_CODE_YES && ch == KEY_F(1)) {
            if (ui_sticky_is(UI_STICKY_HELP)) {
                ui_sticky_close();
                last_preview_oidx = -2;
            } else {
                ui_sticky_open(UI_STICKY_HELP, 1);
            }
            continue;
        }
        if (ui_sticky_is(UI_STICKY_HELP)) {
            if (mcfl_is_escape(kc, ch) && syn == MCFL_K_NONE) {
                ui_sticky_close();
                last_preview_oidx = -2;
            }
            continue;
        }

        if (syn == MCFL_K_CTRL_SHIFT_A) {
            marks_invert(marked, jl.count);
            need_preview = 1;
        } else if (kc == KEY_CODE_YES && ch == KEY_UP && sel > 0) {
            sel--;
            need_preview = 1;
            op_log_clear();
        } else if (kc == KEY_CODE_YES && ch == KEY_DOWN && sel < filt.count - 1) {
            sel++;
            need_preview = 1;
            op_log_clear();
        } else if (mcfl_is_enter(kc, ch)) {
            /* Enter / → : jump into selected dir → Browse */
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
        } else if (mcfl_is_key_left(kc, ch)) {
            /* ← : parent row in list (does not close Jump) */
            if (mcfl_sel_to_parent_path(jl.paths, jl.count, &filt, &sel))
                need_preview = 1;
        } else if (mcfl_is_escape(kc, ch)) {
            /* Esc: close Jump mode; keep previous dir */
            done = 1;
            result = MCFL_BACK;
        } else if (mcfl_is_delete_key(kc, ch)) {
            char **plist = NULL;
            int pn = collect_paths(jl.paths, jl.count, marked, &filt, sel, NULL, &plist);
            if (pn > 0) {
                trash_paths_batch(plist, pn);
                /* Jump list is stale after trash; leave to Browse */
                done = 1;
                result = MCFL_OK;
            }
            free_path_list(plist, pn);
            handleResize();
            last_preview_oidx = -2;
        } else if (mcfl_is_backspace(kc, ch)) {
            mcfl_input_backspace(input, &input_len, &need_filter);
        } else if (kc == KEY_CODE_YES && ch == KEY_PPAGE) {
            sel -= visible; if (sel < 0) sel = 0; need_preview = 1; op_log_clear();
        } else if (kc == KEY_CODE_YES && ch == KEY_NPAGE) {
            sel += visible;
            if (sel >= filt.count) sel = filt.count > 0 ? filt.count - 1 : 0;
            need_preview = 1;
            op_log_clear();
        } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_Q) {
            mcfl_quit_fast();
            done = 1;
            result = MCFL_QUIT;
        } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_Z) {
            undo_apply();
            handleResize();
            last_preview_oidx = -2;
        } else if (kc != KEY_CODE_YES && ch == MCFL_CTRL_A) {
            marks_select_all(marked, jl.count);
            need_preview = 1;
        } else if (kc != KEY_CODE_YES && (ch == MCFL_CTRL_C || ch == MCFL_CTRL_X)) {
            if (clipboard_from_abs_marks(jl.paths, jl.count, marked, &filt, sel) > 0) {
                int dr = show_dir_pick_dest(ch == MCFL_CTRL_C ? DIR_PICK_COPY : DIR_PICK_MOVE);
                if (dr < 0) {
                    /* dest already called mcfl_quit_fast() */
                    done = 1;
                    result = MCFL_QUIT;
                }
            }
            last_preview_oidx = -2;
        } else if (kc != KEY_CODE_YES && ch == '\t') {
            if (filt.count > 0) {
                int oidx = filt.indices[sel];
                if (oidx >= 0 && oidx < jl.count) {
                    marked[oidx] = marked[oidx] ? 0 : 1;
                    if (sel < filt.count - 1) sel++;
                    need_preview = 1;
                }
            }
        } else if (kc != KEY_CODE_YES && iswprint(ch) && ch != L'\t' && input_len < JUMP_MAX_INPUT) {
            input[input_len++] = (wchar_t)ch;
            input[input_len] = L'\0';
            need_filter = 1;
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
	mcfl_term_modkeys_on();
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
