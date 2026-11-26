/* Search/jump/delete-search read Unicode queries via wget_wch + wchar_t
 * (KEY_CODE_YES, iswprint, search_wc_to_mb). Enable wide ncurses before the
 * header; prefer ncursesw/ so we do not pick up the narrow curses.h. */
#ifndef NCURSES_WIDECHAR
#define NCURSES_WIDECHAR 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__has_include) && __has_include(<ncursesw/ncurses.h>)
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif
#include <wchar.h>
#include <wctype.h>
#include <pwd.h>
#include <dirent.h>
#include <signal.h>
#include <linux/limits.h>
#include <sys/ioctl.h>
#include <magic.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#include "config.h"
#include "ui.h"

sigset_t x;
int raised_signal = -1;
int len_bookmarks = 0;
int len_scripts = 0;
int i = 0;
int allocSize;

char next_dir  [PATH_MAX];
char prev_dir  [PATH_MAX];
char temp_dir  [PATH_MAX];
char sort_dir  [PATH_MAX];

char* editor = NULL;
char* shell = NULL;
char* pch = NULL;
struct passwd *info;
char* cache_path = NULL;
char* clipboard_path = NULL;
char* temp_clipboard_path = NULL;
char* trash_path = NULL;
int clearFlag = 0;
int searchFlag = 0;
int backFlag = 0;
int hiddenFlag = SHOW_HIDDEN;
char* last = NULL;
bool Running = true;
char secondKey;
char *buf;
char *path;
FILE *fp;

static int build_path(char *dst, size_t dstsz, const char *base, const char *entry) {
    /* Avoid "//" when base is root. Returns 0 on success, -1 if truncated/error. */
    int n;
    if (base != NULL && base[0] == '/' && base[1] == '\0') {
        n = snprintf(dst, dstsz, "%s%s", base, entry);
    } else {
        n = snprintf(dst, dstsz, "%s/%s", base, entry);
    }
    return (n < 0 || (size_t)n >= dstsz) ? -1 : 0;
}

// Ueberzug++ helpers (declared here because functions.h is included before their definitions)
void openFile(char *filepath);
void getLastToken(char *tokenizer);
void displayAlert(char *message);
int isRegularFile(const char *path);

#include "bookmark.h"
#include "preview.h"

/* Forward decls needed by search.h / delete_search.h / jump_to.h (implemented below) */
int getFiles(char *directory, char ***target);
int compare(const void *a, const void *b);
int getWritePermissions(char *path);
void moveFiles(char *present_dir);
void copyFiles(char *present_dir);
void trashFiles(void);
void tab_boot(void);

/* One directory snapshot. not_reg matches the old compare() rule
 * (isRegularFile(path) == 0): real directories, symlinks to directories,
 * and anything stat() does not call a regular file sort first. */
struct cfl_namechunk {
	struct cfl_namechunk *next;
	size_t used;
	size_t cap;
	char data[];
};

struct cfl_listing {
	char **names;
	unsigned char *not_reg;
	unsigned char *is_link;
	int n;
	struct cfl_namechunk *names_arena;
};

struct cfl_list_cache {
	int valid;
	char dir[PATH_MAX];
	int hidden;
	dev_t dev;
	ino_t ino;
	struct timespec mtim;
	struct cfl_listing list;
};

static struct cfl_list_cache cache_cwd;
static struct cfl_list_cache cache_preview;
static int listing_cached(struct cfl_list_cache *cache, const char *directory);

#include "search.h"
#include "delete_search.h"
#include "jump_to.h"

struct cfl_ent {
	char *name;
	unsigned char not_reg;
	unsigned char is_link;
};

static void listing_free(struct cfl_listing *listing) {
	struct cfl_namechunk *chunk;
	if (!listing)
		return;
	/* names[] point into names_arena; one free per chunk, not per file. */
	free(listing->names);
	free(listing->not_reg);
	free(listing->is_link);
	chunk = listing->names_arena;
	while (chunk) {
		struct cfl_namechunk *next = chunk->next;
		free(chunk);
		chunk = next;
	}
	memset(listing, 0, sizeof *listing);
}

/* Copy a filename into a chunk that never moves, so qsort can keep pointers. */
static char *listing_store_name(struct cfl_namechunk **chunks, const char *s) {
	size_t len = strlen(s) + 1;
	struct cfl_namechunk *chunk = *chunks;
	char *dst;

	if (chunk == NULL || chunk->used + len > chunk->cap) {
		size_t cap = (size_t)1 << 20;
		if (len > cap)
			cap = len;
		chunk = malloc(sizeof(*chunk) + cap);
		if (chunk == NULL)
			return NULL;
		chunk->next = *chunks;
		chunk->used = 0;
		chunk->cap = cap;
		*chunks = chunk;
	}
	dst = chunk->data + chunk->used;
	memcpy(dst, s, len);
	chunk->used += len;
	return dst;
}

static int cfl_ent_cmp(const void *a, const void *b) {
	const struct cfl_ent *ea = a;
	const struct cfl_ent *eb = b;
	if (ea->not_reg != eb->not_reg)
		return (int)eb->not_reg - (int)ea->not_reg;
	return strcasecmp(ea->name, eb->name);
}

/* d_type when the filesystem provides it; otherwise one stat(), same answer
 * compare() used to recompute on every qsort comparison. */
static void classify_entry(const char *directory, const char *name, unsigned char d_type,
                           unsigned char *not_reg, unsigned char *is_link) {
	char path[PATH_MAX];
	struct stat st;

	*not_reg = 1;
	*is_link = 0;
	/* Regular files and directories need no stat and no path build.
	 * Only symlinks (follow the target) and DT_UNKNOWN hit the disk. */
	if (d_type == DT_REG) {
		*not_reg = 0;
		return;
	}
	if (d_type == DT_DIR)
		return;
	if (d_type == DT_LNK)
		*is_link = 1;
	else if (d_type != DT_UNKNOWN)
		return;

	if (build_path(path, sizeof path, directory, name) != 0)
		return;
	if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
		*not_reg = 0;
	if (d_type == DT_UNKNOWN) {
		struct stat lst;
		if (lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode))
			*is_link = 1;
	}
}

static void listing_oom(DIR *pDir) {
	if (pDir)
		closedir(pDir);
	displayAlert("Couldn't allocate memory!");
	exit(1);
}

/* Scan + sort. Caller owns *out. Returns 0, or -1 if directory cannot be opened. */
static int listing_scan(const char *directory, struct cfl_listing *out) {
	DIR *pDir;
	struct dirent *ent;
	struct cfl_ent *ents = NULL;
	int n = 0;
	int cap = 16;
	int i;

	memset(out, 0, sizeof *out);
	pDir = opendir(directory);
	if (pDir == NULL)
		return -1;

	cap = 1024;
	ents = malloc((size_t)cap * sizeof *ents);
	if (ents == NULL)
		listing_oom(pDir);

	while ((ent = readdir(pDir)) != NULL) {
		struct cfl_ent *grown;
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		if (ent->d_name[0] == '.' && hiddenFlag == 0)
			continue;
		if (n >= cap) {
			cap *= 2;
			grown = realloc(ents, (size_t)cap * sizeof *ents);
			if (grown == NULL) {
				free(ents);
				listing_free(out);
				listing_oom(pDir);
			}
			ents = grown;
		}
		ents[n].name = listing_store_name(&out->names_arena, ent->d_name);
		if (ents[n].name == NULL) {
			free(ents);
			listing_free(out);
			listing_oom(pDir);
		}
		classify_entry(directory, ents[n].name, ent->d_type, &ents[n].not_reg, &ents[n].is_link);
		n++;
	}
	closedir(pDir);

	if (n > 1)
		qsort(ents, (size_t)n, sizeof *ents, cfl_ent_cmp);

	out->n = n;
	if (n == 0) {
		free(ents);
		return 0;
	}
	out->names = malloc((size_t)n * sizeof(char *));
	out->not_reg = malloc((size_t)n);
	out->is_link = malloc((size_t)n);
	if (out->names == NULL || out->not_reg == NULL || out->is_link == NULL) {
		free(ents);
		listing_free(out);
		listing_oom(NULL);
	}
	for (i = 0; i < n; i++) {
		out->names[i] = ents[i].name;
		out->not_reg[i] = ents[i].not_reg;
		out->is_link[i] = ents[i].is_link;
	}
	free(ents);
	return 0;
}

