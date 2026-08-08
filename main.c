/* Wide-char ncurses */
#ifndef NCURSES_WIDECHAR
#define NCURSES_WIDECHAR 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ncurses.h>
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
int allocSize;

char next_dir[PATH_MAX];
char prev_dir[PATH_MAX];
char temp_dir[PATH_MAX];
char sort_dir[PATH_MAX];

char *editor = NULL;
char *shell = NULL;
struct passwd *info;
char *cache_path = NULL;
char *clipboard_path = NULL;
char *temp_clipboard_path = NULL;
char *trash_path = NULL;
char *last = NULL;

#include "preview.h"
#include "search.h"
#include "mjump.h"

void initWindows(void) {
	int content_left, panel_top, panel_height, sep_col;
	ui_get_layout(&content_left, &panel_top, &panel_height, &sep_col);

	int margin = UI_MARGIN;
	int content_w = COLS - 2 * margin;
	int left_w  = (content_w * 2) / 5 + 2;
	/*
	 * List window ends with: … | blank | bar | blank (see drawScrollbar).
	 * Preview starts on the next column (sep_col) so that trailing list blank
	 * is the single gap before preview content — no second mid-pane gap.
	 */
	int right_w = COLS - sep_col;
	if (right_w < 4) right_w = 4;

	status_win = newwin(2, content_w, margin, content_left);

	starty = panel_top;
	preview_start = sep_col;

	current_win = newwin(panel_height, left_w,  starty, content_left);
	preview_win = newwin(panel_height, right_w, starty, sep_col);

	keypad(current_win, TRUE);
	sigprocmask(SIG_UNBLOCK, &x, NULL);

	ui_clear_gaps();
}


void init(int argc, char* argv[]) {
	sigemptyset (&x);
	sigaddset(&x, SIGWINCH);

	setlocale(LC_ALL, "");

	uid_t uid = getuid();
	info = getpwuid(uid);

	if( getenv("SHELL") == NULL) {
		shell = malloc(10);
		if(shell == NULL) {
			printf("%s\n", "Couldn't initialize shell");
			exit(1);
		}
		snprintf(shell, 10, "%s", "/bin/bash");
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
		editor = malloc(4);
		if(editor == NULL) {
			printf("%s\n", "Couldn't initialize editor");
			exit(1);
		}
		snprintf(editor, 4, "%s", "nvim");
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

	start_jump_to = 0;
	const char *path_arg = NULL;
	for (int a = 1; a < argc; a++) {
		const char *arg = argv[a];
		if (!arg || !arg[0])
			continue;
		if (strcmp(arg, "-j") == 0) {
			start_jump_to = 1;
			continue;
		}
		if (arg[0] == '-') {
			printf("Usage: %s [-j] [directory]\n", argv[0] ? argv[0] : "cfl");
			printf("  -j    start in jump-to mode ($HOME recursive)\n");
			exit(1);
		}
		if (path_arg) {
			printf("Usage: %s [-j] [directory]\n", argv[0] ? argv[0] : "cfl");
			exit(1);
		}
		path_arg = arg;
	}

	if (path_arg) {
		char arg[PATH_MAX];
		snprintf(arg, sizeof(arg), "%s", path_arg);
		if (arg[0] == '/') {
			if (strlen(arg) > 1 && arg[strlen(arg) - 1] == '/')
				arg[strlen(arg) - 1] = '\0';
			snprintf(dir, sizeof(dir), "%s", arg);
		} else {
			char temp[PATH_MAX];
			snprintf(temp, sizeof(temp), "%s/%s", dir, arg);
			snprintf(dir, sizeof(dir), "%s", temp);
		}
	}

	selectedFiles = 0;
}


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
	free(trash_path);
	free(editor);
	free(shell);
	free(last);
    ueberzugpp_remove();
	ueberzugpp_stop();
	endwin();
}


int main(int argc, char *argv[])
{
	init(argc, argv);
	cursesInit();
	getmaxyx(stdscr, maxy, maxx);
	maxy = LINES - 2 * UI_MARGIN - 2;
	initWindows();
	set_mode(MODE_BROWSE);
	run_mcfl_session();
	WrappeUp();
	return EXIT_SUCCESS;
}
