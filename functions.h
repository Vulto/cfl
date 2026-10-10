/* Wide-char ncurses (wget_wch) must be enabled before <ncurses.h> */
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
#include <limits.h>
#include <stdint.h>

#include "config.h"
#include "cfl_core.h"
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
    return CflBuildPath(dst, dstsz, base, entry);
}

// Ueberzug++ helpers (declared here because functions.h is included before their definitions)
void openFile(char *filepath);
void getLastToken(char *tokenizer);
void displayAlert(char *message);
int isRegularFile(const char *path);
int isDirectory(const char *path);

#include "bookmark.h"
#include "preview.h"

/* Forward decls needed by search.h / delete_search.h / jump_to.h (implemented below) */
int getFiles(char *directory, char ***target);
void sortFiles(char **files, int count, const char *directory);
int getWritePermissions(char *path);
int getParentWritePermissions(const char *path);
void moveFiles(char *present_dir);
void copyFiles(char *present_dir);
void trashFiles(void);

#include "search.h"
#include "delete_search.h"
#include "jump_to.h"

int getFiles(char *directory, char ***target) {
    int count = 0;
    int capacity = 16;
    DIR *pDir;
    struct dirent *pDirent;
    char **result;

    if (target == NULL || directory == NULL)
        return -1;
    *target = NULL;

    pDir = opendir(directory);
    if (pDir == NULL)
        return -1;

    result = malloc((size_t)capacity * sizeof *result);
    if (result == NULL) {
        closedir(pDir);
        displayAlert("Couldn't allocate memory!");
        return -1;
    }

    while (1) {
        errno = 0;
        pDirent = readdir(pDir);
        if (pDirent == NULL) {
            if (errno != 0)
                goto fail;
            break;
        }

        const char *name = pDirent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (name[0] == '.' && hiddenFlag == 0)
            continue;

        if (count == capacity) {
            if (capacity > INT_MAX / 2 ||
                (size_t)capacity > SIZE_MAX / (2 * sizeof *result)) {
                errno = EOVERFLOW;
                goto fail;
            }

            int newCapacity = capacity * 2;
            char **grown = realloc(result, (size_t)newCapacity * sizeof *result);
            if (grown == NULL)
                goto fail;
            result = grown;
            capacity = newCapacity;
        }

        result[count] = strdup(name);
        if (result[count] == NULL)
            goto fail;
        ++count;
    }

    if (closedir(pDir) != 0) {
        for (int j = 0; j < count; ++j)
            free(result[j]);
        free(result);
        return -1;
    }

    *target = result;
    return count;

fail:
    {
        int savedErrno = errno;
        closedir(pDir);
        for (int j = 0; j < count; ++j)
            free(result[j]);
        free(result);
        errno = savedErrno;
        displayAlert("Unable to read directory entries");
        return -1;
    }
}

typedef struct {
    char *name;
    unsigned char isDirectory;
} CflSortEntry;

static int CflCompareSortEntries(const void *left, const void *right) {
    const CflSortEntry *a = left;
    const CflSortEntry *b = right;
    if (a->isDirectory != b->isDirectory)
        return (int)b->isDirectory - (int)a->isDirectory;
    return strcasecmp(a->name, b->name);
}

/* Probe each entry once instead of issuing stat() from every qsort comparison. */
void sortFiles(char **files, int count, const char *directory) {
    if (files == NULL || directory == NULL || count < 2)
        return;
    if ((size_t)count > SIZE_MAX / sizeof(CflSortEntry)) {
        displayAlert("Directory is too large to sort");
        return;
    }

    CflSortEntry *entries = malloc((size_t)count * sizeof *entries);
    if (entries == NULL) {
        displayAlert("Couldn't allocate sorting memory");
        return;
    }

    for (int i = 0; i < count; ++i) {
        char fullPath[PATH_MAX];
        entries[i].name = files[i];
        entries[i].isDirectory =
            build_path(fullPath, sizeof fullPath, directory, files[i]) == 0 &&
            isDirectory(fullPath);
    }

    qsort(entries, (size_t)count, sizeof *entries, CflCompareSortEntries);
    for (int i = 0; i < count; ++i)
        files[i] = entries[i].name;
    free(entries);
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
	int content_left, panel_top, panel_height, sep_col;
	ui_get_layout(&content_left, &panel_top, &panel_height, &sep_col);

	int margin = UI_MARGIN;
	int content_w = COLS - 2 * margin;
	int left_w  = (content_w * 2) / 5 + 2;
	int right_w = content_w - left_w - 1;

	status_win = newwin(2, content_w, margin, content_left);

	starty = panel_top;
	preview_start = sep_col;

	current_win = newwin(panel_height, left_w,  starty, content_left);
	preview_win = newwin(panel_height, right_w, starty, sep_col + 1);

	keypad(current_win, TRUE);
	sigprocmask(SIG_UNBLOCK, &x, NULL);

	ui_clear_gaps();
}

