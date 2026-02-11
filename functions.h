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
char* dir = NULL;
char* next_dir = NULL;
char* prev_dir = NULL;
char* editor = NULL;
char* shell = NULL;
char* temp_dir = NULL;
char* pch = NULL;
struct passwd *info;
char* sort_dir = NULL;
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


void clearImg(void);
// Ueberzug++ helpers (declared here because functions.h is included before their definitions)
static void ueberzugpp_start_once(void);
static void ueberzugpp_add(const char *img_path);
static void ueberzugpp_remove(void);
static void ueberzugpp_stop(void);
void initWindows(void);
void openFile(char *filepath);
void getLastToken(char *tokenizer);
void cursesInit(void) {
	initscr();
	noecho();
	curs_set(0);
}

int getNumberofFiles(char* directory) {
	int len=0;
	DIR *pDir;
	struct dirent *pDirent;

	pDir = opendir (directory);
	if (pDir == NULL) {
		return -1;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		if( strcmp(pDirent->d_name,".") != 0 &&
				strcmp(pDirent->d_name,"..") != 0 ) {
			if( pDirent->d_name[0] == '.' )
				if( hiddenFlag == 0 ) {
					continue;
				}
			len++;
		}
	}
	closedir (pDir);
	return len;
}

int getFiles(char* directory, char* target[]) {
	int i = 0;
	DIR *pDir;
	struct dirent *pDirent;

	pDir = opendir (directory);
	if (pDir == NULL) {
		return -1;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		if( strcmp(pDirent->d_name,".") != 0 &&
				strcmp(pDirent->d_name,"..") != 0 ) {
			if( pDirent->d_name[0] == '.' )
				if( hiddenFlag == 0 )
					continue;
			target[i] = strdup(pDirent->d_name);
			if(target[i++] == NULL) {
				endwin();
				printf("%s\n", "Couldn't allocate memory!");
				exit(1);
			}
		}
	}

	closedir (pDir);
	return i;
}

int compare (const void * a, const void * b ) {
	char *temp_filepath1 = NULL;
	char *temp_filepath2 = NULL;

	allocSize = snprintf(NULL,0,"%s/%s", sort_dir, *(char **)a);
	temp_filepath1 = malloc(allocSize+1);
	if(temp_filepath1 == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_filepath1,allocSize+1,"%s/%s", sort_dir, *(char **)a);
	allocSize = snprintf(NULL,0,"%s/%s", sort_dir, *(char **)b);
	temp_filepath2 = malloc(allocSize+1);
	if(temp_filepath2 == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_filepath2,allocSize+1,"%s/%s", sort_dir, *(char **)b);

	free(temp_filepath1);
	free(temp_filepath2);
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
	current_win = newwin(maxy, maxx/2+2, 0, 0);
	preview_win = newwin(maxy, maxx/2-1, 0, maxx/2+1);
	status_win = newwin(2, maxx, maxy, 0);
	keypad(current_win, TRUE);
	sigprocmask(SIG_UNBLOCK, &x, NULL);
}

int isRegularFile(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return S_ISREG(st.st_mode);
}


