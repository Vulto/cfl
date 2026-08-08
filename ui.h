
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
int hiddenFlag = SHOW_HIDDEN;
/* Set by init() when argv has -j: open Jump under $HOME first */
int start_jump_to = 0;


/*
 * Sticky preview UI (default for interactive panels).
 * Draw into preview_win via ui_sticky_* helpers — no floating/centered modals.
 * Short feedback uses the status line (displayAlert).
 */
typedef enum {
	UI_STICKY_NONE = 0,
	UI_STICKY_HELP,
	UI_STICKY_CREATE,
	UI_STICKY_INFO,
} ui_sticky_kind;

ui_sticky_kind ui_sticky = UI_STICKY_NONE;
int ui_sticky_focus = 0; /* 1 = right panel owns keys; 0 = left list */

/* App modes (no bottom mode bar — status prompt carries context) */
typedef enum {
	MODE_BROWSE = 0,
	MODE_SEARCH,
} cfl_mode;

typedef enum {
	SEARCH_GLOBAL = 0, /* /  */
	SEARCH_LOCAL  = 1, /* s  */
} search_kind;

cfl_mode app_mode = MODE_BROWSE;
search_kind search_sub = SEARCH_GLOBAL;

// Scrollbar helper (reusable for file list right + preview right for symmetry). Implemented later in this file.
void drawScrollbar(WINDOW *win, int total, int first_visible, bool on_left);
void drawFileListScrollbar(void);
void set_mode(cfl_mode m);
void ui_draw_panel_border(WINDOW *win);
void ui_panel_inner(WINDOW *win, int framed, int *y0, int *x0, int *rows, int *cols);
int ui_win_framed(WINDOW *win);
void displayAlert(char *message);
void displayStatus(void);

/* Sticky panel API */
void ui_clip(char *dst, size_t dstsz, const char *src, int max_cols);
void ui_sticky_open(ui_sticky_kind kind, int focus_right);
void ui_sticky_close(void);
void ui_sticky_set_focus(int right);
static inline int ui_sticky_active(void) { return ui_sticky != UI_STICKY_NONE; }
static inline int ui_sticky_is(ui_sticky_kind k) { return ui_sticky == k; }

typedef struct {
	int rows, cols, inner;
	int y;
	int focused;
	int footer_y;
} ui_sticky_draw;

int ui_sticky_begin(ui_sticky_draw *d);
void ui_sticky_title(ui_sticky_draw *d, const char *title);
void ui_sticky_puts(ui_sticky_draw *d, const char *text);
void ui_sticky_input_field(ui_sticky_draw *d, const char *text);
void ui_sticky_footer(ui_sticky_draw *d, const char *focused_hint, const char *unfocused_hint);
void ui_sticky_end(ui_sticky_draw *d);
int ui_sticky_list_rows(const ui_sticky_draw *d);

WINDOW* current_win;
WINDOW* preview_win;
WINDOW* status_win;

void initWindows(void);

/* Used by sticky close to request image clear next frame (defined in preview.h) */
extern int clearFlagImg;

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
	/* No mid-pane sep strip: list window owns the blank after its bar;
	 * preview starts at sep_col. No right-edge margin (preview bar flush). */
	(void)panel_top;
	(void)panel_h;
	(void)sep_col;
}

/* ---------- Sticky preview API ---------- */

static char ui_status_flash[256];
static int ui_status_flash_on = 0;

void ui_clip(char *dst, size_t dstsz, const char *src, int max_cols) {
	if (!dst || dstsz == 0) return;
	if (!src || max_cols <= 0) {
		dst[0] = '\0';
		return;
	}
	size_t n = strlen(src);
	if ((int)n <= max_cols) {
		snprintf(dst, dstsz, "%s", src);
		return;
	}
	if (max_cols <= 2) {
		snprintf(dst, dstsz, "%.*s", max_cols, "..");
		return;
	}
	snprintf(dst, dstsz, "%.*s..", max_cols - 2, src);
}