int isRegularFile(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return S_ISREG(st.st_mode);
}

int isDirectory(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
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
    if (fp == NULL) {
        selectedFiles = 0;
        return;
    }

    char buf[PATH_MAX * 3 + 1];
    int num = 0;
    int readResult;
    while ((readResult = CflReadLine(fp, buf, sizeof buf)) > 0)
        ++num;
    if (readResult < 0)
        displayAlert("Selection file contains an invalid record");
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
			snprintf(temp, sizeof(temp), "%s/%s", dir, arg);
			snprintf(dir, sizeof(dir), "%s", temp);
		}
	} else if(argc > 2) {
		printf("Incorrect Usage!\n");
		exit(1);
	}
	/* Selection starts empty each session; never restore a previous list */
	selectedFiles = 0;
}

int fileExists(char *file) {
    return (access(file, F_OK) != -1) ? EXIT_FAILURE : EXIT_SUCCESS;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
    struct stat snapshot;
    int valid;
} CflSelectionCacheState;

static CflSelectionCacheState CflSelectionCache = {0};

static void CflSelectionCacheClear(void) {
    for (size_t i = 0; i < CflSelectionCache.count; ++i)
        free(CflSelectionCache.items[i]);
    free(CflSelectionCache.items);
    CflSelectionCache.items = NULL;
    CflSelectionCache.count = 0;
    CflSelectionCache.capacity = 0;
    CflSelectionCache.valid = 0;
}

