
#ifndef CFL_UI_H
#define CFL_UI_H


#define UI_MARGIN 1   /* one empty line/column around the entire terminal */

static inline int ui_top(void)    { return UI_MARGIN; }
static inline int ui_left(void)   { return UI_MARGIN; }
static inline int ui_height(void) { return LINES - 2 * UI_MARGIN; }
static inline int ui_width(void)  { return COLS  - 2 * UI_MARGIN; }

int startx, starty, maxx, maxy;
int selectedFiles = 0;
int selection = 0;
int len = 0;
char dir  [PATH_MAX];
char selected_file[NAME_MAX];
int start = 0;
/* When set, right pane shows file info instead of previews/images */
int fileInfoMode = 0;

// Scrollbar helper (reusable for file list right + preview right for symmetry). Implemented later in this file.
void drawScrollbar(WINDOW *win, int total, int first_visible, bool on_left);
void drawFileListScrollbar(void);
// drawPanelSeparator removed (user requested no separator between panes)

WINDOW* current_win;
WINDOW* preview_win;
WINDOW* status_win;
WINDOW* keys_win;

void initWindows(void);

static void ui_get_layout(int *content_left, int *panel_top, int *panel_h, int *sep_col) {
	int margin = UI_MARGIN;
	int content_w = COLS - 2 * margin;
	int content_h = LINES - 2 * margin;
	int left_w = (content_w * 2) / 5 + 2;

	if (content_left) *content_left = margin;
	if (panel_top)    *panel_top    = margin + 2;
	if (panel_h)      *panel_h      = content_h - 2;
	if (sep_col)      *sep_col      = margin + left_w;
}

static void ui_clear_gaps(void) {
	int content_left, panel_top, panel_h, sep_col;
	ui_get_layout(&content_left, &panel_top, &panel_h, &sep_col);
	int margin = UI_MARGIN;

	wattrset(stdscr, A_NORMAL);

	if (LINES > 0)
		mvhline(0, 0, ' ', COLS);
	if (LINES > margin)
		mvhline(LINES - margin, 0, ' ', COLS);
	for (int r = 0; r < LINES; r++)
		mvaddch(r, 0, ' ');
	if (COLS > margin) {
		for (int r = 0; r < LINES; r++)
			mvaddch(r, COLS - margin, ' ');
	}
	for (int r = panel_top; r < panel_top + panel_h && r < LINES; r++)
		mvaddch(r, sep_col, ' ');
}

static void ui_overlay_backdrop(void) {
	wattron(stdscr, A_DIM);
	for (int r = 0; r < LINES; r++)
		mvhline(r, 0, ' ', COLS);
	wattroff(stdscr, A_DIM);
	refresh();
}

static WINDOW *ui_modal_create(int h, int w) {
	int startY = (LINES - h) / 2;
	int startX = (COLS - w) / 2;
	WINDOW *win = newwin(h, w, startY, startX);
	if (!win) return NULL;
	keypad(win, TRUE);
	box(win, 0, 0);
	return win;
}

static void ui_modal_destroy(WINDOW *win) {
	if (!win) return;

	int y, x, h, w;
	getbegyx(win, y, x);
	getmaxyx(win, h, w);
	delwin(win);

	wattrset(stdscr, A_NORMAL);
	for (int r = y; r < y + h && r < LINES; r++) {
		int wclip = w;
		if (x + wclip > COLS) wclip = COLS - x;
		if (wclip > 0)
			mvhline(r, x, ' ', wclip);
	}
	ui_clear_gaps();
	refresh();
}

static void ui_show_message(const char *message) {
	int msgLen = (int)strlen(message) + 2;
	if (msgLen < 10) msgLen = 10;
	int winHeight = 3;
	int winWidth = msgLen + 2;

	ui_overlay_backdrop();
	WINDOW *win = ui_modal_create(winHeight, winWidth);
	if (!win) return;

	wattron(win, A_BOLD);
	mvwprintw(win, 1, 1, "%s", message);
	wattroff(win, A_BOLD);
	wrefresh(win);

	wgetch(win);
	ui_modal_destroy(win);
}

static void ui_prompt_string(const char *label, char *buf, int maxlen) {
	int width = 50;
	int height = 3;

	ui_overlay_backdrop();
	WINDOW *win = ui_modal_create(height, width);
	if (!win) return;

	mvwprintw(win, 1, 1, "%s", label);
	wrefresh(win);

	int label_len = (int)strlen(label);
	int input_col = 1 + label_len;
	int input_max = width - input_col - 2;
	if (input_max < 1) input_max = 1;
	if (input_max > maxlen - 1) input_max = maxlen - 1;

	echo();
	curs_set(1);
	mvwgetnstr(win, 1, input_col, buf, input_max);
	noecho();
	curs_set(0);

	ui_modal_destroy(win);
}

