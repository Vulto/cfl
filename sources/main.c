#include "common.h"

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

int main(int argc, char* argv[]) {
    init(argc, argv);
    cursesInit();

    int ch = 0;

    while(ch != KEY_ESCAPE) {
        int temp_len;
        len = getNumberofFiles(dir);

        if(len <= 0) {
            temp_len = 1;
        } else {
            temp_len = len;
        }
        char* directories[temp_len];

        int status;
        status = getFiles(dir, directories);

        if( status == -1 ) {
            endwin();
            printf("Couldn't open \'%s\'", dir);
            exit(1);
        }

        allocSize = snprintf(NULL,0,"%s",dir);
        sort_dir = malloc(allocSize+1);
        if(sort_dir == NULL){
            endwin();
            printf("%s\n", "Couldn't allocate memory!");
            exit(1);
        }
        strncpy(sort_dir,dir,allocSize+1);

        if( len > 0 ) {
            qsort(directories, len, sizeof (char*), compare);
        }

        if(selection > len-1) {
            selection = len-1;
        }

        handleFlags(directories);

        getmaxyx(stdscr, maxy, maxx);
        maxy = maxy - 2;

        initWindows();

        int t = 0;
        for( i = start ; i < len ; i++, t++ ) {
            if( t == maxy -1 ) {
                break;
            };

            free( temp_dir );
            allocSize = snprintf( NULL,0,"%s/%s",dir,directories[i] );
            temp_dir = malloc(allocSize + 1 );
            if( temp_dir == NULL ) {
                endwin();
                printf("%s\n", "Couldn't allocate memory!");
                exit(1);
            }

            snprintf( temp_dir,allocSize+1,"%s/%s",dir,directories[i] );

            if( i==selection ) {
                wattron( current_win, A_STANDOUT );
            } else {
                wattroff( current_win, A_STANDOUT );
            }

            if( isRegularFile(temp_dir) == 0 ) {
                wattron( current_win, A_BOLD );
            } else {
                wattroff( current_win, A_BOLD );
            } 

            wmove( current_win,t+1,2 );

            if( checkClipboard( temp_dir ) == 0 ) {
                wprintw( current_win, "%.*s\n", maxx/2, directories[i] );
            } else {
                wprintw( current_win, "* %.*s\n", maxx/2-3, directories[i] );
            }
        }

        if( selection == -1 ) {
            selection = 0;
            directories[0] = malloc(6);
            if( directories[0] == NULL ) {
                endwin();
                printf("%s\n", "Couldn't allocate memory!");
                exit(1);
            }
            snprintf( directories[0],6,"%s","Empty" );
        }
        snprintf( selected_file,NAME_MAX,"%s",directories[selection] );

        displayStatus();

        allocSize = snprintf( NULL,0,"%s",dir );
        prev_dir = malloc( allocSize + 1 );
        if( prev_dir == NULL ) {
            endwin();
            printf("%s\n", "Couldn't allocate memory!");
            exit(1);
        }
        snprintf( prev_dir,allocSize+1,"%s",dir );
        getParentPath(prev_dir);

        allocSize = snprintf( NULL,0,"%s/%s", dir, directories[selection] );
        next_dir = malloc( allocSize+1 );
        if( next_dir == NULL ) {
            endwin();
            printf( "%s\n", "Couldn't allocate memory!" );
            exit(1);
        }

        if(strcasecmp(dir,"/") == 0) {
            snprintf(next_dir,allocSize+1,"%s%s", dir, directories[selection]);
        } else {
            snprintf(next_dir,allocSize+1,"%s/%s", dir, directories[selection]);
        }

        len_preview = getNumberofFiles(next_dir);
        if(len_preview <= 0) {
            temp_len = 1;
        } else {
            temp_len = len_preview;
        }

        char** next_directories;
        next_directories = (char **)malloc(temp_len * sizeof(char *));
        if(next_directories == NULL){
            endwin();
            printf("%s\n", "Couldn't allocate memory!");
            exit(1);
        }

        status = getFiles(next_dir, next_directories);

        free(sort_dir);
        if(len_preview > 0){
            allocSize = snprintf(NULL,0,"%s",next_dir);
            sort_dir = malloc(allocSize+1);
            if(sort_dir == NULL){
                endwin();
                printf("%s\n", "Couldn't allocate memory!");
                exit(1);
            }
            snprintf(sort_dir,allocSize+1,"%s",next_dir);
            qsort(next_directories, len_preview, sizeof (char*), compare);
            free(sort_dir);
        }

        if(len != 0){
            if(len_preview != -1) {
                PreviewNextDir(next_dir, next_directories);
            } else {
                getPreview(next_dir, maxy, maxx/2+2);
            }
        }

        struct sigaction act;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        act.sa_handler = SIG_DFL;

        if(sigaction(SIGUSR1, &act, NULL) == -1) 
            printf("unable to handle siguser1\n");
        if(sigaction(SIGCHLD, &act, NULL) == -1) 
            printf("unable to handle sigchild\n");

        refreshWindows();
        pid_t pid = 0;

        int fd = 0;
        int pfd[2];

        switch( ch = wgetch(current_win) ){
            case KEY_NAVUP:
                scrollUp();
                break;

            case KEY_NAVDOWN:
                scrollDown();
                break;

            case KEY_NAVNEXT:
            case '\n':
                goForward();
                break;

            case KEY_NAVBACK:
                goBack();
                break;

            case KEY_NPAGE:
                nextPage();
                break;

            case KEY_PPAGE:
                prevPage();
                break;

            case KEY_START:
                selection = 0;
                start = 0;
                break;

            case KEY_GOEND:
                goEnd();
                break;

            case KEY_TOP:
                selection = start;
                break;

            case KEY_BOTTOM:
                goBottom();
                break;

            case KEY_MID:
                break;

            case KEY_SEARCHALL:
                searchAll(fp, path, pid, fd);
                break;

            case KEY_SEARCHDIR:
                searchHere(pfd, path, pid, fd);
                break;

            case KEY_SHELL:
                clearImg();
                endwin();
                goShell(pid);
                start = 0;
                selection = 0;
                refresh();
                break;

            case KEY_RENAME:
                clearImg();
                renameFiles(*directories);
                selectedFiles = 0;
                break;

            case KEY_SEL:
                free(temp_dir);
                allocSize = snprintf(NULL,0,"%s/%s",dir,directories[selection]);
                temp_dir = malloc(allocSize+2);
                if(temp_dir == NULL){
                    endwin();
                    printf("%s\n", "Couldn't allocate memory!");
                    exit(1);
                }
                snprintf(temp_dir, allocSize+2, "%s/%s", dir, directories[selection]);
                char *temp = strdup(temp_dir);
                if(temp == NULL){
                    endwin();
                    printf("%s\n", "Couldn't allocate memory!");
                    exit(1);
                }
                if (checkClipboard(temp_dir) == 1) {
                    removeClipboard(replace(temp,"\n","//"));
                    selectedFiles--;
                } else {
                    writeClipboard(replace(temp,"\n","//"));
                    selectedFiles++;
                }
                free(temp);
                scrollDown();
                break;

            case KEY_EMPTYSEL:
                selectedFiles = 0;
                break;

            case KEY_PASTE:
                copyFiles(dir);
                break;

            case KEY_MV:
                moveFiles(dir);
                selectedFiles = 0;
                break;

            case KEY_REMOVEMENU:
                clearImg();
                Deleting(); 
                break;
            case KEY_BOOKMARK:
                clearImg();
                ShowBookMark();
                break;

            case KEY_ADDBOOKMARK:
                clearImg();
                AddBookMark();
                break;

            case KEY_RMBOOKMARK:
                RmBookMark(pid);
                endwin();
                break;

            case KEY_VIEWSEL:
                endwin();
                ViewSel(pid);
                break;

            case KEY_EDITSEL:
                EditSel(pid);
                break;

            case KEY_INFO:
                // getVidPreview(next_dir);
                clearFlagImg = 1;
                break;

            case KEY_TOGGLEHIDE:
                // i don't like it, seems a bit spaghetti. 
                if( hiddenFlag == 1 )
                    hiddenFlag = 0;
                else
                    hiddenFlag = 1;
                start = 0;
                selection = 0;
                break;

            case KEY_PREVIEW:
                viewPreview();
                break;

            case KEY_SCRIPT:
                getScripts(*directories);
                break;
            case KEY_DIR:
                CreateDir();
                break;
        }

        free(next_dir);
        free(prev_dir);
        for(i=0; i<len_preview; i++) {
            free(next_directories[i]);
        }
        for(i=0; i<len; i++) {
            free(directories[i]);
        }
        if(len <= 0)
            free(directories[0]);
        free(next_directories);
    }

    WrappeUp();
    return EXIT_SUCCESS;
}
