// common.h

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
#include <pwd.h>
#include <dirent.h>
#include <signal.h>
#include <linux/limits.h>
#include <sys/ioctl.h>

#include "config.h"

// Define global variables
extern sigset_t x;
extern int raised_signal;
extern int len;
extern int len_preview;
extern int len_bookmarks;
extern int len_scripts;
extern int selectedFiles;
extern int i;
extern int allocSize;
extern char selected_file[NAME_MAX];
extern char* dir;
extern char* next_dir;
extern char* prev_dir;
extern char* editor;
extern char* shell;
extern char* temp_dir;
extern char* pch;
extern struct passwd *info;
extern char* sort_dir;
extern char* cache_path;
extern char* clipboard_path;
extern char* bookmarks_path;
extern char* scripts_path;
extern char* temp_clipboard_path;
extern char* trash_path;
extern int selection;
extern int start;
extern int clearFlag;
extern int clearFlagImg;
extern int searchFlag;
extern int backFlag;
extern int hiddenFlag;
extern int pdfflag;
extern char* last;
extern WINDOW* current_win;
extern WINDOW* preview_win;
extern WINDOW* status_win;
extern WINDOW* keys_win;
extern int startx, starty, maxx, maxy;
extern int ch;
extern pid_t pid;
extern int fd;
extern int temp_len;
extern bool Running;
extern char* directories;
extern char secondKey;
extern char* buf;
extern FILE *fp;

// Prototypes
void init(int argc, char* argv[]);
int getNumberofFiles(char* directory);
int getFiles(char* directory, char* target[]);
int compare(const void* a, const void* b);
void initWindows(void);
void displayStatus(void);
void getParentPath(char* path);
void refreshWindows(void);
void scrollUp(void);
void scrollDown(void);
void goForward(void);
void goBack(void);
void nextPage(void);
void prevPage(void);
int checkClipboard(char* filepath);
char* replace(char* str, char* a, char* b);
void writeClipboard(char* filepath);
void removeClipboard(char* filepath);
void emptyClipboard(void);
void copyFiles(char* present_dir);
void removeFiles(void);
void moveFiles(char* present_dir);
int fileExists(char* file);
WINDOW *createNewWin(int height, int width, int starty, int startx);
void displayAlert(char *message);
int getNumberOfBookmarks(void);
void displayBookmarks(void);
void openBookmarkDir(char secondKey);
int bookmarkExists(char bookmark);
void addBookmark(char bookmark, char *path);
void setSelectionCount(void);
void handleFlags(char** directories);

void openFile(char *filepath);

void getLastToken(char* tokenizer);

void getTextPreview(char *filepath, int maxx);

void getMIME(const char *filepath, char mime[50]);

void getFileType(char *filepath);

void getPreview(char *filepath, int maxy, int maxx);

void viewPreview(void);

void getImgPreview(char *filepath, int maxy, int maxx);

void clearImg(void);

int isRegularFile(const char* path);

void cursesInit();

void keyboard(void);

void getOut(void);

void keyHandler();

void goEnd(void);

void goBottom(void);

void getScripts(char *directories);

void searchAll(FILE *fp, char *path, pid_t pid, int fd);

void searchHere(int *pfd, char *path, pid_t, int fd);

void goMid(void);

void callShell(void);

void renameFiles(char *directories);

void goShell(pid_t pid);

// void Select(char *directories[selection], char* dir);
