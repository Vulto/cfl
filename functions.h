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
#include <magic.h>

#include "config.h"

sigset_t x;
int raised_signal = -1;
int len = 0;
int len_preview = 0;
int len_bookmarks = 0;
int len_scripts = 0;
int selectedFiles = 0;
int i = 0;
int allocSize;
char selected_file[NAME_MAX];

char dir  [PATH_MAX];
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
char* bookmarks_path = NULL;
char* scripts_path = NULL;
char* temp_clipboard_path = NULL;
char* trash_path = NULL;
int selection = 0;
int start = 0;
int clearFlag = 0;
int clearFlagImg = 0;
int searchFlag = 0;
int backFlag = 0;
int hiddenFlag = SHOW_HIDDEN;
int pdfflag = SHOW_PDF_PREVIEWS;
char* last = NULL;
WINDOW* current_win;
WINDOW* preview_win;
WINDOW* status_win;
WINDOW* keys_win;
int startx, starty, maxx, maxy;
bool Running = true;
char secondKey;
char *buf;
char *path;
FILE *fp;


// Ueberzug++ helpers (declared here because functions.h is included before their definitions)
void initWindows(void);
void openFile(char *filepath);
void getLastToken(char *tokenizer);
void displayAlert(char *message);
int isRegularFile(const char *path);

#include "bookmark.h"
#include "preview.h"
void cursesInit(void) {
	initscr();
	noecho();
	curs_set(0);
	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_BLACK, COLOR_WHITE);
		init_pair(2, COLOR_WHITE, 8);
	}
}

void handleResize(void) {
	endwin();
	refresh();                    /* ncurses reads new LINES/COLS */

	getmaxyx(stdscr, maxy, maxx);
	maxy = maxy - 2;

	/* clean old windows */
	if (status_win)  delwin(status_win);
	if (current_win) delwin(current_win);
	if (preview_win) delwin(preview_win);
	if (keys_win)    delwin(keys_win);

	status_win = current_win = preview_win = keys_win = NULL;

	initWindows();
}

int getFiles(char* directory, char*** target) {
	int i = 0;
	int capacity = 16;
	DIR *pDir;
	struct dirent *pDirent;
	char **result;

	pDir = opendir (directory);
	if (pDir == NULL) {
		return -1;
	}

	result = malloc((size_t)capacity * sizeof(char *));
	if(result == NULL) {
		closedir(pDir);
		displayAlert("Couldn't allocate memory!");
		exit(1);
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		if( strcmp(pDirent->d_name,".") != 0 &&
				strcmp(pDirent->d_name,"..") != 0 ) {
			if( pDirent->d_name[0] == '.' )
				if( hiddenFlag == 0 )
					continue;
			if(i >= capacity) {
				capacity *= 2;
				char **grown = realloc(result, (size_t)capacity * sizeof(char *));
				if(grown == NULL) {
					closedir(pDir);
					displayAlert("Couldn't allocate memory!");
					exit(1);
				}
				result = grown;
			}

			result[i] = strdup(pDirent->d_name);
			if(result[i++] == NULL) {
				closedir(pDir);
				displayAlert("Couldn't allocate memory!");
				exit(1);
			}
		}
	}

	closedir (pDir);
	*target = result;
	return i;
}