void ui_sticky_open(ui_sticky_kind kind, int focus_right) {
	ui_sticky = kind;
	ui_sticky_focus = focus_right ? 1 : 0;
	clearFlagImg = 1;
	if (!ui_sticky_focus)
		curs_set(0);
}

void ui_sticky_close(void) {
	ui_sticky = UI_STICKY_NONE;
	ui_sticky_focus = 0;
	curs_set(0);
	clearFlagImg = 1;
}

void ui_sticky_set_focus(int right) {
	ui_sticky_focus = right ? 1 : 0;
	if (!ui_sticky_focus)
		curs_set(0);
}

int ui_sticky_begin(ui_sticky_draw *d) {
	if (!d || !preview_win || !ui_sticky_active())
		return 0;
	wclear(preview_win);
	getmaxyx(preview_win, d->rows, d->cols);
	d->inner = d->cols - 3;
	if (d->inner < 1) d->inner = 1;
	d->y = 0;
	d->focused = ui_sticky_focus;
	d->footer_y = d->rows > 0 ? d->rows - 1 : 0;
	return 1;
}

void ui_sticky_title(ui_sticky_draw *d, const char *title) {
	if (!d || !preview_win || !title) return;
	char tbuf[128];
	ui_clip(tbuf, sizeof tbuf, title, d->inner > 127 ? 127 : d->inner);
	if (d->focused)
		wattron(preview_win, A_BOLD);
	mvwprintw(preview_win, d->y++, 1, "%s", tbuf);
	if (d->focused)
		wattroff(preview_win, A_BOLD);
}

void ui_sticky_puts(ui_sticky_draw *d, const char *text) {
	if (!d || !preview_win) return;
	if (d->y >= d->footer_y) return;
	char buf[512];
	ui_clip(buf, sizeof buf, text ? text : "", d->inner > 511 ? 511 : d->inner);
	mvwprintw(preview_win, d->y++, 1, "%s", buf);
}

void ui_sticky_input_field(ui_sticky_draw *d, const char *text) {
	if (!d || !preview_win) return;
	if (d->y >= d->footer_y) return;
	char buf[512];
	const char *t = (text && text[0]) ? text : " ";
	ui_clip(buf, sizeof buf, t, d->inner > 2 ? d->inner - 2 : d->inner);
	if (d->focused)
		wattron(preview_win, A_REVERSE);
	mvwprintw(preview_win, d->y, 1, "%s", buf);
	if (d->focused) {
		int used = (int)strlen(buf);
		for (int c = 1 + used; c < d->cols - 1 && c < 1 + d->inner; c++)
			mvwaddch(preview_win, d->y, c, ' ');
		wattroff(preview_win, A_REVERSE);
	}
	d->y++;
}

void ui_sticky_footer(ui_sticky_draw *d, const char *focused_hint, const char *unfocused_hint) {
	if (!d || !preview_win) return;
	const char *ft = d->focused
		? (focused_hint ? focused_hint : "")
		: (unfocused_hint ? unfocused_hint : "");
	char fbuf[128];
	ui_clip(fbuf, sizeof fbuf, ft, d->inner > 127 ? 127 : d->inner);
	wattron(preview_win, A_DIM);
	mvwprintw(preview_win, d->footer_y, 1, "%s", fbuf);
	wattroff(preview_win, A_DIM);
}

void ui_sticky_end(ui_sticky_draw *d) {
	(void)d;
	if (!preview_win) return;
	if (ui_sticky_focus)
		curs_set(1);
	else
		curs_set(0);
	ui_draw_panel_border(preview_win);
	wrefresh(preview_win);
}

int ui_sticky_list_rows(const ui_sticky_draw *d) {
	if (!d) return 1;
	int n = d->footer_y - d->y;
	return n < 1 ? 1 : n;
}