static int CflSelectionStatEqual(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev &&
           a->st_ino == b->st_ino &&
           a->st_size == b->st_size &&
           a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
           a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

static int CflSelectionCacheAppend(const char *item) {
    if (CflSelectionCache.count == CflSelectionCache.capacity) {
        size_t newCapacity = CflSelectionCache.capacity ?
            CflSelectionCache.capacity * 2 : 16;
        if (newCapacity < CflSelectionCache.capacity ||
            newCapacity > SIZE_MAX / sizeof *CflSelectionCache.items)
            return -1;
        char **grown = realloc(CflSelectionCache.items,
                               newCapacity * sizeof *CflSelectionCache.items);
        if (grown == NULL)
            return -1;
        CflSelectionCache.items = grown;
        CflSelectionCache.capacity = newCapacity;
    }

    char *copy = strdup(item);
    if (copy == NULL)
        return -1;
    CflSelectionCache.items[CflSelectionCache.count++] = copy;
    return 0;
}

static int CflCompareSelectionStrings(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static int CflSelectionCacheLoad(void) {
    struct stat before;
    if (stat(clipboard_path, &before) != 0) {
        CflSelectionCacheClear();
        return errno == ENOENT ? 0 : -1;
    }

    if (CflSelectionCache.valid &&
        CflSelectionStatEqual(&before, &CflSelectionCache.snapshot))
        return 1;

    CflSelectionCacheClear();
    FILE *stream = fopen(clipboard_path, "r");
    if (stream == NULL)
        return -1;

    char line[PATH_MAX * 3 + 1];
    int readResult;
    while ((readResult = CflReadLine(stream, line, sizeof line)) > 0) {
        if (CflSelectionCacheAppend(line) != 0) {
            fclose(stream);
            CflSelectionCacheClear();
            return -1;
        }
    }

    int readFailed = readResult < 0 || ferror(stream);
    if (fclose(stream) != 0)
        readFailed = 1;
    if (readFailed) {
        CflSelectionCacheClear();
        return -1;
    }

    if (CflSelectionCache.count > 1)
        qsort(CflSelectionCache.items, CflSelectionCache.count,
              sizeof *CflSelectionCache.items, CflCompareSelectionStrings);

    struct stat after;
    if (stat(clipboard_path, &after) != 0 ||
        !CflSelectionStatEqual(&before, &after)) {
        CflSelectionCacheClear();
        return -1;
    }

    CflSelectionCache.snapshot = after;
    CflSelectionCache.valid = 1;
    return 1;
}

static int CflSelectionCacheContains(const char *item) {
    size_t low = 0;
    size_t high = CflSelectionCache.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(item, CflSelectionCache.items[middle]);
        if (order == 0)
            return 1;
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return 0;
}

static void CflSelectionCacheRecordAppend(const char *item) {
    if (!CflSelectionCache.valid)
        return;

    size_t low = 0;
    size_t high = CflSelectionCache.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(item, CflSelectionCache.items[middle]);
        if (order == 0)
            goto updateSnapshot;
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }

    if (CflSelectionCache.count == CflSelectionCache.capacity) {
        size_t newCapacity = CflSelectionCache.capacity ?
            CflSelectionCache.capacity * 2 : 16;
        if (newCapacity < CflSelectionCache.capacity ||
            newCapacity > SIZE_MAX / sizeof *CflSelectionCache.items)
            goto invalidate;
        char **grown = realloc(CflSelectionCache.items,
                               newCapacity * sizeof *CflSelectionCache.items);
        if (grown == NULL)
            goto invalidate;
        CflSelectionCache.items = grown;
        CflSelectionCache.capacity = newCapacity;
    }

    char *copy = strdup(item);
    if (copy == NULL)
        goto invalidate;
    memmove(&CflSelectionCache.items[low + 1],
            &CflSelectionCache.items[low],
            (CflSelectionCache.count - low) * sizeof *CflSelectionCache.items);
    CflSelectionCache.items[low] = copy;
    CflSelectionCache.count++;

updateSnapshot:
    if (stat(clipboard_path, &CflSelectionCache.snapshot) == 0)
        return;
invalidate:
    CflSelectionCacheClear();
}

int checkClipboard(char *filepath) {
    if (filepath == NULL || clipboard_path == NULL)
        return 0;

    char encoded[PATH_MAX * 3 + 1];
    if (CflEncodePath(filepath, encoded, sizeof encoded) != 0)
        return 0;

    int loaded = CflSelectionCacheLoad();
    if (loaded <= 0)
        return 0;
    return CflSelectionCacheContains(encoded);
}

void writeClipboard(char *filepath) {
    if (filepath == NULL || clipboard_path == NULL)
        return;
    FILE *f = fopen(clipboard_path, "a");
    if (f == NULL) {
        displayAlert("Error accessing selection file");
        return;
    }

    int failed = fprintf(f, "%s\n", filepath) < 0;
    if (fclose(f) != 0)
        failed = 1;
    if (failed) {
        CflSelectionCacheClear();
        displayAlert("Error writing selection file");
        return;
    }
    CflSelectionCacheRecordAppend(filepath);
}

void removeClipboard(char *filepath) {
    if (filepath == NULL || clipboard_path == NULL || temp_clipboard_path == NULL)
        return;
    FILE *f1 = fopen(clipboard_path, "r");
    if (f1 == NULL) {
        displayAlert("Couldn't open selection file");
        return;
    }

    FILE *f2 = fopen(temp_clipboard_path, "w");
    if (f2 == NULL) {
        fclose(f1);
        displayAlert("Couldn't create temporary selection file");
        return;
    }

    char buf[PATH_MAX * 3 + 1];
    int failed = 0;
    int readResult;
    while ((readResult = CflReadLine(f1, buf, sizeof buf)) > 0) {
        if (strcmp(buf, filepath) != 0 && fprintf(f2, "%s\n", buf) < 0) {
            failed = 1;
            break;
        }
    }
    if (readResult < 0 || ferror(f1))
        failed = 1;
    if (fclose(f2) != 0)
        failed = 1;
    fclose(f1);

    if (failed) {
        remove(temp_clipboard_path);
        displayAlert("Couldn't update selection file");
        return;
    }

    if (rename(temp_clipboard_path, clipboard_path) != 0) {
        remove(temp_clipboard_path);
        displayAlert("Couldn't replace selection file");
    } else {
        CflSelectionCacheClear();
    }
}

void clearClipboard(void) {
    if (clipboard_path)
        remove(clipboard_path);
    CflSelectionCacheClear();
    selectedFiles = 0;
}

/* Mark every entry in the current directory listing (Shift+V). */
void selectAllFiles(char **directories, int n) {
	if (!directories || n <= 0)
		return;
	for (int i = 0; i < n; i++) {
		if (!directories[i])
			continue;
		char path[PATH_MAX];
		if (build_path(path, sizeof path, dir, directories[i]) != 0)
			continue;
		if (checkClipboard(path) == 1)
			continue;
        char encoded[PATH_MAX * 3 + 1];
        if (CflEncodePath(path, encoded, sizeof encoded) != 0)
            continue;
        writeClipboard(encoded);
	}
	setSelectionCount();
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

int getParentWritePermissions(const char *path) {
    if (path == NULL || path[0] == '\0')
        return EXIT_SUCCESS;

    char parent[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof parent)
        return EXIT_SUCCESS;
    memcpy(parent, path, len + 1);

    char *slash = strrchr(parent, '/');
    if (slash == NULL) {
        memcpy(parent, ".", 2);
    } else if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }

    return access(parent, W_OK | X_OK) == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int cfl_wait_child(pid_t pid) {
    int status;
    pid_t waited;

    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0)
        return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return 0;
}

void copyFiles(char *present_dir) {
    FILE *f = fopen(clipboard_path, "r");
    char buf[PATH_MAX * 3 + 1];
    int flag;
    int failed = 0;

    if (f == NULL)
        return;

    flag = getWritePermissions(present_dir) == 1 ? 0 : 1;
    endwin();

    int readResult;
    while ((readResult = CflReadLine(f, buf, sizeof buf)) > 0) {
        if (buf[0] == '\0')
            continue;
        CflDecodePath(buf);

        pid_t pid = fork();
        if (pid == 0) {
            if (flag == 0)
                execlp("cp", "cp", "-dirv", "--", buf, present_dir, (char *)0);
            else
                execlp("sudo", "sudo", "cp", "-dirv", "--", buf, present_dir, (char *)0);
            _exit(127);
        }
        if (pid < 0 || cfl_wait_child(pid) != 0)
            failed = 1;
    }
    if (readResult < 0)
        failed = 1;

    fclose(f);
    refresh();
    if (!failed)
        clearClipboard();
    else
        displayAlert("One or more copy operations failed; selection retained");
}

void removeFiles(void) {
    FILE *f = fopen(clipboard_path, "r");
    char buf[PATH_MAX * 3 + 1];
    int failed = 0;

    if (f == NULL)
        return;

    endwin();

    int readResult;
    while ((readResult = CflReadLine(f, buf, sizeof buf)) > 0) {
        if (buf[0] == '\0')
            continue;
        CflDecodePath(buf);

        int flag = getParentWritePermissions(buf) == 1 ? 0 : 1;
        pid_t pid = fork();
        if (pid == 0) {
            if (flag == 0)
                execlp("rm", "rm", "-rfv", "--", buf, (char *)0);
            else
                execlp("sudo", "sudo", "rm", "-rfv", "--", buf, (char *)0);
            _exit(127);
        }
        if (pid < 0 || cfl_wait_child(pid) != 0)
            failed = 1;
    }
    if (readResult < 0)
        failed = 1;

    fclose(f);
    refresh();
    if (!failed)
        clearClipboard();
    else
        displayAlert("One or more delete operations failed; selection retained");
}

void renameFiles(char **directories){
	if( access( clipboard_path, F_OK ) == -1 ){
		snprintf(temp_dir, sizeof(temp_dir), "%s/%s", dir, directories[selection]);
        char encoded[PATH_MAX * 3 + 1];
        if (CflEncodePath(temp_dir, encoded, sizeof encoded) != 0) {
            displayAlert("Couldn't encode selected path");
            return;
        }
        writeClipboard(encoded);
	}
	pid_t pid;

	FILE *f = fopen(clipboard_path, "r");
	FILE *f2;

	char *cmd = NULL;

	char buf[PATH_MAX * 3 + 1];
	char buf2[PATH_MAX * 3 + 1];

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

    int failed = 0;
    int readResult;
    while ((readResult = CflReadLine(f, buf, sizeof buf)) > 0) {
        count2 = -1;
        f2 = fopen(temp_clipboard_path, "r");
        if (f2 == NULL) {
            displayAlert("Couldn't reopen temporary selection file");
            failed = 1;
            break;
        }
        int readResult2;
        while ((readResult2 = CflReadLine(f2, buf2, sizeof buf2)) > 0) {
            count2++;
            if (count2 == count) {
                CflDecodePath(buf2);
                CflDecodePath(buf);

                pid = fork();
                if (pid == 0) {
                    execlp("mv", "mv", "-ivn", "--", buf, buf2, (char *)0);
                    _exit(127);
                } else if (pid < 0 || cfl_wait_child(pid) != 0) {
                    displayAlert("Rename operation failed");
                    failed = 1;
                }
            }
        }
        if (readResult2 < 0) {
            displayAlert("Invalid line in temporary selection file");
            failed = 1;
        }
        count++;
        fclose(f2);
    }
    if (readResult < 0) {
        displayAlert("Invalid line in selection file");
        failed = 1;
    }
    fclose(f);

    if (!failed)
        clearClipboard();
    else
        displayAlert("Some rename operations failed; selection retained");
    remove(temp_clipboard_path);

	refresh();
}

void moveFiles(char *present_dir) {
    FILE *f = fopen(clipboard_path, "r");
    char buf[PATH_MAX * 3 + 1];
    char dest_path[PATH_MAX];
    int failed = 0;

    if (f == NULL) {
        perror("Failed to open selection file");
        return;
    }

    endwin();
    int readResult;
    while ((readResult = CflReadLine(f, buf, sizeof buf)) > 0) {
        if (buf[0] == '\0')
            continue;
        CflDecodePath(buf);

        const char *base = strrchr(buf, '/');
        base = base ? base + 1 : buf;
        if (build_path(dest_path, sizeof dest_path, present_dir, base) != 0) {
            fprintf(stderr, "Destination path too long: %s\n", base);
            failed = 1;
            continue;
        }

        struct stat destinationStat;
        if (lstat(dest_path, &destinationStat) == 0) {
            fprintf(stderr, "Destination already exists: %s\n", dest_path);
            failed = 1;
            continue;
        }
        if (errno != ENOENT) {
            perror("Cannot inspect destination");
            failed = 1;
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execlp("mv", "mv", "-n", "--", buf, dest_path, (char *)0);
            _exit(127);
        }
        if (pid < 0 || cfl_wait_child(pid) != 0) {
            perror("Error moving file");
            failed = 1;
            continue;
        }

        struct stat sourceStat;
        if (lstat(buf, &sourceStat) == 0) {
            fprintf(stderr, "Move skipped or failed (destination may exist): %s\n", base);
            failed = 1;
        } else if (errno != ENOENT) {
            perror("Cannot verify moved source");
            failed = 1;
        } else {
            printf("Moved: %s -> %s\n", buf, dest_path);
        }
    }
    if (readResult < 0)
        failed = 1;

    fclose(f);
    refresh();
    if (!failed)
        clearClipboard();
    else
        displayAlert("One or more move operations failed; selection retained");
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
	char buf[PATH_MAX * 3 + 1];
	int readResult;
	while ((readResult = CflReadLine(f, buf, sizeof buf)) > 0) {
		CflDecodePath(buf);
		if (trash_one(buf) == 0) {
			printf("Trashed: %s\n", buf);
			ok++;
		} else {
			perror(buf);
			fail++;
		}
    }
    if (readResult < 0)
        fail++;
    fclose(f);
    refresh();
    if (fail == 0 && ok > 0)
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
	snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, dirName);
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
    ueberzugpp_remove();
	ueberzugpp_stop();
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
