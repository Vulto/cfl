#include "functions.h"

int main(int argc, char* argv[]) {
    init(argc, argv);
    cursesInit();

    int ch = 0;

    while(ch != KEY_ESCAPE) {
        char **directories = NULL;
        int filled = getFiles(dir, &directories);
        if (filled == -1) {
            displayAlert("Couldn't open directory");
            break;
        }
        len = filled;

        allocSize = snprintf(NULL,0,"%s",dir);
        sort_dir = malloc(allocSize+1);
        if(sort_dir == NULL){
            displayAlert("Couldn't allocate memory!");
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
                displayAlert("Couldn't allocate memory!");
                exit(1);
            }

            snprintf( temp_dir,allocSize+1,"%s/%s",dir,directories[i] );

            if( i==selection ) {
                if (has_colors()) wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            } else {
                if (has_colors()) wattroff(current_win, A_REVERSE);
                wattroff(current_win, A_BOLD);
            }

            if( isRegularFile(temp_dir) == 0 ) {
                wattron( current_win, A_BOLD );
            } else {
                wattroff( current_win, A_BOLD );
            }

            wmove( current_win,t,2 );

            if( checkClipboard( temp_dir ) == 0 ) {
                wprintw( current_win, "%.*s\n", maxx/2, directories[i] );
            } else {
                wprintw( current_win, "* %.*s\n", maxx/2-3, directories[i] );
            }
        }

        if( selection == -1 ) {
            selection = 0;
        }

        if (len > 0) {
            snprintf(selected_file, NAME_MAX, "%s", directories[selection]);
        } else {
            snprintf(selected_file, NAME_MAX, "%s", "Empty");
        }

        displayStatus();

        allocSize = snprintf( NULL,0,"%s",dir );
        prev_dir = malloc( allocSize + 1 );
        if( prev_dir == NULL ) {
            displayAlert("Couldn't allocate memory!");
            exit(1);
        }
        snprintf( prev_dir,allocSize+1,"%s",dir );
        getParentPath(prev_dir);

        char **next_directories = NULL;
        len_preview = -1;
        if (len > 0) {
            allocSize = snprintf( NULL,0,"%s/%s", dir, directories[selection] );
            next_dir = malloc( allocSize+1 );
            if( next_dir == NULL ) {
                displayAlert("Couldn't allocate memory!");
                exit(1);
            }

            if(strcasecmp(dir,"/") == 0) {
                snprintf(next_dir,allocSize+1,"%s%s", dir, directories[selection]);
            } else {
                snprintf(next_dir,allocSize+1,"%s/%s", dir, directories[selection]);
            }

            len_preview = getFiles(next_dir, &next_directories);
            if(len_preview > 0){
                free(sort_dir);
                allocSize = snprintf(NULL,0,"%s",next_dir);
                sort_dir = malloc(allocSize+1);
                if(sort_dir == NULL){
                    displayAlert("Couldn't allocate memory!");
                    exit(1);
                }
                snprintf(sort_dir,allocSize+1,"%s",next_dir);
                qsort(next_directories, len_preview, sizeof (char*), compare);
            }
        }

        free(sort_dir);

        if(len != 0){
            if(len_preview > -1) {
                PreviewNextDir(next_dir, next_directories);
            } else {
                getPreview(next_dir, maxy, maxx/2+2);
            }
        }

        refreshWindows();
        pid_t pid = 0;
        int fd = 0;
        int pfd[2];

        switch( ch = wgetch(current_win) ) {
            case KEY_RESIZE:
                handleResize();
                break;
            case KEY_NAVUP: scrollUp(); break;
            case KEY_NAVDOWN: scrollDown(); break;
            case KEY_NAVNEXT:
            case '\n': goForward(); break;
            case KEY_NAVBACK: goBack(); break;
            case KEY_NPAGE: nextPage(); break;
            case KEY_PPAGE: prevPage(); break;
            case KEY_START: goSTART(); break;
            case KEY_GOEND: goEND(); break;
            case KEY_TOP: selection = start; break;
            case KEY_BOTTOM: goBOTTOM(); break;
            case KEY_MID: goMID(); break;
            case KEY_SEARCHALL: searchAll(fp, path, pid, fd); break;
            case KEY_SEARCHDIR: searchHere(pfd, path, pid, fd); break;
            case KEY_SHELL:
                clearImg(); endwin(); goShell(pid); start = 0; selection = 0; refresh(); break;
            case KEY_RENAME:
                clearImg(); renameFiles(directories); selectedFiles = 0; break;
            case KEY_SEL: {
                free(temp_dir);
                allocSize = snprintf(NULL,0,"%s/%s",dir,directories[selection]);
                temp_dir = malloc(allocSize+2);
                if(temp_dir == NULL){ displayAlert("Couldn't allocate memory!"); exit(1); }
                snprintf(temp_dir, allocSize+2, "%s/%s", dir, directories[selection]);
                char *temp = strdup(temp_dir);
                if(temp == NULL){ displayAlert("Couldn't allocate memory!"); exit(1); }
                if (checkClipboard(temp_dir) == 1) { removeClipboard(replace(temp,"\n","//")); selectedFiles--; }
                else { writeClipboard(replace(temp,"\n","//")); selectedFiles++; }
                free(temp);
                scrollDown();
                break;
            }
            case KEY_EMPTYSEL: clearClipboard(); setSelectionCount(); break;
            case KEY_PASTE: copyFiles(dir); break;
            case KEY_MV: moveFiles(dir); selectedFiles = 0; break;
            case KEY_REMOVEMENU: clearImg(); Deleting(); break;
            case KEY_VIEWSEL: endwin(); ViewSel(pid); break;
            case KEY_EDITSEL: EditSel(pid); break;
            case KEY_INFO: clearFlagImg = 1; break;
            case KEY_TOGGLEHIDE: hiddenFlag = !hiddenFlag; start = 0; selection = 0; break;
            case KEY_PREVIEW: viewPreview(); break;
            case KEY_SCRIPT: getScripts(directories); break;
            case KEY_BOOKMARK: go_bookmark(); break;
            case KEY_ADDBOOKMARK: add_bookmark(); break;
            case KEY_RMBOOKMARK: remove_bookmark_prompt(); break;
            case KEY_DIR: CreateDir(); break;
        }

        if (len > 0) free(next_dir);
        free(prev_dir);
        for(i=0; i<(len_preview > 0 ? len_preview : 0); i++) {
            free(next_directories[i]);
        }
        for(i=0; i<len; i++) {
            free(directories[i]);
        }
        free(next_directories);
        free(directories);
    }

    WrappeUp();
    return EXIT_SUCCESS;
}
