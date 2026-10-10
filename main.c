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

        allocSize = snprintf(NULL, 0, "%s", dir);
        if (snprintf(sort_dir, sizeof sort_dir, "%s", dir) >= (int)sizeof sort_dir) {
            displayAlert("Path too long");
            exit(1);
        }

        if( len > 0 ) {
            qsort(directories, len, sizeof (char*), compare);
        }

        if(selection > len-1) {
            selection = len-1;
        }

        handleFlags(directories);

        getmaxyx(stdscr, maxy, maxx);
        /* Respect 1-line top + bottom margin + status bar */
        maxy = LINES - 2 * UI_MARGIN - 2;

        initWindows();
        wclear(current_win);

        int t = 0;
        for( i = start ; i < len ; i++, t++ ) {
            if( t == maxy -1 ) {
                break;
            };

            if (build_path(temp_dir, sizeof temp_dir, dir, directories[i]) != 0) {
                displayAlert("Path too long");
                exit(1);
            }

            int is_sel = (i == selection);
            int is_marked = (checkClipboard(temp_dir) == 1);

            /* Cursor: reverse bar. Marked (v/V): dark gray. Cursor wins when both. */
            wattrset(current_win, A_NORMAL);
            if (is_sel) {
                if (has_colors()) wattron(current_win, A_REVERSE);
                wattron(current_win, A_BOLD);
            } else if (is_marked) {
                if (has_colors()) wattron(current_win, COLOR_PAIR(4));
                else wattron(current_win, A_DIM);
            }

            if (isRegularFile(temp_dir) == 0)
                wattron(current_win, A_BOLD);

            wmove(current_win, t, 2);

            /* Reserve space for right-edge scrollbar inside current_win */
            int rows, list_w;
            getmaxyx(current_win, rows, list_w);
            (void)rows;
            int trunc = list_w - 5;  /* left pad (2) + right bar reservation + safety */

            /* Symlink display: "name → destination" using unicode arrow */
            char entry_display[PATH_MAX * 2 + 32];
            if (isSymlink(temp_dir)) {
                char target[PATH_MAX];
                if (readSymlinkTarget(temp_dir, target, sizeof target) == 0) {
                    snprintf(entry_display, sizeof entry_display, "%s → %s", directories[i], target);
                } else {
                    snprintf(entry_display, sizeof entry_display, "%s → (unreadable)", directories[i]);
                }
            } else {
                snprintf(entry_display, sizeof entry_display, "%s", directories[i]);
            }
            if ((int)strlen(entry_display) > trunc)
                entry_display[trunc] = '\0';

            wprintw(current_win, "%s", entry_display);
            int printed_len = (int)strlen(entry_display);

            /* Full-width bar for cursor and multi-selected rows (same width) */
            if (is_sel || is_marked) {
                for (int c = 2 + printed_len; c < list_w - 1; c++)
                    mvwaddch(current_win, t, c, ' ');
            }
            wattrset(current_win, A_NORMAL);
        }

        /* Hygiene: reset attrs so scrollbar (drawn later) doesn't inherit bold/reverse */
        wattrset(current_win, A_NORMAL);

        if( selection == -1 ) {
            selection = 0;
        }

        if (len > 0) {
            snprintf(selected_file, NAME_MAX, "%s", directories[selection]);
        } else {
            snprintf(selected_file, NAME_MAX, "%s", "Empty");
        }

        displayStatus();

        allocSize = snprintf(NULL, 0, "%s", dir);
        if (snprintf(prev_dir, sizeof prev_dir, "%s", dir) >= (int)sizeof prev_dir) {
            displayAlert("Path too long");
            exit(1);
        }
        getParentPath(prev_dir);

        char **next_directories = NULL;
        len_preview = -1;
        if (len > 0) {
            if (build_path(next_dir, sizeof next_dir, dir, directories[selection]) != 0) {
                displayAlert("Path too long");
                exit(1);
            }

            len_preview = getFiles(next_dir, &next_directories);
            if (len_preview > 0) {
                if (snprintf(sort_dir, sizeof sort_dir, "%s", next_dir) >= (int)sizeof sort_dir) {
                    displayAlert("Path too long");
                    exit(1);
                }
                qsort(next_directories, len_preview, sizeof(char *), compare);
            }
        }

        if (fileInfoMode) {
            ueberzugpp_remove();
            if (len > 0)
                show_file_info(next_dir);
            else if (preview_win) {
                wclear(preview_win);
                mvwprintw(preview_win, 1, 2, "No file selected");
                wrefresh(preview_win);
            }
        } else if (len != 0) {
            if (len_preview > -1) {
                PreviewNextDir(next_dir, next_directories);
            } else {
                getPreview(next_dir, maxy, maxx / 2 + 2);
            }
        }

        /* Draw the two right-side scrollbars. Preview bar is drawn inside its own renderers. */
        drawFileListScrollbar();
        refreshWindows();
        pid_t pid = 0;

        switch( ch = wgetch(current_win) ) {
            case KEY_RESIZE:
                handleResize();
                break;
            case KEY_MOUSE:
                handleMouse();
                break;
            case KEY_NAVUP: scrollUp(); break;
            case KEY_NAVDOWN: scrollDown(); break;
            case KEY_NAVNEXT:
            case '\n': goForward(); break;
            case KEY_NAVBACK: goBack(); break;
            case KEY_NPAGE: nextPage(); break;
            case KEY_PPAGE: prevPage(); break;
            /* Preview pane scrolling (text, hex, directory listings) */
            case '[': previewPageUp(); break;
            case ']': previewPageDown(); break;
            case KEY_JUMPTO:
                ueberzugpp_remove();
                show_jump_to();
                break;
            case 'K': previewScrollUp(); break;
            case KEY_START: goSTART(); break;
            case KEY_GOEND: goEND(); break;
            case KEY_TOP: selection = start; break;
            case KEY_BOTTOM: goBOTTOM(); break;
            case KEY_MID: goMID(); break;
            case KEY_SHELL:
                ueberzugpp_remove(); endwin(); goShell(pid); start = 0; selection = 0; refresh(); break;
            case KEY_RENAME:
                ueberzugpp_remove(); renameFiles(directories); break;
            case KEY_SEL: {
                if (len <= 0) { break; }
                if (build_path(temp_dir, sizeof temp_dir, dir, directories[selection]) != 0) {
                    displayAlert("Path too long");
                    break;
                }
                char encoded[PATH_MAX * 3 + 1];
                if (CflEncodePath(temp_dir, encoded, sizeof encoded) != 0) {
                    displayAlert("Couldn't encode selected path");
                    break;
                }
                if (checkClipboard(temp_dir) == 1) {
                    removeClipboard(encoded);
                    if (selectedFiles > 0) selectedFiles--;
                } else {
                    writeClipboard(encoded);
                    selectedFiles++;
                }
                scrollDown();
                break;
            }
            case KEY_SELALL:
                selectAllFiles(directories, len);
                break;
			case '?': help(stdscr); break;
            case KEY_EMPTYSEL: clearClipboard(); setSelectionCount(); break;
            case KEY_PASTE: copyFiles(dir); break;
            case KEY_MV: moveFiles(dir); break;
            case KEY_MOVETO:
                ueberzugpp_remove();
                show_move_to();
                break;
            case KEY_COPYTO:
                ueberzugpp_remove();
                show_copy_to();
                break;
            case KEY_REMOVEMENU: ueberzugpp_remove(); Deleting(); break;
            case KEY_VIEWSEL: endwin(); ViewSel(pid); break;
            case KEY_EDITSEL: EditSel(pid); break;
            case KEY_INFO:
                fileInfoMode = !fileInfoMode;
                if (fileInfoMode)
                    ueberzugpp_remove();
                clearFlagImg = 1;
                break;
            case KEY_TOGGLEHIDE: hiddenFlag = !hiddenFlag; start = 0; selection = 0; break;
            case KEY_BOOKMARK: go_bookmark(); break;
            case KEY_ADDBOOKMARK: add_bookmark(); break;
            case KEY_RMBOOKMARK: remove_bookmark_prompt(); break;
            case KEY_SEARCH: show_file_search(directories, len); break;
            case KEY_DELETE_SEARCH:
                ueberzugpp_remove();
                show_delete_search(directories, len);
                break;
            case KEY_DIR: CreateDir(); break;
        }

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