int compare (const void * a, const void * b ) {
	char temp_filepath1[PATH_MAX];
	char temp_filepath2[PATH_MAX];

	snprintf(temp_filepath1, sizeof(temp_filepath1), "%s/%s", sort_dir, *(char **)a);
	snprintf(temp_filepath2, sizeof(temp_filepath2), "%s/%s", sort_dir, *(char **)b);

	int is_dir1 = (isRegularFile(temp_filepath1) == 0);
	int is_dir2 = (isRegularFile(temp_filepath2) == 0);

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
		clearImg();
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

void initWindows(void) {
	/* Status / info bar at the very top.
	 * Two lines: line 0 = info, line 1 = blank separator. */
	status_win = newwin(2, maxx, 0, 0);

	/* Main panes start below the status bar */
	starty = 2;
	current_win = newwin(maxy, maxx/2+2, starty, 0);
	preview_win = newwin(maxy, maxx/2-1, starty, maxx/2+1);

	keypad(current_win, TRUE);
	sigprocmask(SIG_UNBLOCK, &x, NULL);
}

int isRegularFile(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return S_ISREG(st.st_mode);
}


void displayStatus(void) {
	wclear(status_win);
	wmove(status_win,0,0);
	if(SHOW_SELECTION_COUNT == 1) {
		wprintw(status_win,"[%d] ", selectedFiles);
	}
	wprintw(status_win, "(%d/%d)", selection+1, len);
	wprintw(status_win, " %s", dir);
	if(strcasecmp(dir,"/") == 0) {
		wprintw(status_win, "%s", selected_file);
	} else {
		wprintw(status_win, "/%s", selected_file);
	}
	/* line 1 intentionally left blank as a separator */
	wrefresh(status_win);
}

void displayAlert(char *message) {
	int msgLen = strlen(message) + 1;
	int rows, cols;

	getmaxyx(stdscr, rows, cols);

	int winHeight = 3;
	int winWidth = msgLen + 2;
	int startY = (rows - winHeight) / 2;
	int startX = (cols - winWidth) / 2;

	WINDOW *statusWin = newwin(winHeight, winWidth, startY, startX);

	box(statusWin, 0, 0);

	wattron(statusWin, A_BOLD);
	mvwprintw(statusWin, 1, 1, "%s", message);
	wattroff(statusWin, A_BOLD);

	wrefresh(statusWin);

	getch();

	delwin(statusWin);
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

	allocSize = snprintf(NULL,0,"%s/clipboard",cache_path);
	clipboard_path = malloc(allocSize+1);
	if(clipboard_path == NULL) {
		printf("%s\n", "Couldn't initialize clipboard path");
		exit(1);
	}
	snprintf(clipboard_path,allocSize+1,"%s/clipboard",cache_path);

	allocSize = snprintf(NULL,0,"%s/bookmarks",cache_path);
	bookmarks_path = malloc(allocSize+1);
	if(bookmarks_path == NULL) {
		printf("%s\n", "Couldn't initialize bookmarks path");
		exit(1);
	}
	snprintf(bookmarks_path,allocSize+1,"%s/bookmarks",cache_path);

	allocSize = snprintf(NULL,0,"%s/scripts",cache_path);
	scripts_path = malloc(allocSize+1);
	if(scripts_path == NULL) {
		printf("%s\n", "Couldn't initialize scripts path");
		exit(1);
	}
	snprintf(scripts_path,allocSize+1,"%s/scripts",cache_path);

	allocSize = snprintf(NULL,0,"%s/clipboard.tmp",cache_path);
	temp_clipboard_path = malloc(allocSize+1);
	if(temp_clipboard_path == NULL) {
		printf("%s\n", "Couldn't initialize temp clipboard path path");
		exit(1);
	}
	snprintf(temp_clipboard_path,allocSize+1,"%s/clipboard.tmp",cache_path);

	allocSize = snprintf(NULL,0,"%s/.local/share/trash",info->pw_dir);
	trash_path = malloc(allocSize+1);
	if(trash_path == NULL) {
		printf("%s\n", "Couldn't initialize trash path");
		exit(1);
	}
	snprintf(trash_path,allocSize+1,"%s/.local/share/trash",info->pw_dir);

	if (stat(scripts_path, &st) == -1) {
		mkdir(scripts_path, 0751);
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
	if( SHOW_SELECTION_COUNT == 1 ) {
		setSelectionCount();
	}
}
// Checks if a file exists
int fileExists(char *file) {
	if( access ( file, F_OK ) != -1 ) {
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
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

int checkClipboard(char *filepath) {
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	char *temp = NULL;
	allocSize = snprintf(NULL,0,"%s", filepath);
	temp = malloc(allocSize+1);
	if(temp == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp, allocSize+1, "%s", filepath);
	temp[strlen(temp)]='\0';
	if(f == NULL) {
		free(temp);
		return 0;
	}
	replace(temp,"\n","//");
	while(fgets(buf, PATH_MAX, (FILE*) f)) {
		buf[strlen(buf)-1] = '\0';
		if(strcmp(temp,buf) == 0) {
			free(temp);
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	free(temp);
	return 0;
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
		fclose(f1); // Make sure to close the opened file before exiting
		exit(1);
	}

	while (fgets(buf, PATH_MAX, f1)) {
		buf[strlen(buf) - 1] = '\0';
		if (strcasecmp(buf, filepath) != 0)
			fprintf(f2, "%s\n", buf);
	}

	fclose(f1);
	fclose(f2);

	// Rename the temporary file to overwrite the original clipboard file
	if (rename(temp_clipboard_path, clipboard_path) != 0) {
		endwin();
		perror("Couldn't move temporary clipboard file");
		exit(1);
	}
}

// Empties the clipboard file and resets selection count
void clearClipboard(void) {
    FILE *f = fopen(clipboard_path, "w");
    if (f == NULL) {
        endwin();
        printf("Couldn't open clipboard to clear!\n");
        exit(1);
    }
    fclose(f);
    selectedFiles = 0;
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
	if( access(path, W_OK) == 0 ) {
		return 1;
	} else {
		return 0;
	}
}

void copyFiles(char *present_dir) {
	FILE *f = fopen(clipboard_path, "r");
	char buf[PATH_MAX];
	int flag;
	if(f == NULL) {
		return;
	}

	if(getWritePermissions(dir) == 1) {
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
				execlp("cp", "cp", "-r", "-v", buf, present_dir, (char *)0);
			} else {
				execlp("sudo", "sudo", "cp", "-r", "-v", buf, present_dir, (char *)0);
			}
			exit(1);
		} else {
			int status;
			waitpid(pid, &status, 0);
		}
	}
	refresh();
	fclose(f);
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
	remove(clipboard_path);
}

void renameFiles(char **directories){
	if( access( clipboard_path, F_OK ) == -1 ){
		snprintf(temp_dir, sizeof(temp_dir), "%s/%s", dir, directories[selection]);
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
					execlp("mv", "mv", "-vn", buf, buf2, (char *)0);
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

	remove(clipboard_path);
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

		// Construct the destination path
		snprintf(dest_path, PATH_MAX, "%s/%s", present_dir, strrchr(buf, '/') + 1);

		// Move the file
		if (rename(buf, dest_path) != 0) {
			perror("Error moving file");
		} else {
			printf("Moved: %s -> %s\n", buf, dest_path);
		}
	}

	fclose(f);
	refresh();
	remove(clipboard_path);
}

void refreshWindows(void) {
	wrefresh(current_win);
	wrefresh(preview_win);
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

void getScripts(char** directories) {
	int pid;
	char **scripts = NULL;
	len_scripts = getFiles(scripts_path, &scripts);
	if(len_scripts <= 0) {
		displayAlert("No scripts found!");
		if (scripts) free(scripts);
		return;
	}

	clearImg();
	keys_win = newwin(len_scripts+1, maxx, starty + maxy - len_scripts - 1, 0);
	wprintw(keys_win,"%s\t%s\n", "S.No.", "Name");
	for(i=0; i<len_scripts; i++){
		wprintw(keys_win, "%d\t%s\n", i+1, scripts[i]);
	}
	secondKey = wgetch(keys_win);
	int option = secondKey - '0';
	option--;
	if(option < len_scripts && option >= 0) {
		snprintf(temp_dir, sizeof(temp_dir), "%s/%s", scripts_path, scripts[option]);

		char buf[PATH_MAX];
		snprintf(buf, sizeof(buf), "%s/%s", dir, directories[selection]);

		endwin();
		pid = fork();
		if( pid == 0 ) {
			chdir(dir);
			execl(temp_dir, scripts[option], buf, (char *)0);
			exit(1);
		} else {
			int status;
			waitpid(pid, &status, 0);
		}

		refresh();
	}
	for(i=0; i<len_scripts; i++) {
		free(scripts[i]);
	}
	free(scripts);
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
		keys_win = newwin(3, maxx, starty + maxy - 3, 0);
		wprintw(keys_win,"Key\tCommand");
		wprintw(keys_win,"\n%c\tMove to Garbage Can", KEY_GARBAGE);
		wprintw(keys_win,"\n%c\tDelete", KEY_DELETE);
		wrefresh(keys_win);
		secondKey = wgetch(current_win);
		delwin(keys_win);
		if(secondKey == KEY_GARBAGE) {
			moveFiles(trash_path);
		} else if( secondKey == KEY_DELETE ) {
#if CONFIRM_ON_DEL
			displayAlert("Confirm (y/n): ");
			char confirm = wgetch(status_win);
			if(confirm == 'y') {
				removeFiles();
				selectedFiles = 0;
			} else {
				displayAlert("ABORTED");
				sleep(1);
			}
#else
			removeFiles();
			selectedFiles = 0;
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

void WrappeUp(void) {
	free(cache_path);
	free(temp_clipboard_path);
	free(clipboard_path);
	free(bookmarks_path);
	free(scripts_path);
	free(trash_path);
	free(editor);
	free(shell);

	if(last != NULL) {
		free(last);
	}
	clearImg();
	ueberzugpp_stop();
	endwin();
}

void CreateDir() {
	int height = 3, width = 50;
	int startY = (LINES - height) / 2;
	int startX = (COLS - width) / 2;
	WINDOW *subWin = newwin(height, width, startY, startX);

	box(subWin, 0, 0);
	mvwprintw(subWin, 1, 1, "Enter directory name: ");
	wrefresh(subWin);

	char dirName[256];
	echo();
	mvwgetnstr(subWin, 1, 22, dirName, 255);
	noecho();
	delwin(subWin);

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

void viewPreview(void) {
    FILE *file;
    char *preview_path = NULL;
    int allocSize;
    size_t file_size;
    char *buffer;
    
    // Calculate the size of the path string and allocate memory
    allocSize = snprintf(NULL, 0, "%s/preview", cache_path);
    if (allocSize < 0) {
        endwin();
        printf("Error calculating allocation size!\n");
        exit(EXIT_FAILURE);
    }

    preview_path = (char *)malloc(allocSize + 1);
    if (preview_path == NULL) {
        endwin();
        printf("Couldn't allocate memory!\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(preview_path, allocSize + 1, "%s/preview", cache_path) < 0) {
        endwin();
        free(preview_path);
        printf("Error formatting preview path!\n");
        exit(EXIT_FAILURE);
    }

    endwin();

    // Open the file for reading
    file = fopen(preview_path, "r");
    if (file == NULL) {
        printf("Error opening preview file: %s\n", preview_path);
        free(preview_path);
        exit(EXIT_FAILURE);
    }

    // Determine the file size
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer to hold the entire file contents
    buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        printf("Error allocating buffer memory\n");
        fclose(file);
        free(preview_path);
        exit(EXIT_FAILURE);
    }

    // Read the file into the buffer
    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0'; // Null-terminate the string

    // Print the contents to the console
    printf("%s\n", buffer);

    // Clean up
    fclose(file);
    free(buffer);
    free(preview_path);
    refresh();
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

void PreviewNextDir(char *next_dir_arg, char **next_directories) {
    for(int i=0; i<len_preview; i++ ) {
        if(i == maxy - 1) break;
        wmove(preview_win,i+1,2);
        snprintf(temp_dir, sizeof(temp_dir), "%s/%s", next_dir_arg, next_directories[i]);
        if( isRegularFile(temp_dir) == 0 ){
            wattron(preview_win, A_BOLD);
        } else {
            wattroff(preview_win, A_BOLD);
        }
        wprintw(preview_win, "%.*s\n", maxx/2 - 3, next_directories[i]);
    }
}