static int ui_prompt_key(const char *label) {
	int width = 48;
	int height = 3;

	ui_overlay_backdrop();
	WINDOW *win = ui_modal_create(height, width);
	if (!win) return ERR;

	mvwprintw(win, 1, 1, "%s", label);
	wrefresh(win);

	int key = wgetch(win);
	ui_modal_destroy(win);
	return key;
}

void cursesInit(void) {
	initscr();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);

	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_BLACK, COLOR_WHITE);
		init_pair(2, COLOR_WHITE, 8);
		/* Pair 3: marked-for-delete (dark red background when 256 colors available) */
		if (COLORS >= 256)
			init_pair(3, COLOR_WHITE, 88);
		else
			init_pair(3, COLOR_WHITE, COLOR_RED);
		/* Pair 4: multi-selected files (v / V) — dark gray background */
		if (COLORS >= 256)
			init_pair(4, COLOR_WHITE, 238);
		else
			init_pair(4, COLOR_WHITE, COLOR_BLACK);
	}

	/* Mouse for scrollbar interaction (wheel, click, drag on both bars) */
	mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | REPORT_MOUSE_POSITION, NULL);
	mouseinterval(0);
}

void handleResize(void) {
	endwin();
	refresh();                    /* ncurses reads new LINES/COLS */

	getmaxyx(stdscr, maxy, maxx);
	/* usable height for old scroll logic (panels) */
	maxy = LINES - 2 * UI_MARGIN - 2;   /* account for margins + status */

	if (status_win)  delwin(status_win);
	if (current_win) delwin(current_win);
	if (preview_win) delwin(preview_win);
	if (keys_win)    delwin(keys_win);

	status_win = current_win = preview_win = keys_win = NULL;

	initWindows();
}

void displayAlert(char *message) {
	ui_show_message(message);
}

void displayStatus(void) {
	wclear(status_win);
	wmove(status_win,0,1);
	if(SHOW_SELECTION_COUNT == 1) {
		wprintw(status_win,"[%d] ", selectedFiles);
	}
	if (fileInfoMode) {
		wattron(status_win, A_BOLD);
		wprintw(status_win, "[INFO] ");
		wattroff(status_win, A_BOLD);
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

void refreshWindows(void) {
	wrefresh(current_win);
	wrefresh(preview_win);
}

/* Reusable thin proportional thumb-only scrollbar.
 * Drawn inside the given window on left (col 0) or right (last col) edge.
 * Both the file-list bar and the preview bar are now placed on the right edge of their panes.
 */
void drawScrollbar(WINDOW *win, int total, int first_visible, bool on_left) {
	if (!win || total <= 0) return;

	int win_rows, win_cols;
	getmaxyx(win, win_rows, win_cols);
	if (win_rows <= 1 || win_cols <= 2) return;

	int bar_col = on_left ? 0 : (win_cols - 1);

	if (total <= win_rows) {
		/* Content fits: hide cleanly by clearing the column */
		wattrset(win, A_NORMAL);
		for (int r = 0; r < win_rows; r++) {
			mvwaddch(win, r, bar_col, ' ');
		}
		return;
	}

	/* Proportional thumb (only) using floating point for smoothness */
	double vis_f = (double)win_rows;
	double scrollable = (double)total - vis_f;

	int thumb_h = (int)((vis_f / (double)total) * vis_f + 0.5);
	if (thumb_h < 1) thumb_h = 1;
	if (thumb_h > win_rows) thumb_h = win_rows;

	double pos = 0.0;
	if (scrollable > 0.0) {
		pos = (double)first_visible / scrollable * (vis_f - thumb_h);
	}
	int thumb_start = (int)(pos + 0.5);
	if (thumb_start < 0) thumb_start = 0;
	if (thumb_start + thumb_h > win_rows) thumb_start = win_rows - thumb_h;

	/* Clear then draw only the thumb segment */
	wattrset(win, A_NORMAL);
	for (int r = 0; r < win_rows; r++) {
		mvwaddch(win, r, bar_col, ' ');
	}

	wattrset(win, A_DIM);
	for (int r = 0; r < thumb_h; r++) {
		mvwaddch(win, thumb_start + r, bar_col, ACS_VLINE);
	}
	wattrset(win, A_NORMAL);
}

/* Convenience wrapper for the file list (always right edge, using global len/start) */
void drawFileListScrollbar(void) {
	if (current_win) {
		drawScrollbar(current_win, len, start, false);  /* false = right side */
	}
}

#endif