/* Reuse a sorted snapshot until the directory's identity or mtime changes. */
static int listing_cached(struct cfl_list_cache *cache, const char *directory) {
	struct stat before, after;
	struct cfl_listing fresh;
	int changed;

	if (stat(directory, &before) != 0 || !S_ISDIR(before.st_mode))
		return -1;
	if (cache->valid && cache->hidden == hiddenFlag && cache->dev == before.st_dev &&
	    cache->ino == before.st_ino &&
	    cache->mtim.tv_sec == before.st_mtim.tv_sec &&
	    cache->mtim.tv_nsec == before.st_mtim.tv_nsec &&
	    strcmp(cache->dir, directory) == 0)
		return cache->list.n;

	if (listing_scan(directory, &fresh) != 0)
		return -1;
	changed = 0;
	if (stat(directory, &after) != 0)
		changed = 1;
	else if (after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
	         after.st_mtim.tv_nsec != before.st_mtim.tv_nsec)
		changed = 1;

	listing_free(&cache->list);
	cache->list = fresh;
	snprintf(cache->dir, sizeof cache->dir, "%s", directory);
	cache->hidden = hiddenFlag;
	cache->dev = before.st_dev;
	cache->ino = before.st_ino;
	cache->mtim = before.st_mtim;
	cache->valid = changed ? 0 : 1;
	return cache->list.n;
}

static void listing_cache_invalidate(void) {
	listing_free(&cache_cwd.list);
	listing_free(&cache_preview.list);
	cache_cwd.valid = 0;
	cache_preview.valid = 0;
	cache_cwd.dir[0] = '\0';
	cache_preview.dir[0] = '\0';
}

int getFiles(char* directory, char*** target) {
	struct cfl_listing listing;
	char **owned;
	int i, n;

	if (listing_scan(directory, &listing) != 0)
		return -1;
	n = listing.n;
	owned = malloc((size_t)(n > 0 ? n : 1) * sizeof(char *));
	if (owned == NULL) {
		listing_free(&listing);
		displayAlert("Couldn't allocate memory!");
		exit(1);
	}
	for (i = 0; i < n; i++) {
		owned[i] = strdup(listing.names[i]);
		if (owned[i] == NULL) {
			while (i > 0)
				free(owned[--i]);
			free(owned);
			listing_free(&listing);
			displayAlert("Couldn't allocate memory!");
			exit(1);
		}
	}
	listing_free(&listing);
	*target = owned;
	return n;
}

/* Kept for callers that still qsort names. The file list uses cfl_ent_cmp
 * on kinds captured once per directory change. */
int compare (const void * a, const void * b ) {
	char temp_filepath1[PATH_MAX];
	char temp_filepath2[PATH_MAX];

	int is_dir1 = 0;
	int is_dir2 = 0;
	if (build_path(temp_filepath1, sizeof temp_filepath1, sort_dir, *(char **)a) == 0)
		is_dir1 = (isRegularFile(temp_filepath1) == 0);
	if (build_path(temp_filepath2, sizeof temp_filepath2, sort_dir, *(char **)b) == 0)
		is_dir2 = (isRegularFile(temp_filepath2) == 0);

	if (is_dir1 != is_dir2) {
		return is_dir2 - is_dir1;
	}
	return strcasecmp(*(char **)a, *(char **)b);
}

void handleFlags(char** directories) {
	if(clearFlag == 1){
		wclear(preview_win);
		wrefresh(preview_win);
		clearFlag = 0;
	}

	if(clearFlagImg == 1){
		//clearImg();
		ueberzugpp_remove();

		clearFlagImg = 0;
	}

	if(searchFlag == 1){
		searchFlag = 0;
		last[strlen(last)-1] = '\0';
		for(i=0; i<len; i++)
			if(strcmp(directories[i],last) == 0) {
				selection = i;
				break;
			}
		if(len > maxy){
			if(selection > maxy-3)
				start = selection - maxy + 3;
		}

		endwin();
		refresh();
	}

	if(backFlag == 1){
		backFlag = 0;
		for(i=0; i<len; i++)
			if(strcmp(directories[i],last) == 0) {
				selection = i;
				break;
			}
		if(len > maxy) {
			if(selection > maxy-3)
				start = selection - maxy + 3;
		}
	}
}

// Moving this function to ui.h requires further refactoring
void initWindows(void) {
	static int win_lines = -1, win_cols = -1;
	int content_left, panel_top, panel_height, sep_col;
	int margin, content_w, left_w, right_w;
	int same;

	sigprocmask(SIG_UNBLOCK, &x, NULL);

	/* endwin() (shell, pager) leaves the old WINDOW objects detached. */
	if (isendwin()) {
		if (status_win) delwin(status_win);
		if (current_win) delwin(current_win);
		if (preview_win) delwin(preview_win);
		status_win = current_win = preview_win = NULL;
		win_lines = -1;
		win_cols = -1;
		refresh();
	}

	ui_get_layout(&content_left, &panel_top, &panel_height, &sep_col);

	margin = UI_MARGIN;
	content_w = COLS - 2 * margin;
	left_w  = (content_w * 2) / 5 + 2;
	right_w = content_w - left_w - 1;

	starty = panel_top;
	preview_start = sep_col;

	/* newwin() every key leaked a full set of windows and got slower as
	 * the ncurses window list grew. Reuse them until the terminal size changes.
	 * handleResize() deletes and nulls the windows before calling us. */
	same = current_win && preview_win && status_win &&
	       win_lines == LINES && win_cols == COLS;
	if (same) {
		ui_clear_gaps();
		return;
	}

	if (status_win) delwin(status_win);
	if (current_win) delwin(current_win);
	if (preview_win) delwin(preview_win);

	status_win = newwin(2, content_w, margin, content_left);
	current_win = newwin(panel_height, left_w,  starty, content_left);
	preview_win = newwin(panel_height, right_w, starty, sep_col + 1);
	win_lines = LINES;
	win_cols = COLS;

	keypad(current_win, TRUE);
	ui_clear_gaps();
}

int isRegularFile(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return S_ISREG(st.st_mode);
}

int isSymlink(const char *path) {
	struct stat st;
	if (lstat(path, &st) != 0) return 0;
	return S_ISLNK(st.st_mode);
}

int readSymlinkTarget(const char *path, char *buf, size_t bufsz) {
	if (!path || !buf || bufsz == 0) return -1;
	ssize_t n = readlink(path, buf, bufsz - 1);
	if (n < 0) {
		buf[0] = '\0';
		return -1;
	}
	buf[n] = '\0';
	return 0;
}

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