void cursesInit(void) {
	initscr();
	/*
	 * raw: Ctrl+V/X/Q readable as bytes.
	 * nonl: Return key stays CR (\\r); Ctrl+J stays LF (\\n) so they differ.
	 * Without nonl, Enter often becomes \\n and collides with Ctrl+J — Enter
	 * would never hop into the selected directory.
	 */
	raw();
	nonl();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	signal(SIGINT, SIG_IGN);

	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(1, COLOR_BLACK, COLOR_WHITE);
		init_pair(2, COLOR_WHITE, 8);
		/* 3: delete-search mark (dark red when 256 colors) */
		if (COLORS >= 256)
			init_pair(3, COLOR_WHITE, 88);
		else
			init_pair(3, COLOR_WHITE, COLOR_RED);
		/* 4: multi-mark / selected row highlight */
		if (COLORS >= 256)
			init_pair(4, COLOR_WHITE, 238);
		else
			init_pair(4, COLOR_WHITE, COLOR_BLACK);
	}

	/* Mouse for scrollbar interaction (wheel, click, drag on both bars) */
	mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | BUTTON5_PRESSED | REPORT_MOUSE_POSITION, NULL);
	mouseinterval(0);
}

int ui_win_framed(WINDOW *win) {
	if (!UI_PANEL_BORDERS || !win) return 0;
	/* Borders only for file list and preview panels */
	if (win == current_win) return UI_BORDER_FILE_LIST ? 1 : 0;
	if (win == preview_win) return UI_BORDER_PREVIEW ? 1 : 0;
	return 0;
}

void ui_panel_inner(WINDOW *win, int framed, int *y0, int *x0, int *rows, int *cols) {
	int h, w;
	getmaxyx(win, h, w);
	if (framed && h >= 3 && w >= 3) {
		if (y0) *y0 = 1;
		if (x0) *x0 = 1;
		if (rows) *rows = h - 2;
		if (cols) *cols = w - 2;
	} else {
		if (y0) *y0 = 0;
		if (x0) *x0 = 0;
		if (rows) *rows = h;
		if (cols) *cols = w;
	}
}

/* Sharp ACS border only (portable). No-op when UI_PANEL_BORDERS is 0. */
void ui_draw_panel_border(WINDOW *win) {
	if (!win || !UI_PANEL_BORDERS) return;
	if (!ui_win_framed(win)) return;
	box(win, 0, 0);
}

void handleResize(void) {
	endwin();
	refresh();                    /* ncurses reads new LINES/COLS */

	getmaxyx(stdscr, maxy, maxx);
	/* usable height for old scroll logic (panels) */
	maxy = LINES - 2 * UI_MARGIN - 2;   /* account for margins + status */

	/* initWindows() frees and recreates all panes */
	initWindows();
}

/* Non-blocking status-line flash (no floating windows). */
void displayAlert(char *message) {
	if (!message) {
		ui_status_flash_on = 0;
		ui_status_flash[0] = '\0';
		return;
	}
	snprintf(ui_status_flash, sizeof ui_status_flash, "%s", message);
	ui_status_flash_on = 1;
	if (status_win)
		displayStatus();
}

void displayStatus(void) {
	if (!status_win) return;
	wclear(status_win);
	wmove(status_win, 0, 1);

	if (ui_status_flash_on && ui_status_flash[0]) {
		wattron(status_win, A_BOLD);
		wprintw(status_win, "%s", ui_status_flash);
		wattroff(status_win, A_BOLD);
		/* Show flash once, then resume normal status next frame */
		ui_status_flash_on = 0;
		wrefresh(status_win);
		return;
	}

	const char *tag = NULL;
	const char *focus_name = NULL;
	switch (ui_sticky) {
	case UI_STICKY_CREATE:
		tag = "NEW";
		focus_name = "create";
		break;
	case UI_STICKY_HELP:
		tag = "HELP";
		focus_name = "keys";
		break;
	case UI_STICKY_INFO:
		tag = "INFO";
		break;
	default:
		break;
	}
	if (tag) {
		wattron(status_win, A_BOLD);
		wprintw(status_win, "[%s] ", tag);
		wattroff(status_win, A_BOLD);
		if (focus_name)
			wprintw(status_win, "focus:%s Tab=switch ",
				ui_sticky_focus ? focus_name : "list");
	}

	wprintw(status_win, "(%d/%d)", selection + 1, len);
	wprintw(status_win, " %s", dir);
	if (strcasecmp(dir, "/") == 0) {
		wprintw(status_win, "%s", selected_file);
	} else {
		wprintw(status_win, "/%s", selected_file);
	}
	wrefresh(status_win);
}