void displayStatus(void) {
	wmove(status_win,1,0);
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
		allocSize = snprintf(NULL,0,"%s",cwd);
		dir = malloc(allocSize+1);
		if(dir == NULL) {
			printf("%s\n", "Couldn't initialize dir");
			exit(1);
		}
		snprintf(dir,allocSize+1,"%s",cwd);
	} else {
		printf("Couldn't open current directory");
		exit(1);
	}

	if(argc == 2) {
		if(argv[1][0] == '/') {
			free(dir);
			allocSize = snprintf(NULL, 0, "%s", argv[1]);
			dir = malloc(allocSize+1);
			if(dir == NULL) {
				printf("%s\n", "Couldn't initialize dir");
				exit(1);
			}
			if(strlen(argv[1]) > 1 && argv[1][strlen(argv[1])-1] == '/') {
				argv[1][strlen(argv[1])-1] = '\0';
			}
			snprintf(dir,allocSize+1,"%s", argv[1]);
		} else { // Relative Path
			char *temp;
			int temp_size;
			temp_size = snprintf(NULL, 0, "%s", argv[1]);
			allocSize = snprintf(NULL, 0, "%s", dir);
			temp = malloc(temp_size + allocSize + 2);
			snprintf(temp, temp_size + allocSize + 2, "%s/%s", dir, argv[1]);
			free(dir);
			dir = malloc(temp_size + allocSize + 2);
			if(dir == NULL) {
				printf("%s\n", "Couldn't initialize dir");
				exit(1);
			}
			snprintf(dir, temp_size + allocSize + 2, "%s", temp);
			free(temp);
		}
	} else if(argc > 2) { // Excess arguments given
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

void renameFiles(char *directories){
	if( access( clipboard_path, F_OK ) == -1 ){
		free(temp_dir);
		allocSize = snprintf(NULL,0,"%s/%c",dir,directories[selection]);
		temp_dir = malloc(allocSize+1);
		if(temp_dir == NULL){
			endwin();
			fprintf(stderr, "%s\n", "Couldn't allocate memory!");
			exit(1);
		}
		snprintf(temp_dir,allocSize+1,"%s/%c",dir,directories[selection]);
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
		free(dir);
		allocSize = snprintf(NULL,0,"%s",next_dir);
		dir = malloc(allocSize+1);
		if(dir == NULL){
			endwin();
			printf("%s\n", "Couldn't allocate memory!");
			exit(1);
		}
		snprintf(dir,allocSize+1,"%s",next_dir);
		selection = 0;
		start = 0;
	} else {
		openFile(next_dir);
		clearFlag = 1;
	}
}

void goBack(void) {
	free(temp_dir);
	allocSize = snprintf(NULL,0,"%s",dir);
	temp_dir = malloc(allocSize+1);
	if(temp_dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_dir,allocSize+1,"%s",dir);

	free(dir);
	allocSize = snprintf(NULL,0,"%s",prev_dir);
	dir = malloc(allocSize+1);
	if(dir == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(dir,allocSize+1,"%s",prev_dir);

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

void getScripts(char* directories) {
	int pid;
	len_scripts = getNumberofFiles(scripts_path);
	if(len_scripts <= 0) {
		displayAlert("No scripts found!");
	} else {
		clearImg();
		int status;
		char* scripts[len_scripts];
		status = getFiles(scripts_path, scripts);
		if(status == -1){
			displayAlert("Cannot fetch scripts!");
			sleep(1);
			return;
		}
		keys_win = newwin(len_scripts+1, maxx, maxy-len_scripts, 0);
		wprintw(keys_win,"%s\t%s\n", "S.No.", "Name");
		for(i=0; i<len_scripts; i++){
			wprintw(keys_win, "%d\t%s\n", i+1, scripts[i]);
		}
		secondKey = wgetch(keys_win);
		int option = secondKey - '0';
		option--;
		if(option < len_scripts && option >= 0) {
			free(temp_dir);
			allocSize = snprintf(NULL, 0, "%s/%s", scripts_path, scripts[option]);
			temp_dir = malloc(allocSize+1);
			if(temp_dir == NULL) {
				endwin();
				printf("%s\n", "Couldn't allocate memory!");
				exit(1);
			}
			snprintf(temp_dir, allocSize+1, "%s/%s", scripts_path, scripts[option]);

			allocSize = snprintf(NULL, 0, "%s/%c", dir, directories[selection]);
			buf = malloc(allocSize+1);
			if(buf == NULL) {
				endwin();
				printf("%s\n", "Couldn't allocate memory!");
				exit(1);
			}
			snprintf(buf, allocSize+1, "%s/%c", dir, directories[selection]);

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

			free(buf);
			refresh();
		}
		for(i=0; i<len_scripts; i++) {
			free(scripts[i]);
		}
	}
}

void searchAll(FILE *fp, char *path, pid_t pid, int fd) {
	clearImg();

	free(temp_dir);
	allocSize = snprintf(NULL,0,"%s/preview",cache_path);
	temp_dir = malloc(allocSize+1);
	if(temp_dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_dir,allocSize+1,"%s/preview",cache_path);

	remove(temp_dir);

	endwin();
	pid = fork();
	if( pid == 0 ){
		fd = open(temp_dir, O_CREAT | O_WRONLY, 0755);
		dup2(fd, 1);
		chdir(dir);
		execlp("fzf","fzf",(char *)0);
		exit(1);
	} else {
		int status;
		waitpid(pid,&status,0);
	}

	buf = malloc(PATH_MAX);
	if(buf == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	memset(buf, '\0', PATH_MAX);
	fp = fopen(temp_dir, "r");
	while(fgets(buf,PATH_MAX,fp) != NULL){}
	fclose(fp);

	allocSize = snprintf(NULL, 0, "%s/%s",dir,buf);
	path = malloc(allocSize+1);
	if(path == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(path, allocSize+1, "%s/%s",dir,buf);

	free(temp_dir);
	allocSize = snprintf(NULL,0,"%s", path);
	temp_dir = malloc(allocSize+1);
	if(temp_dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_dir,allocSize+1,"%s",path);
	getLastToken("/");
	getParentPath(path);
	free(dir);
	allocSize = snprintf(NULL,0,"%s", path);
	dir = malloc(allocSize+1);
	if(dir == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(dir,allocSize+1,"%s", path);

	selection = 0;
	start = 0;
	clearFlag = 1;
	searchFlag = 1;

	free(buf);
	free(path);
}


void searchHere(int *pfd, char *path, pid_t pid, int fd){
	clearImg();

	free(temp_dir);
	allocSize = snprintf(NULL,0,"%s/preview",cache_path);
	temp_dir = malloc(allocSize+1);
	if(temp_dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_dir,allocSize+1,"%s/preview",cache_path);

	remove(temp_dir);

	pipe(pfd);
	pid = fork();
	if(pid == 0) {
		dup2(pfd[1], 1);
		close(pfd[1]);
		chdir(dir);
		if( hiddenFlag == 1 )
			execlp("ls","ls","-a",(char *)0);
		else
			execlp("ls","ls",(char *)0);
		exit(1);
	} else {
		int status;
		waitpid(pid,&status,0);
		close(pfd[1]);
		pid = fork();
		if(pid == 0){
			fd = open(temp_dir, O_CREAT | O_WRONLY, 0755);
			dup2(pfd[0],0);
			dup2(fd, 1);
			close(pfd[0]);
			execlp("fzf","fzf",(char *)0);
			exit(1);
		} else {
			waitpid(pid, &status, 0);
			close(pfd[0]);
		}
	}

	buf = malloc(PATH_MAX);
	if(buf == NULL) {
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	memset(buf, '\0', PATH_MAX);
	fp = fopen(temp_dir, "r");
	while(fgets(buf,PATH_MAX,fp) != NULL){}
	fclose(fp);

	allocSize = snprintf(NULL,0,"%s/%s",dir,buf);
	path = malloc(allocSize+1);
	if(path == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(path,allocSize+1,"%s/%s",dir,buf);

	free(temp_dir);
	allocSize = snprintf(NULL,0,"%s",path);
	temp_dir = malloc(allocSize+1);
	if(temp_dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(1);
	}
	snprintf(temp_dir,allocSize+1,"%s",path);
	getLastToken("/");
	getParentPath(path);

	free(dir);
	allocSize = snprintf(NULL,0,"%s",path);
	dir = malloc(allocSize+1);
	if(dir == NULL){
		endwin();
		printf("%s\n", "Couldn't allocate memory!");
		exit(EXIT_FAILURE);
	}
	snprintf(dir,allocSize+1,"%s",path);

	free(buf);
	free(path);

	selection = 0;
	start = 0;
	clearFlag = 1;
	searchFlag = 1;
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
		keys_win = newwin(3, maxx, maxy-3, 0);
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
	free(dir);
	free(temp_dir);

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

	werase(subWin);
	box(subWin, 0, 0);
	if (mkdir(dirName, 0755) == 0) {
		mvwprintw(subWin, 1, 1, "Directory '%s' created", dirName);
	} else {
		mvwprintw(subWin, 1, 1, "Error creating directory '%s'", dirName);
	}

	wrefresh(subWin);
	getch();
	endwin();
}

// preview functions bellow

#include <magic.h>

void getMIME(const char *filepath, char mime[50]) {
    // Initialize magic cookie
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic == NULL) {
        fprintf(stderr, "magic_open failed\n");
        return;
    }

    // Load default magic database
    if (magic_load(magic, NULL) != 0) {
        fprintf(stderr, "magic_load failed: %s\n", magic_error(magic));
        magic_close(magic);
        return;
    }

    // Get MIME type of the file
    const char *mime_type = magic_file(magic, filepath);
    if (mime_type == NULL) {
        fprintf(stderr, "magic_file failed: %s\n", magic_error(magic));
        magic_close(magic);
        return;
    }

    // Copy MIME type or primary type
    if (strncmp(mime_type, "app", 3) == 0) {
        snprintf(mime, 50, "%s", mime_type);
    } else {
        const const char *slash_pos = strchr(mime_type, '/');
        if (slash_pos != NULL) {
            snprintf(mime, slash_pos - mime_type + 1, "%s", mime_type);
        } else {
            snprintf(mime, 50, "%s", mime_type);
        }
    }
}

void getFileType(char *filepath) {
    allocSize = snprintf(NULL,0,"%s",filepath);
    temp_dir = malloc(allocSize+1);
    if(temp_dir == NULL) {
        endwin();
        printf("%s\n", "Couldn't allocate memory!");
        exit(1);
    }
    snprintf(temp_dir,allocSize+1,"%s", filepath);
    getLastToken(".");
}


static int is_probably_text_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char buf[1024];
    ssize_t n = read(fd, buf, sizeof buf);
    close(fd);
    if (n <= 0) return 0;
    for (ssize_t i=0;i<n;i++) {
        if (buf[i] == 0) return 0; // NUL suggests binary
    }
    return 1;
}

static void render_text_preview(const char *path, int maxx) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        wmove(preview_win, 1, 2);
        wprintw(preview_win, "%.*s", maxx-4, "Can't open file");
        wrefresh(preview_win);
        return;
    }
    char line[4096];
    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int max_lines = TEXT_PREVIEW_LINES;
    int y = 1;
    for (int i=0; i<max_lines && fgets(line, sizeof line, fp); ++i) {
        wmove(preview_win, y++, 2);
        wprintw(preview_win, "%.*s", maxx-4, line);
    }
    fclose(fp);
    wrefresh(preview_win);
}

static void render_hex_preview(const char *path, int maxx) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        wmove(preview_win, 1, 2);
        wprintw(preview_win, "%.*s", maxx-4, "Can't open file");
        wrefresh(preview_win);
        return;
    }
    int rows, cols;
    getmaxyx(preview_win, rows, cols);
    int avail_lines = rows - 2;
    if (avail_lines < 1) avail_lines = 1;
    size_t bytes_per_line = 16;
    size_t max_bytes = (size_t)avail_lines * bytes_per_line;
    unsigned char *buf = (unsigned char*)malloc(max_bytes);
    if (!buf) {
        fclose(fp);
        wmove(preview_win, 1, 2);
        wprintw(preview_win, "%.*s", maxx-4, "Out of memory");
        wrefresh(preview_win);
        return;
    }
    size_t n = fread(buf, 1, max_bytes, fp);
    fclose(fp);
    size_t offset = 0;
    int y = 1;
    while (offset < n && y <= avail_lines) {
        char ascii[17]; ascii[16] = '\0';
        wmove(preview_win, y, 2);
        // offset
        wprintw(preview_win, "%08zx  ", offset);
        // hex bytes
        for (size_t i=0;i<bytes_per_line;i++) {
            if (offset + i < n) {
                unsigned char c = buf[offset+i];
                wprintw(preview_win, "%02x ", (unsigned)c);
                ascii[i] = (c>=32 && c<=126) ? c : '.';
            } else {
                wprintw(preview_win, "   ");
                ascii[i] = ' ';
            }
            if (i == 7) wprintw(preview_win, " ");
        }
        wprintw(preview_win, " %s", ascii);
        y++;
        offset += bytes_per_line;
    }
    free(buf);
    wrefresh(preview_win);
}


void getTextPreview(char *filepath, int maxx) {
    struct stat st;
    if (stat(filepath, &st) != 0) return;
    if ((size_t)st.st_size > MAX_PREVIEW_BYTES) return;

    if (is_probably_text_file(filepath)) {
        render_text_preview(filepath, maxx);
    } else {
        render_hex_preview(filepath, maxx);
    }
}


void getPreview(char *filepath, int maxy, int maxx) {
    (void)maxy;
    (void)maxx;
    getFileType(filepath);
    if (strcasecmp("jpg", last) == 0 ||
        strcasecmp("png", last) == 0 ||
        strcasecmp("gif", last) == 0 ||
        strcasecmp("jpeg", last) == 0) {
        ueberzugpp_add(filepath);
        clearFlagImg = 1;
    } else {
        ueberzugpp_remove();
        getTextPreview(filepath, maxx);
    }
}


/* GenImgPreview removed: using ueberzugpp IPC */

// ---- Ueberzug++ integration (image previews) ----
static pid_t ueberzug_pid = -1;
static FILE *ueberzug_in = NULL;
static int ueberzug_failed = 0;

static void ueberzugpp_start_once(void) {
    if (ueberzug_in != NULL || ueberzug_failed) return;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        ueberzug_failed = 1;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        ueberzug_failed = 1;
        return;
    }

    if (pid == 0) {
        // child: stdin <- pipe read end
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        // Detach from ncurses output (ueberzugpp renders independently)
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp(UEBERZUGPP_BIN, UEBERZUGPP_BIN, "layer", "-o", UEBERZUGPP_OUTPUT, (char *)NULL);
        _exit(127);
    }

    // parent
    close(pipefd[0]);
    ueberzug_pid = pid;
    ueberzug_in = fdopen(pipefd[1], "w");
    if (!ueberzug_in) {
        close(pipefd[1]);
        ueberzug_failed = 1;
        ueberzug_pid = -1;
        return;
    }
    setvbuf(ueberzug_in, NULL, _IOLBF, 0); // line buffered
}

static void ueberzugpp_remove(void) {
    if (!ueberzug_in) return;
    fprintf(ueberzug_in, "{\"action\":\"remove\",\"identifier\":\"%s\"}\n", UEBERZUGPP_IDENTIFIER);
    fflush(ueberzug_in);
}

static void ueberzugpp_add(const char *img_path) {
    if (!img_path) return;

    ueberzugpp_start_once();
    if (!ueberzug_in) return;

    int win_y, win_x, win_h, win_w;
    getbegyx(preview_win, win_y, win_x);
    getmaxyx(preview_win, win_h, win_w);

    // Small padding to avoid clobbering UI boundaries
    int x = win_x + 1;
    int y = win_y + 1;
    int w = win_w - 2;
    int h = win_h - 2;

    if (w <= 0 || h <= 0) return;

    // Replace previous image in-place
    ueberzugpp_remove();

    // JSON IPC expects cell coordinates and size in cells
    fprintf(ueberzug_in,
            "{\"action\":\"add\",\"identifier\":\"%s\",\"path\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}\n",
            UEBERZUGPP_IDENTIFIER, img_path, x, y, w, h);
    fflush(ueberzug_in);
}

static void ueberzugpp_stop(void) {
    if (ueberzug_in) {
        ueberzugpp_remove();
        fclose(ueberzug_in);
        ueberzug_in = NULL;
    }
    if (ueberzug_pid > 0) {
        kill(ueberzug_pid, SIGTERM);
        // Best effort wait; don't hang if already reaped
        waitpid(ueberzug_pid, NULL, WNOHANG);
        ueberzug_pid = -1;
    }
}
// ---- end Ueberzug++ integration ----



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

void clearImg() {
    // Remove current image preview (if any)
    ueberzugpp_remove();
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

void PreviewNextDir(char *next_dir, char **next_directories) {
    for(int i=0; i<len_preview; i++ ) {
        if(i == maxy - 1)
            break;
        wmove(preview_win,i+1,2);
        free(temp_dir);
        allocSize = snprintf(NULL,0,"%s/%s", next_dir, next_directories[i]);
        temp_dir = malloc(allocSize+1);
        if(temp_dir == NULL) {
            endwin();
            printf("%s\n", "Couldn't allocate memory!");
            exit(1);
        }
        snprintf(temp_dir, allocSize+1, "%s/%s", next_dir, next_directories[i]);
        if( isRegularFile(temp_dir) == 0 ){
            wattron(preview_win, A_BOLD);
        } else {
            wattroff(preview_win, A_BOLD);
        }
        wprintw(preview_win, "%.*s\n", maxx/2 - 3, next_directories[i]);
    }
}