void init(int argc, char* argv[]) {
	sigemptyset (&x);
	sigaddset(&x, SIGWINCH);

	setlocale(LC_ALL, "");

	uid_t uid = getuid();
	info = getpwuid(uid);

	if( getenv("SHELL") == NULL) {
		shell = strdup("/bin/bash");
		if(shell == NULL) {
			printf("%s\n", "Couldn't initialize shell");
			exit(1);
		}
	} else {
		allocSize = snprintf(NULL, 0, "%s", getenv("SHELL"));
		shell = malloc(allocSize + 1);
		if(shell == NULL) {
			printf("%s\n", "Couldn't initialize shell");
			exit(1);
		}
		snprintf(shell, allocSize+1, "%s", getenv("SHELL"));
	}

	if( getenv("EDITOR") == NULL) {
		editor = strdup("nvim");
		if(editor == NULL) {
			printf("%s\n", "Couldn't initialize editor");
			exit(1);
		}
	} else {
		allocSize = snprintf(NULL, 0, "%s", getenv("EDITOR"));
		editor = malloc(allocSize + 1);
		if(editor == NULL) {
			printf("%s\n", "Couldn't initialize editor");
			exit(1);
		}
		snprintf(editor, allocSize+1, "%s", getenv("EDITOR"));
	}

	struct stat st = {0};
	if( getenv("XDG_CONFIG_HOME") == NULL) {
		allocSize = snprintf(NULL,0,"%s/.config/cfl",info->pw_dir);
		cache_path = malloc(allocSize+1);
		if(cache_path == NULL) {
			printf("%s\n", "Couldn't initialize cache path");
			exit(1);
		}
		snprintf(cache_path,allocSize+1,"%s/.config/cfl",info->pw_dir);
	} else {
		allocSize = snprintf(NULL,0,"%s/cfl",getenv("XDG_CONFIG_HOME"));
		cache_path = malloc(allocSize+1);
		if(cache_path == NULL) {
			printf("%s\n", "Couldn't initialize cache path");
			exit(1);
		}
		snprintf(cache_path,allocSize+1,"%s/cfl",getenv("XDG_CONFIG_HOME"));
	}
	if (stat(cache_path, &st) == -1) {
		mkdir(cache_path, 0751);
	}

	/* Remove legacy shared clipboard (pre session-local selection) */
	{
		char legacy_clipboard[PATH_MAX];
		if (snprintf(legacy_clipboard, sizeof legacy_clipboard, "%s/clipboard", cache_path)
		    < (int)sizeof legacy_clipboard) {
			remove(legacy_clipboard);
		}
	}

	/* Session-private clipboard: not shared across instances or restarts */
	allocSize = snprintf(NULL, 0, "%s/clipboard.%d", cache_path, (int)getpid());
	clipboard_path = malloc(allocSize + 1);
	if (clipboard_path == NULL) {
		printf("%s\n", "Couldn't initialize clipboard path");
		exit(1);
	}
	snprintf(clipboard_path, allocSize + 1, "%s/clipboard.%d", cache_path, (int)getpid());

	allocSize = snprintf(NULL,0,"%s/bookmarks",cache_path);
	bookmarks_path = malloc(allocSize+1);
	if(bookmarks_path == NULL) {
		printf("%s\n", "Couldn't initialize bookmarks path");
		exit(1);
	}
	snprintf(bookmarks_path,allocSize+1,"%s/bookmarks",cache_path);

	allocSize = snprintf(NULL, 0, "%s/clipboard.%d.tmp", cache_path, (int)getpid());
	temp_clipboard_path = malloc(allocSize + 1);
	if (temp_clipboard_path == NULL) {
		printf("%s\n", "Couldn't initialize temp clipboard path path");
		exit(1);
	}
	snprintf(temp_clipboard_path, allocSize + 1, "%s/clipboard.%d.tmp", cache_path, (int)getpid());

	/* FreeDesktop home trash: $XDG_DATA_HOME/Trash or ~/.local/share/Trash */
	if (getenv("XDG_DATA_HOME") != NULL && getenv("XDG_DATA_HOME")[0] != '\0') {
		allocSize = snprintf(NULL, 0, "%s/Trash", getenv("XDG_DATA_HOME"));
		trash_path = malloc(allocSize + 1);
		if (trash_path == NULL) {
			printf("%s\n", "Couldn't initialize trash path");
			exit(1);
		}
		snprintf(trash_path, allocSize + 1, "%s/Trash", getenv("XDG_DATA_HOME"));
	} else {
		allocSize = snprintf(NULL, 0, "%s/.local/share/Trash", info->pw_dir);
		trash_path = malloc(allocSize + 1);
		if (trash_path == NULL) {
			printf("%s\n", "Couldn't initialize trash path");
			exit(1);
		}
		snprintf(trash_path, allocSize + 1, "%s/.local/share/Trash", info->pw_dir);
	}

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != NULL) {
		snprintf(dir, sizeof(dir), "%s", cwd);
	} else {
		snprintf(dir, sizeof(dir), "/");
	}

	if(argc == 2) {
		char arg[PATH_MAX];
		snprintf(arg, sizeof(arg), "%s", argv[1]);
		if(arg[0] == '/') {
			if(strlen(arg) > 1 && arg[strlen(arg)-1] == '/') {
				arg[strlen(arg)-1] = '\0';
			}
			snprintf(dir, sizeof(dir), "%s", arg);
		} else {
			char temp[PATH_MAX];
			if (build_path(temp, sizeof temp, dir, arg) != 0) {
				printf("Path too long\n");
				exit(1);
			}
			snprintf(dir, sizeof(dir), "%s", temp);
		}
	} else if(argc > 2) {
		printf("Incorrect Usage!\n");
		exit(1);
	}
	/* Selection starts empty each session; never restore a previous list */
	selectedFiles = 0;
	tab_boot();
}

/* In-process tabs (nnn contexts). Each one keeps its own directory, cursor,
 * list scroll, hidden-files flag, file-info mode, and preview scroll.
 * The selection clipboard stays global so copy/paste works across tabs. */
int cfl_cur_tab = 0;
int cfl_tab_active[CFL_TABS] = {1, 0, 0, 0};

struct cfl_tab {
	char dir[PATH_MAX];
	char selected_name[NAME_MAX];
	int selection;
	int start;
	int hidden;
	int file_info;
	int preview_scroll;
};

static struct cfl_tab cfl_tabs[CFL_TABS];
static int tab_need_reseek = 0;
static int tab_need_scroll = 0;

static void tab_capture(struct cfl_tab *t) {
	snprintf(t->dir, sizeof t->dir, "%s", dir);
	snprintf(t->selected_name, sizeof t->selected_name, "%s", selected_file);
	t->selection = selection;
	t->start = start;
	t->hidden = hiddenFlag;
	t->file_info = fileInfoMode;
	t->preview_scroll = preview_scroll;
}

static void tab_apply(const struct cfl_tab *t) {
	snprintf(dir, sizeof dir, "%s", t->dir);
	snprintf(selected_file, sizeof selected_file, "%s", t->selected_name);
	selection = t->selection;
	start = t->start;
	hiddenFlag = t->hidden;
	fileInfoMode = t->file_info;
	preview_scroll = t->preview_scroll;
}