void set_mode(cfl_mode m) {
	app_mode = m;
	if (m != MODE_SEARCH)
		search_sub = SEARCH_GLOBAL;
}

void refreshWindows(void) {
	wrefresh(current_win);
	wrefresh(preview_win);
}

/* Reusable thin proportional thumb-only scrollbar.
 * on_left: bar on left edge; else on the right.
 * File list (current_win): bar is inset by 1 so layout is
 *   content | 1 blank | bar | 1 blank  (trailing blank is the last window col).
 * Preview: bar flush on the last col (trailing blank 0; right edge is terminal edge).
 */
void drawScrollbar(WINDOW *win, int total, int first_visible, bool on_left) {
	if (!win || total <= 0) return;

	int full_h, full_w;
	getmaxyx(win, full_h, full_w);
	int framed = ui_win_framed(win);
	int y0 = 0, x0 = 0, win_rows = full_h, win_cols = full_w;
	ui_panel_inner(win, framed, &y0, &x0, &win_rows, &win_cols);
	if (win_rows <= 1 || win_cols <= 2) return;

	/* 1-col pad to the right of the file-list bar only (not preview). */
	int trail = (!on_left && win == current_win) ? 1 : 0;
	if (win_cols <= 2 + trail) return;

	int bar_col = on_left ? x0 : (x0 + win_cols - 1 - trail);

	wattrset(win, A_NORMAL);
	if (trail) {
		for (int r = 0; r < win_rows; r++)
			mvwaddch(win, y0 + r, bar_col + 1, ' ');
	}

	if (total <= win_rows) {
		for (int r = 0; r < win_rows; r++)
			mvwaddch(win, y0 + r, bar_col, ' ');
		return;
	}

	double vis_f = (double)win_rows;
	double scrollable = (double)total - vis_f;

	int thumb_h = (int)((vis_f / (double)total) * vis_f + 0.5);
	if (thumb_h < 1) thumb_h = 1;
	if (thumb_h > win_rows) thumb_h = win_rows;

	double pos = 0.0;
	if (scrollable > 0.0)
		pos = (double)first_visible / scrollable * (vis_f - thumb_h);
	int thumb_start = (int)(pos + 0.5);
	if (thumb_start < 0) thumb_start = 0;
	if (thumb_start + thumb_h > win_rows) thumb_start = win_rows - thumb_h;

	for (int r = 0; r < win_rows; r++)
		mvwaddch(win, y0 + r, bar_col, ' ');

	wattrset(win, A_DIM);
	for (int r = 0; r < thumb_h; r++)
		mvwaddch(win, y0 + thumb_start + r, bar_col, ACS_VLINE);
	wattrset(win, A_NORMAL);
}

/* Convenience wrapper for the file list (always right edge, using global len/start) */
void drawFileListScrollbar(void) {
	if (current_win) {
		drawScrollbar(current_win, len, start, false);  /* false = right side */
	}
}


/* --- path / listing helpers --- */
/* --- util: path/listing (was util.h) --- */
/* path helpers used by preview + listing */
static int build_path(char *dst, size_t dstsz, const char *base, const char *entry) {
    /* Avoid "//" when base is root. Returns 0 on success, -1 if truncated/error. */
    int n;
    if (base != NULL && base[0] == '/' && base[1] == '\0') {
        n = snprintf(dst, dstsz, "%s%s", base, entry);
    } else {
        n = snprintf(dst, dstsz, "%s/%s", base, entry);
    }
    return (n < 0 || (size_t)n >= dstsz) ? -1 : 0;
}

int isRegularFile(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return S_ISREG(st.st_mode);
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




#endif
