// bookmark.c

#include "common.h"

// Helper function to allocate memory and check for errors
char *AllocateBuffer(size_t size) {
    char *buf = malloc(size);
    if (buf == NULL) {
        endwin();
        printf("Couldn't allocate memory!\n");
        exit(1);
    }
    return buf;
}

// Helper function to open a file and check for errors
FILE *OpenFile(const char *path, const char *mode) {
    FILE *fp = fopen(path, mode);
    if (fp == NULL) {
        endwin();
        printf("Couldn't Open Bookmarks File!\n");
        exit(1);
    }
    return fp;
}

// Helper function to read a line from file
char *ReadLine(FILE *fp, char *buf, size_t size) {
    return fgets(buf, size, fp);
}

int GetNumberOfBookmarks(void) {
    FILE *fp = OpenFile(bookmarks_path, "r");
    char *buf = AllocateBuffer(PATH_MAX);
    int num = 0;
    while (ReadLine(fp, buf, PATH_MAX)) {
        num++;
    }
    free(buf);
    fclose(fp);
    return num;
}

void DisplayBookmarks(void) {
    FILE *fp = OpenFile(bookmarks_path, "r");
    char *buf = AllocateBuffer(PATH_MAX);
    wprintw(keys_win, "Key\tPath\n");
    while (ReadLine(fp, buf, PATH_MAX)) {
        wprintw(keys_win, "%c", buf[0]);
        
        free(temp_dir);
        allocSize = snprintf(NULL, 0, "%s", buf);
        temp_dir = AllocateBuffer(allocSize + 1);

        strncpy(temp_dir, buf + 2, strlen(buf) - 2);
        strtok(temp_dir, "\n");
        wprintw(keys_win, "\t%s\n", temp_dir);
    }
    free(buf);
    fclose(fp);
}

void OpenBookmarkDir(char secondKey) {
    FILE *fp = OpenFile(bookmarks_path, "r");
    char *buf = AllocateBuffer(PATH_MAX);
    while (ReadLine(fp, buf, PATH_MAX)) {
        if (buf[0] == secondKey) {
            free(temp_dir);
            allocSize = snprintf(NULL, 0, "%s", buf);
            temp_dir = AllocateBuffer(allocSize + 1);
            strncpy(temp_dir, buf + 2, strlen(buf) - 2);
            strtok(temp_dir, "\n");
            replace(temp_dir, "//", "\n");
            if (fileExists(temp_dir) == 1) {
                free(dir);
                allocSize = snprintf(NULL, 0, "%s", temp_dir);
                dir = AllocateBuffer(allocSize + 1);
                snprintf(dir, allocSize + 1, "%s", temp_dir);
            }
            start = 0;
            selection = 0;
            break;
        }
    }
    free(buf);
    fclose(fp);
}

int BookmarkExists(char bookmark) {
    FILE *fp = OpenFile(bookmarks_path, "r");
    char *buf = AllocateBuffer(PATH_MAX);
    while (ReadLine(fp, buf, PATH_MAX)) {
        if (buf[0] == bookmark) {
            free(buf);
            fclose(fp);
            return 1;
        }
    }
    free(buf);
    fclose(fp);
    return 0;
}

void AddBookmark(char bookmark, char *path) {
    FILE *fp = OpenFile(bookmarks_path, "a+");
    int allocSize = snprintf(NULL, 0, "%s", path);
    path = realloc(path, allocSize + 2); // Allocating extra space for additional chars
    char *temp = strdup(path);
    if (temp == NULL) {
        endwin();
        printf("Couldn't allocate memory!\n");
        exit(1);
    }
    fprintf(fp, "%c:%s\n", bookmark, replace(temp, "\n", "//"));
    free(temp);
    fclose(fp);
}

WINDOW *createNewWin(int height, int width, int starty, int startx) {
    WINDOW *local_win = newwin(height, width, starty, startx);
    return local_win;
}

void ShowBookMark(void) {
    len_bookmarks = GetNumberOfBookmarks();
    if (len_bookmarks == -1) {
        displayAlert("No Bookmarks Found!");
    } else {
        keys_win = createNewWin(len_bookmarks + 1, maxx, maxy - len_bookmarks, 0);
        DisplayBookmarks();
        secondKey = wgetch(keys_win);
        OpenBookmarkDir(secondKey);
        delwin(keys_win);
    }
}

void AddBookMark(void) {
    displayAlert("Enter Bookmark Key");
    secondKey = wgetch(status_win);
    if (BookmarkExists(secondKey) == 1) {
        displayAlert("Bookmark Key Exists!");
    } else {
        AddBookmark(secondKey, dir);
    }
}

void RmBookMark(pid_t pid) {
    if (access(bookmarks_path, F_OK) != -1) {
        sigprocmask(SIG_BLOCK, &x, NULL);
        pid = fork();
        if (pid == 0) {
            execlp(editor, editor, bookmarks_path, (char *)0);
            exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
            setSelectionCount();
        }
        refresh();
    } else {
        displayAlert("Bookmark List is Empty!");
    }
}