void tab_boot(void) {
	memset(cfl_tabs, 0, sizeof cfl_tabs);
	cfl_cur_tab = 0;
	cfl_tab_active[0] = 1;
	cfl_tab_active[1] = cfl_tab_active[2] = cfl_tab_active[3] = 0;
	tab_capture(&cfl_tabs[0]);
	cfl_tabs[0].selected_name[0] = '\0';
}

/* 0-based. Unused tabs open on the directory of the tab you left.
 * Returns 1 when the visible context changed. */
int switch_tab(int n) {
	struct cfl_tab *dst;

	if (n < 0 || n >= CFL_TABS || n == cfl_cur_tab)
		return 0;

	tab_capture(&cfl_tabs[cfl_cur_tab]);
	cfl_tab_active[cfl_cur_tab] = 1;

	dst = &cfl_tabs[n];
	if (!cfl_tab_active[n]) {
		tab_capture(dst);
		dst->selection = 0;
		dst->start = 0;
		dst->preview_scroll = 0;
		dst->selected_name[0] = '\0';
		cfl_tab_active[n] = 1;
		tab_need_reseek = 0;
	} else {
		tab_need_reseek = 1;
	}
	tab_need_scroll = 1;

	tab_apply(dst);
	cfl_cur_tab = n;
	/* Drop the other tab's image and any text still drawn in the preview. */
	if (preview_win) {
		wclear(preview_win);
		wrefresh(preview_win);
	}
	ueberzugpp_remove();
	return 1;
}

/* After a tab restore, land on the file that was current there. */
void tab_reseek_if_needed(char **names, int n) {
	const char *want;
	int i;

	if (!tab_need_reseek)
		return;
	tab_need_reseek = 0;
	if (!names || n <= 0)
		return;
	want = cfl_tabs[cfl_cur_tab].selected_name;
	if (!want[0] || strcmp(want, "Empty") == 0)
		return;
	for (i = 0; i < n; i++) {
		if (names[i] && strcmp(names[i], want) == 0) {
			selection = i;
			return;
		}
	}
}

/* Keep the restored cursor inside the list window. */
void tab_fix_scroll(void) {
	int visible;

	if (!tab_need_scroll)
		return;
	tab_need_scroll = 0;
	if (len <= 0) {
		start = 0;
		selection = 0;
		return;
	}
	if (selection < 0)
		selection = 0;
	if (selection >= len)
		selection = len - 1;
	visible = (maxy > 1) ? (maxy - 1) : 1;
	if (start < 0)
		start = 0;
	if (start > selection)
		start = selection;
	if (selection >= start + visible)
		start = selection - visible + 1;
	if (start < 0)
		start = 0;
}

int fileExists(char *file) {
    return (access(file, F_OK) != -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}

char* replace(char* str, char* a, char* b) {
	int len  = strlen(str);
	int lena = strlen(a);
	int lenb = strlen(b);
	char* p;
	for (p = str; (p = strstr(p, a)); ++p) {
		if (lena != lenb) // shift end as needed
			memmove(p+lenb, p+lena, len - (p - str) + lenb);
		memcpy(p, b, lenb);
	}
	return str;
}

/* In-memory clipboard so marking and drawing are not O(n) file scans.
 * Empty slots end a probe. Tombstones keep probes correct after a removal. */
struct clip_slot {
	unsigned hash;
	char *path;
	unsigned char state; /* 0 empty, 1 used, 2 tomb */
};

static struct clip_slot *clip_tab = NULL;
static size_t clip_cap = 0;
static size_t clip_len = 0;
static int clip_ready = 0;

static unsigned clip_hash(const char *s) {
	unsigned h = 2166136261u;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 16777619u;
	}
	return h ? h : 1u;
}

static void clip_free_tab(void) {
	size_t i;
	if (clip_tab) {
		for (i = 0; i < clip_cap; i++) {
			if (clip_tab[i].state == 1)
				free(clip_tab[i].path);
		}
		free(clip_tab);
	}
	clip_tab = NULL;
	clip_cap = 0;
	clip_len = 0;
}

static int clip_lookup(const char *path, unsigned h, size_t *slot) {
	size_t i, mask, first_tomb;
	int saw_tomb = 0;

	if (clip_cap == 0) {
		*slot = 0;
		return 0;
	}
	mask = clip_cap - 1;
	i = (size_t)h & mask;
	first_tomb = 0;
	for (size_t steps = 0; steps < clip_cap; steps++) {
		if (clip_tab[i].state == 0) {
			*slot = saw_tomb ? first_tomb : i;
			return 0;
		}
		if (clip_tab[i].state == 2) {
			if (!saw_tomb) {
				first_tomb = i;
				saw_tomb = 1;
			}
		} else if (clip_tab[i].hash == h && strcmp(clip_tab[i].path, path) == 0) {
			*slot = i;
			return 1;
		}
		i = (i + 1) & mask;
	}
	*slot = saw_tomb ? first_tomb : 0;
	return 0;
}

static void clip_grow(void) {
	size_t ncap = clip_cap ? clip_cap * 2 : 1024;
	struct clip_slot *old = clip_tab;
	size_t ocap = clip_cap;
	size_t i;

	clip_tab = calloc(ncap, sizeof *clip_tab);
	if (clip_tab == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	clip_cap = ncap;
	clip_len = 0;
	for (i = 0; i < ocap; i++) {
		size_t slot;
		if (old[i].state != 1)
			continue;
		clip_lookup(old[i].path, old[i].hash, &slot);
		clip_tab[slot].state = 1;
		clip_tab[slot].hash = old[i].hash;
		clip_tab[slot].path = old[i].path;
		clip_len++;
	}
	free(old);
}

static void clip_add_copy(const char *path) {
	unsigned h;
	size_t slot;

	if (!path || !path[0])
		return;
	if (clip_cap == 0 || (clip_len + 1) * 10 >= clip_cap * 7)
		clip_grow();
	h = clip_hash(path);
	if (clip_lookup(path, h, &slot))
		return;
	clip_tab[slot].state = 1;
	clip_tab[slot].hash = h;
	clip_tab[slot].path = strdup(path);
	if (clip_tab[slot].path == NULL) {
		clip_tab[slot].state = 0;
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	clip_len++;
}

static void clip_load(void) {
	FILE *f;
	char buf[PATH_MAX];

	if (clip_ready)
		return;
	clip_free_tab();
	clip_ready = 1;
	if (!clipboard_path)
		return;
	f = fopen(clipboard_path, "r");
	if (f == NULL)
		return;
	while (fgets(buf, PATH_MAX, f)) {
		size_t n = strlen(buf);
		if (n && buf[n - 1] == '\n')
			buf[n - 1] = '\0';
		if (buf[0])
			clip_add_copy(buf);
	}
	fclose(f);
}

static int clip_contains(const char *path) {
	unsigned h;
	size_t slot;
	clip_load();
	if (!path || !path[0] || clip_cap == 0)
		return 0;
	h = clip_hash(path);
	return clip_lookup(path, h, &slot);
}

int checkClipboard(char *filepath) {
	char *temp = NULL;
	int found;

	allocSize = snprintf(NULL,0,"%s", filepath);
	temp = malloc(allocSize+1);
	if(temp == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp, allocSize+1, "%s", filepath);
	replace(temp,"\n","//");
	found = clip_contains(temp);
	free(temp);
	return found;
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
	if (clip_ready)
		clip_add_copy(filepath);
}

void removeClipboard(char *filepath) {
	FILE *f1;
	FILE *f2;
	char buf[PATH_MAX];

	f1 = fopen(clipboard_path, "r");
	f2 = fopen(temp_clipboard_path, "w");
	if (f1 == NULL) {
		endwin();
		printf("Couldn't Open Clipboard File!\n");
		exit(1);
	}
	if (f2 == NULL) {
		endwin();
		printf("Couldn't Create Temporary Clipboard File!\n");
		fclose(f1); 
		exit(1);
	}

	while (fgets(buf, PATH_MAX, f1)) {
		buf[strlen(buf) - 1] = '\0';
		if (strcasecmp(buf, filepath) != 0)
			fprintf(f2, "%s\n", buf);
	}

	fclose(f1);
	fclose(f2);

	if (rename(temp_clipboard_path, clipboard_path) != 0) {
		endwin();
		perror("Couldn't move temporary clipboard file");
		exit(1);
	}
	if (clip_ready) {
		unsigned h = clip_hash(filepath);
		size_t slot;
		if (clip_lookup(filepath, h, &slot) && clip_tab[slot].state == 1) {
			free(clip_tab[slot].path);
			clip_tab[slot].path = NULL;
			clip_tab[slot].state = 2;
			if (clip_len)
				clip_len--;
		}
	}
}

void clearClipboard(void) {
	/* Remove the session clipboard file so selection is fully gone */
	if (clipboard_path)
		remove(clipboard_path);
	clip_free_tab();
	clip_ready = 1;
	selectedFiles = 0;
}

/* Mark every entry in the current directory listing (Shift+V).
 * One append and a hash lookup per name. The old loop re-read the
 * clipboard file for every file, which is quadratic. */
void selectAllFiles(char **directories, int n) {
	FILE *f;
	int i;

	if (!directories || n <= 0)
		return;
	clip_load();
	f = fopen(clipboard_path, "a");
	if (f == NULL) {
		endwin();
		printf("Error accessing clipboard!\n");
		exit(1);
	}
	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		if (!directories[i])
			continue;
		if (build_path(path, sizeof path, dir, directories[i]) != 0)
			continue;
		replace(path, "\n", "//");
		if (clip_contains(path))
			continue;
		fprintf(f, "%s\n", path);
		clip_add_copy(path);
	}
	fclose(f);
	selectedFiles = (int)clip_len;
}

void getParentPath(char *path) {
	char *p;
	p = strrchr(path,'/');
	path[p-path] = '\0';
	if(path[0] == '\0') {
		path[0] = '/';
		path[1] = '\0';
	}
}

int getWritePermissions(char *path) {
    return (access(path, W_OK) == 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

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

void removeFiles(void){
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	int flag;
	if(f == NULL){
		return;
	}
	endwin();
	while(fgets(buf, PATH_MAX, (FILE*) f)){
		buf[strlen(buf)-1] = '\0';
		replace(buf,"//","\n");
		if( getWritePermissions(buf) == 1 )
			flag = 0;
		else
			flag = 1;
		pid_t pid = fork();
		if(pid == 0) {
			if( flag == 0 ) 
				execlp("rm", "rm", "-rfv", buf, (char *)0);
			else
				execlp("sudo", "sudo", "rm", "-rfv", buf, (char *)0);
			exit(1);
		}else{
			int status;
			waitpid(pid, &status, 0);
		}
	}
	refresh();
	fclose(f);
	clearClipboard();
}

void renameFiles(char **directories){
	if( access( clipboard_path, F_OK ) == -1 ){
		if (build_path(temp_dir, sizeof temp_dir, dir, directories[selection]) != 0) {
			displayAlert("Path too long");
			return;
		}
		char *temp = strdup(temp_dir);
		if(temp == NULL){
			endwin();
			printf("%s\n", "Couldn't allocate memory!");
			exit(1);
		}
		writeClipboard(replace(temp,"\n","//"));
		free(temp);
	}
	pid_t pid;

	FILE *f = fopen(clipboard_path, "r");
	FILE *f2;

	char *cmd = NULL;

	char buf[PATH_MAX];
	char buf2[PATH_MAX];

	int count = 0;
	int count2 = 0;

	pid = fork();
	if( pid == 0 ){
		execlp("cp", "cp", clipboard_path, temp_clipboard_path, (char *)0);
		exit(1);
	} else {
		int status;
		waitpid(pid, &status, 0);
	}
	endwin();

	allocSize = snprintf(NULL,0,"%s", editor);
	cmd = malloc(allocSize + 1);
	if(cmd == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(cmd,allocSize+1,"%s", editor);

	pid = fork();
	if( pid == 0 ) {
		execlp(cmd, cmd, temp_clipboard_path, (char *)0);
		exit(1);
	} else {
		int status;
		waitpid(pid, &status, 0);
		free(cmd);
	}

	while(fgets(buf, PATH_MAX, (FILE*) f)) {
		count2=-1;
		f2 = fopen(temp_clipboard_path,"r");
		while(fgets(buf2, PATH_MAX, (FILE*) f2)) {
			count2++;
			if(buf[strlen(buf)-1] == '\n')
				buf[strlen(buf)-1] = '\0';
			if(count2 == count){
				if(buf2[strlen(buf2)-1] == '\n')
					buf2[strlen(buf2)-1] = '\0';

				replace(buf2,"//","\n");
				replace(buf,"//","\n");

				pid = fork();
				if( pid == 0 ) {
					execlp("mv", "mv", "-ivn", buf, buf2, (char *)0);
					exit(1);
				} else {
					int status;
					waitpid(pid, &status, 0);
				}
			}
		}
		count++;
		fclose(f2);
	}
	fclose(f);

	clearClipboard();
	remove(temp_clipboard_path);

	refresh();
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
		if (build_path(dest_path, sizeof dest_path, present_dir, base) != 0) {
			fprintf(stderr, "Path too long: %s\n", buf);
			continue;
		}

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

/* ---------- FreeDesktop Trash (home trash) ---------- */

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
void handleMouse(void) {
	static bool dragging_file = false;
	static bool dragging_preview = false;

	MEVENT ev;
	if (getmouse(&ev) != OK) return;
	if (!current_win || !preview_win) return;

	int mx = ev.x;
	int my = ev.y;

	int file_begy, file_begx, file_rows, file_cols;
	getbegyx(current_win, file_begy, file_begx);
	getmaxyx(current_win, file_rows, file_cols);

	int prev_begy, prev_begx, prev_rows, prev_cols;
	getbegyx(preview_win, prev_begy, prev_begx);
	getmaxyx(preview_win, prev_rows, prev_cols);
	(void)prev_begy;

	int file_sb_col    = file_begx + file_cols - 1;                    /* right edge of left panel */
	int preview_sb_col = prev_begx + prev_cols - 1;                    /* right edge of right panel (symmetric) */

	int panel_top    = file_begy;
	int panel_height = file_rows;

	if (my < panel_top || my >= panel_top + panel_height) return;

	/* Wheel */
	if (ev.bstate & BUTTON4_PRESSED) { /* up */
		int lines = 5;
		if (mx <= file_sb_col + 2) {
			start -= lines; if (start < 0) start = 0;
			wclear(current_win);
		} else {
			preview_scroll -= lines; if (preview_scroll < 0) preview_scroll = 0;
			wclear(preview_win);
		}
		return;
	}
	if (ev.bstate & BUTTON5_PRESSED) { /* down */
		int lines = 5;
		if (mx <= file_sb_col + 2) {
			if (len > file_rows) {
				start += lines;
				int maxs = len - file_rows; if (start > maxs) start = maxs;
			}
			wclear(current_win);
		} else {
			preview_scroll += lines;
			long ptotal = (len_preview > 0) ? len_preview : preview_total_lines;
			int maxs = ptotal > prev_rows ? (int)(ptotal - prev_rows) : 0;
			if (preview_scroll > maxs) preview_scroll = maxs;
			wclear(preview_win);
		}
		return;
	}

	/* Click to jump */
	if (ev.bstate & BUTTON1_PRESSED) {
		int rel_y = my - panel_top;
		if (rel_y < 0) rel_y = 0;
		if (rel_y >= panel_height) rel_y = panel_height - 1;

		if (mx == file_sb_col) {
			if (len > file_rows && file_rows > 1) {
				double ratio = (double)rel_y / (panel_height - 1.0);
				start = (int)(ratio * (len - file_rows) + 0.5);
				if (start < 0) start = 0;
				int maxs = len - file_rows; if (start > maxs) start = maxs;
				wclear(current_win);
			}
		} else if (mx == preview_sb_col) {
			long ptotal = (len_preview > 0) ? len_preview : preview_total_lines;
			if (ptotal > prev_rows && prev_rows > 1) {
				double ratio = (double)rel_y / (panel_height - 1.0);
				preview_scroll = (int)(ratio * (ptotal - prev_rows) + 0.5);
				if (preview_scroll < 0) preview_scroll = 0;
				wclear(preview_win);
			}
		}

		dragging_file   = (mx == file_sb_col);
		dragging_preview = (mx == preview_sb_col);
	}

	/* Drag */
	if ((dragging_file || dragging_preview) &&
	    (ev.bstate & (BUTTON1_PRESSED | REPORT_MOUSE_POSITION | BUTTON1_RELEASED))) {

		int rel_y = my - panel_top;
		if (rel_y < 0) rel_y = 0;
		if (rel_y >= panel_height) rel_y = panel_height - 1;

		if (dragging_file && len > file_rows && file_rows > 1) {
			double ratio = (double)rel_y / (panel_height - 1.0);
			start = (int)(ratio * (len - file_rows) + 0.5);
			if (start < 0) start = 0;
			int maxs = len - file_rows; if (start > maxs) start = maxs;
			wclear(current_win);
		}
		long ptotal = (len_preview > 0) ? len_preview : preview_total_lines;
		if (dragging_preview && ptotal > prev_rows && prev_rows > 1) {  /* right edge of preview pane */
			double ratio = (double)rel_y / (panel_height - 1.0);
			preview_scroll = (int)(ratio * (ptotal - prev_rows) + 0.5);
			if (preview_scroll < 0) preview_scroll = 0;
			wclear(preview_win);
		}

		if (ev.bstate & BUTTON1_RELEASED) {
			dragging_file = false;
			dragging_preview = false;
		}
	}

	if (ev.bstate & BUTTON1_RELEASED) {
		dragging_file = false;
		dragging_preview = false;
	}
}

void scrollUp(void) {
	selection--;
	selection = ( selection < 0 ) ? 0 : selection;
	if(len >= maxy-1)
		if(selection <= start + maxy/2){
			if(start == 0)
				wclear(current_win);
			else{
				start--;
				wclear(current_win);
			}
		}
}

void scrollDown(void) {
	selection++;
	selection = ( selection > len-1 ) ? len-1 : selection;
	if(len >= maxy-1)
		if(selection - 1 > maxy/2){
			if(start + maxy - 2 != len){
				start++;
				wclear(current_win);
			}
		}
}

void nextPage(void) {
	if(selection + maxy-1 < len){
		start = start + maxy-1;
		selection = selection + maxy-1;
		wclear(current_win);
	}
}

void prevPage(void) {
	start = ( start - maxy-1 > 0 ) ? start - maxy-1 : 0;
	selection = ( selection - maxy-1 > 0 ? selection - maxy-1 : 0);
	wclear(current_win);
}

void goForward(void) {
	if(len_preview != -1){
		snprintf(dir, sizeof(dir), "%s", next_dir);
		selection = 0;
		start = 0;
	} else {
		openFile(next_dir);
		clearFlag = 1;
	}
}

void goBack(void) {
	snprintf(temp_dir, sizeof(temp_dir), "%s", dir);
	snprintf(dir, sizeof(dir), "%s", prev_dir);

	selection = 0;
	start = 0;
	backFlag = 1;

	getLastToken("/");
}

void goTOP(void) {
	selection = start;
}

void goEND(void) {
	selection = len - 1;
	if(len > maxy - 2) {
		start = len - maxy + 2;
	} else {
		start = 0;
	}
}

void goSTART( void ) {
	selection = 0;
	start = 0;
}

void goBOTTOM() {
	if(len >= maxy) {
		selection = start + maxy - 2;
	} else {
		selection = len - 1;
	}
}

void goMID(void) {
	if(len >= maxy) {
		selection = start + maxy/2;
	} else {
		selection = (len / 2) - 1;
	}
}

/* Gets the last token from temp_dir by using `tokenizer` as a delimeter */
void getLastToken(char *tokenizer) {
	pch = strtok(temp_dir, tokenizer);
	while (pch != NULL) {
		free(last);
		last = strdup(pch);
		if(last == NULL) {
			endwin();
			printf("%s\n", "Couldn't allocate memory!");
			exit(1);
		}
		pch = strtok(NULL,tokenizer);
	}
}

void goShell(pid_t pid) {
	pid = fork();
	if( pid == 0 ) {
		chdir(dir);
		execlp(shell, shell, (char *)0);
		exit(1);
	} else {
		int status;
		waitpid(pid, &status, 0);
	}

	start = 0;
	selection = 0;
}

void Deleting(void) {
	if( fileExists(clipboard_path) == 1 ) {
		ui_overlay_backdrop();
		keys_win = ui_modal_create(5, 40);
		if (!keys_win) return;
		mvwprintw(keys_win, 1, 2, "Key\tCommand");
		mvwprintw(keys_win, 2, 2, "%c\tMove to Trash", KEY_GARBAGE);
		mvwprintw(keys_win, 3, 2, "%c\tDelete", KEY_DELETE);
		wrefresh(keys_win);
		secondKey = wgetch(keys_win);
		ui_modal_destroy(keys_win);
		keys_win = NULL;
		if(secondKey == KEY_GARBAGE) {
			trashFiles();
		} else if( secondKey == KEY_DELETE ) {
#if CONFIRM_ON_DEL
			displayAlert("Confirm (y/n): ");
			char confirm = wgetch(status_win);
			if(confirm == 'y') {
				removeFiles();
			} else {
				displayAlert("ABORTED");
				sleep(1);
			}
#else
			removeFiles();
			displayAlert("File deletion success!");
#endif
		}
	} else {
		displayAlert("Select some files first!");
	}
}

void ViewSel(pid_t pid) {
	if( access( clipboard_path, F_OK ) != -1 ) {
		pid = fork();
		if( pid == 0 ) {
			execlp("less", "less", clipboard_path, (char *)0);
			exit(1);
		} else {
			int status;
			waitpid(pid, &status, 0);
		}
		refresh();
	} else {
		displayAlert("Selection List is Empty!");
		sleep(1);
	}
}

void EditSel(pid_t pid) {
	sigprocmask(SIG_BLOCK, &x, NULL);
	endwin();

	if(access(clipboard_path, F_OK) != -1 ) {
		pid = fork();
		if(pid == 0 ){
			execlp(editor, editor, clipboard_path, (char *)0);
			exit(1);
		} else {
			int status;
			waitpid(pid, &status, 0);
			setSelectionCount();
		}
		refresh();
	} else {
		displayAlert("Selection List is Empty!");
	}
}

void CreateDir() {
	char dirName[256] = {0};
	ui_prompt_string("Enter directory name: ", dirName, 256);

	if (strlen(dirName) == 0) {
		displayAlert("Directory name cannot be empty");
		return;
	}

	char fullPath[PATH_MAX];
	if (build_path(fullPath, sizeof fullPath, dir, dirName) != 0) {
		displayAlert("Path too long");
		return;
	}
	if (mkdir(fullPath, 0755) == 0) {
		displayAlert("Directory created");
	} else {
		displayAlert("Error creating directory");
	}
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

void help(WINDOW *main_win) {
    (void)main_win;
    int rows = LINES / 2;
    int cols = COLS / 2;

    ui_overlay_backdrop();
    WINDOW *help_win = ui_modal_create(rows, cols);
    if (!help_win) return;

    mvwprintw(help_win, 0, (cols - 14)/2, " cfl Keybindings ");

    // title or header
    mvwprintw(help_win, 2, 2, "Key          Action");

    // Hardcoded list for now — later extract from a table/struct
    int line = 4;
    mvwprintw(help_win, line++, 2, " h       Parent directory");
    mvwprintw(help_win, line++, 2, " j       Next item");
    mvwprintw(help_win, line++, 2, " k       Previous item");
    mvwprintw(help_win, line++, 2, " l       Open file or Enter dir");
    mvwprintw(help_win, line++, 2, " 1-4     Switch tab (new tab opens here)");
    mvwprintw(help_win, line++, 2, " v       Toggle select current item");
    mvwprintw(help_win, line++, 2, " V       Select all files in directory");
    mvwprintw(help_win, line++, 2, " m       Move selection to current directory");
    mvwprintw(help_win, line++, 2, " t       Move to… (pick destination directory)");
    mvwprintw(help_win, line++, 2, " C       Copy to… (pick destination directory)");
    mvwprintw(help_win, line++, 2, " p       Paste current selected items");
    mvwprintw(help_win, line++, 2, " d       Delete current selected items");
    mvwprintw(help_win, line++, 2, " D       Delete-search mode (Tab mark, Enter delete)");
    mvwprintw(help_win, line++, 2, " n       Create Folder");
    mvwprintw(help_win, line++, 2, " r       Rename selected items");
    mvwprintw(help_win, line++, 2, " +       Add bookmark (It will ask to bind a key to this bookmark)");
    mvwprintw(help_win, line++, 2, " - + key Remove bookmark");
    mvwprintw(help_win, line++, 2, " = + key Open bookmark ");
    mvwprintw(help_win, line++, 2, " /       Fuzzy search (left list + right preview)");
    mvwprintw(help_win, line++, 2, " J       Jump to (/home /etc /usr directories)");
    mvwprintw(help_win, line++, 2, " i       Toggle file info in preview panel");
    mvwprintw(help_win, line++, 2, " K       Scroll preview pane up");
    mvwprintw(help_win, line++, 2, " [ / ]   Page up/down in preview pane");
    mvwprintw(help_win, line++, 2, " ?       Show this help");
    mvwprintw(help_win, line++, 2, " q       Quit (shell cd via cfl.sh wrapper)");
    // Future: use an array of {char key; const char* desc;} and loop

    mvwprintw(help_win, rows-2, 2, "Press any key to close");

    wrefresh(help_win);

    // Wait for any keypress
    wgetch(help_win);

    ui_modal_destroy(help_win);
}

/* Write last browsed directory so a shell wrapper can cd after quit.
 * Path: $CFL_LASTDIR if set, else $cache_path/lastdir. */
static void save_lastdir(void) {
	struct stat st;
	if (dir[0] == '\0' || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return;

	char pathbuf[PATH_MAX];
	const char *out = getenv("CFL_LASTDIR");
	if (!out || !out[0]) {
		if (!cache_path)
			return;
		if (snprintf(pathbuf, sizeof pathbuf, "%s/lastdir", cache_path) >= (int)sizeof pathbuf)
			return;
		out = pathbuf;
	}

	FILE *f = fopen(out, "w");
	if (!f)
		return;
	fprintf(f, "%s\n", dir);
	fclose(f);
}

void WrappeUp(void) {
	/* Leave path for shell wrapper: cd to last dir after q */
	save_lastdir();

	/* Drop session selection so nothing persists across restarts */
	if (clipboard_path)
		remove(clipboard_path);
	if (temp_clipboard_path)
		remove(temp_clipboard_path);
	free(cache_path);
	free(temp_clipboard_path);
	free(clipboard_path);
	free(bookmarks_path);
	free(trash_path);
	free(editor);
	free(shell);
	free(last);
    preview_shutdown();
	endwin();
}

/* ============================================================
 * File Info (toggle: right preview panel, not a modal window)
 * ============================================================ */
static const char *human_size(off_t bytes, char *buf, size_t bufsz) {
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

static unsigned info_gcd_u(unsigned a, unsigned b) {
    while (b) {
        unsigned t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

static void info_format_proportion(unsigned w, unsigned h, char *buf, size_t bufsz) {
    if (w == 0 || h == 0) {
        snprintf(buf, bufsz, "—");
        return;
    }
    unsigned g = info_gcd_u(w, h);
    unsigned rw = w / g, rh = h / g;
    double ratio = (double)w / (double)h;
    snprintf(buf, bufsz, "%u:%u (%.3f)", rw, rh, ratio);
}

/* Run mediainfo with a single --Inform=template argv (required for this CLI).
 * Template fields should be separated by '|' for easy parsing. Returns 0 on success. */
static int info_mediainfo_inform(const char *filepath, const char *inform_eq_arg,
                                 char *out, size_t outsz) {
    if (!filepath || !inform_eq_arg || !out || outsz == 0)
        return -1;
    out[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        /* Must be one argv: --Inform=Stream;fields — separate --Inform breaks the template */
        execlp("mediainfo", "mediainfo", inform_eq_arg, filepath, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    size_t got = fread(out, 1, outsz - 1, fp);
    out[got] = '\0';
    fclose(fp);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || got == 0)
        return -1;

    while (got > 0 && (out[got - 1] == '\n' || out[got - 1] == '\r' ||
                       out[got - 1] == ' ' || out[got - 1] == '\t'))
        out[--got] = '\0';
    return 0;
}

static int info_parse_u(const char *s, unsigned *v) {
    if (!s || !*s || !v) return -1;
    char *end = NULL;
    unsigned long x = strtoul(s, &end, 10);
    if (end == s) return -1;
    *v = (unsigned)x;
    return 0;
}

/* Split mediainfo '|' (or newline) delimited Inform output (mutates buf). */
static int info_split_fields(char *buf, char **fields, int max_fields) {
    int n = 0;
    char *p = buf;
    while (n < max_fields && p) {
        fields[n++] = p;
        char *sep = p;
        while (*sep && *sep != '|' && *sep != '\n' && *sep != '\r')
            sep++;
        if (!*sep) break;
        *sep = '\0';
        p = sep + 1;
        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p) break;
    }
    return n;
}

static void info_line(WINDOW *win, int *y, int rows, int cols, const char *fmt, ...) {
    if (!win || !y || *y >= rows) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    int safe = cols - 3;
    if (safe < 1) safe = 1;
    if ((int)strlen(line) > safe)
        line[safe] = '\0';
    mvwprintw(win, (*y)++, 2, "%s", line);
}

/* Draw file info into preview_win; stops ueberzug. Safe to call every frame. */
void show_file_info(const char *fullpath) {
    if (!preview_win || !fullpath) return;

    ueberzugpp_remove();
    clearFlagImg = 1;
    preview_scroll = 0;
    len_preview = -1;

    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    wclear(preview_win);

    int y = 0;
    wattron(preview_win, A_BOLD);
    info_line(preview_win, &y, rows, cols, " File Info ");
    wattroff(preview_win, A_BOLD);
    y++;

    struct stat st;
    /* lstat so we report symlinks correctly */
    if (lstat(fullpath, &st) != 0) {
        info_line(preview_win, &y, rows, cols, "Cannot stat file");
        wrefresh(preview_win);
        return;
    }

    const char *base = strrchr(fullpath, '/');
    base = base ? base + 1 : fullpath;

    char hsize[32];
    human_size(st.st_size, hsize, sizeof hsize);

    char mtime_str[64];
    struct tm *tm = localtime(&st.st_mtime);
    if (tm)
        strftime(mtime_str, sizeof mtime_str, "%Y-%m-%d %H:%M:%S", tm);
    else
        snprintf(mtime_str, sizeof mtime_str, "?");

    info_line(preview_win, &y, rows, cols, "Name: %s", base);

    if ((int)strlen(fullpath) > cols - 10 && cols > 14)
        info_line(preview_win, &y, rows, cols, "Path: ...%s",
                  fullpath + strlen(fullpath) - (cols - 14));
    else
        info_line(preview_win, &y, rows, cols, "Path: %s", fullpath);

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        if (readSymlinkTarget(fullpath, target, sizeof target) == 0)
            info_line(preview_win, &y, rows, cols, "Type: Symlink → %s", target);
        else
            info_line(preview_win, &y, rows, cols, "Type: Symlink (broken)");
    } else if (S_ISDIR(st.st_mode)) {
        info_line(preview_win, &y, rows, cols, "Type: Directory");
    } else if (S_ISREG(st.st_mode)) {
        info_line(preview_win, &y, rows, cols, "Type: Regular file");
    } else {
        info_line(preview_win, &y, rows, cols, "Type: Other");
    }

    info_line(preview_win, &y, rows, cols, "Size: %s (%lld bytes)", hsize,
              (long long)st.st_size);

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
    info_line(preview_win, &y, rows, cols, "Perms: %s  (0%o)", perms, st.st_mode & 07777);
    info_line(preview_win, &y, rows, cols, "Modified: %s", mtime_str);

    {
        struct passwd *pw = getpwuid(st.st_uid);
        info_line(preview_win, &y, rows, cols, "Owner: %s (%u)",
                  pw && pw->pw_name ? pw->pw_name : "?", (unsigned)st.st_uid);
    }

    /* Media details for regular files (follow symlink to target when possible) */
    char mime[128] = {0};
    const char *media_path = fullpath;
    char resolved[PATH_MAX];

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        if (readSymlinkTarget(fullpath, target, sizeof target) == 0) {
            if (target[0] == '/') {
                snprintf(resolved, sizeof resolved, "%s", target);
            } else {
                char parent[PATH_MAX];
                snprintf(parent, sizeof parent, "%s", fullpath);
                char *slash = strrchr(parent, '/');
                if (slash) *slash = '\0';
                if (build_path(resolved, sizeof resolved, parent, target) != 0)
                    resolved[0] = '\0';
            }
            struct stat rst;
            if (resolved[0] && stat(resolved, &rst) == 0 && S_ISREG(rst.st_mode))
                media_path = resolved;
        }
    }

    int probe = 0;
    if (S_ISREG(st.st_mode)) {
        probe = 1;
        media_path = fullpath;
    } else if (S_ISLNK(st.st_mode) && media_path != fullpath) {
        probe = 1;
    }

    if (probe && getMIME(media_path, mime) == 0) {
        info_line(preview_win, &y, rows, cols, "MIME: %s", mime);

        int is_image = (strncmp(mime, "image/", 6) == 0);
        int is_video = (strncmp(mime, "video/", 6) == 0);

        /* Containers sometimes classified under application/ */
        if (!is_video && (strstr(mime, "mp4") || strstr(mime, "webm") ||
                          strstr(mime, "matroska") || strstr(mime, "x-msvideo") ||
                          strstr(mime, "quicktime") || strstr(mime, "x-matroska")))
            is_video = 1;

        if (is_image || is_video) {
            y++;
            wattron(preview_win, A_BOLD);
            info_line(preview_win, &y, rows, cols, is_image ? " Image " : " Video ");
            wattroff(preview_win, A_BOLD);

            static char cache_path[PATH_MAX];
            static char cache_blob[512];
            static int cache_ok = 0;
            static int cache_is_video = 0;

            const char *inform = is_video
                ? "--Inform=Video;%Width%|%Height%|%FrameRate%|%Format%|%Duration/String3%"
                : "--Inform=Image;%Width%|%Height%";

            char blob[512];
            int ok = 0;
            if (cache_ok && cache_is_video == is_video &&
                strcmp(cache_path, media_path) == 0) {
                snprintf(blob, sizeof blob, "%s", cache_blob);
                ok = 1;
            } else if (info_mediainfo_inform(media_path, inform, blob, sizeof blob) == 0) {
                snprintf(cache_path, sizeof cache_path, "%s", media_path);
                snprintf(cache_blob, sizeof cache_blob, "%s", blob);
                cache_ok = 1;
                cache_is_video = is_video;
                ok = 1;
            } else {
                cache_ok = 0;
            }

            if (!ok) {
                info_line(preview_win, &y, rows, cols, "Media details: unavailable (mediainfo)");
            } else {
                char *fields[8] = {0};
                int nfields = info_split_fields(blob, fields, 8);
                unsigned width = 0, height = 0;
                if (nfields >= 1) info_parse_u(fields[0], &width);
                if (nfields >= 2) info_parse_u(fields[1], &height);

                if (width && height) {
                    info_line(preview_win, &y, rows, cols, "Resolution: %ux%u", width, height);
                    char prop[64];
                    info_format_proportion(width, height, prop, sizeof prop);
                    info_line(preview_win, &y, rows, cols, "Proportion: %s", prop);
                } else {
                    info_line(preview_win, &y, rows, cols, "Resolution: (unknown)");
                }

                if (is_video) {
                    const char *fps = (nfields >= 3 && fields[2] && fields[2][0]) ? fields[2] : "—";
                    const char *codec = (nfields >= 4 && fields[3] && fields[3][0]) ? fields[3] : "—";
                    const char *dur = (nfields >= 5 && fields[4] && fields[4][0]) ? fields[4] : "—";
                    info_line(preview_win, &y, rows, cols, "FPS: %s", fps);
                    info_line(preview_win, &y, rows, cols, "Codec: %s", codec);
                    info_line(preview_win, &y, rows, cols, "Duration: %s", dur);
                    info_line(preview_win, &y, rows, cols, "Size: %s (%lld bytes)", hsize,
                              (long long)st.st_size);
                }
            }
        }
    }

    if (rows > 2) {
        wattron(preview_win, A_DIM);
        mvwprintw(preview_win, rows - 1, 2, "i = toggle file info");
        wattroff(preview_win, A_DIM);
    }
    wrefresh(preview_win);
}
