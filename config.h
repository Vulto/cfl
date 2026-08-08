//config.h

/* ============================================================
 * UI — panel borders (recompile after changes)
 * ============================================================ */

/* Panel borders: 0 = off, 1 = sharp ACS box on file list and/or preview only.
 * Status bar and mode line never have borders. */
#ifndef UI_PANEL_BORDERS
#define UI_PANEL_BORDERS 0
#endif

#ifndef UI_BORDER_FILE_LIST
#define UI_BORDER_FILE_LIST false
#endif
#ifndef UI_BORDER_PREVIEW
#define UI_BORDER_PREVIEW false
#endif

// Ueberzug++ image preview (optional; if missing, images just won't render)
#ifndef UEBERZUGPP_BIN
#define UEBERZUGPP_BIN "ueberzugpp"
#endif

// Output backend. For st on X11, "x11" is typical. Other values: wayland, sixel, kitty, iterm2, chafa.
#ifndef UEBERZUGPP_OUTPUT
#define UEBERZUGPP_OUTPUT "x11"
#endif

#ifndef UEBERZUGPP_IDENTIFIER
#define UEBERZUGPP_IDENTIFIER "cfl_preview"
#endif


#ifndef MAX_PREVIEW_BYTES
#define MAX_PREVIEW_BYTES (50 * 1024 * 1024) // 50 MB
#endif

#ifndef TEXT_PREVIEW_LINES
#define TEXT_PREVIEW_LINES 60
#endif


// Set to 1 if you want to see hidden files on startup
#define SHOW_HIDDEN false

// (P_SEPARATOR was removed per user request for visual symmetry — both scrollbars now on the right of their panes)

// Set to false if you don't want to see number of selected files in the statusbar
#define SHOW_SELECTION_COUNT true

// Program used to open files
#define FILE_OPENER "nopen"

// Go to parent directory
#define KEY_NAVBACK 'h'

// Go down
#define KEY_NAVDOWN 'j'

// Go up
#define KEY_NAVUP 'k'

// Go to next directory / Open file
#define KEY_NAVNEXT 'l'

// Go to the beginning of the dir
#define KEY_START 'g'

// Go to the end of the directory
#define KEY_GOEND 'G'

// Go to the top of the current view
#define KEY_TOP 'T'

// Go to the middle of the current view
#define KEY_MID 'M'

// Go to the bottom of the current view
#define KEY_BOTTOM 'B'

// Tab: focus sticky preview panels (help/create)
#define KEY_VIEWSEL '\t'

// Move-to: multi-mark sources in current dir, then pick destination
#define KEY_MOVETO 'm'

// Copy-to: multi-mark sources, then pick destination (T is KEY_TOP)
#define KEY_COPYTO 'C'

// Rename-to mode: filter current dir, Enter, type new basename (dual-pane)
#define KEY_RENAME 'r'

// View file info
#define KEY_INFO 'i'

// Toggle Hidden Files
#define KEY_TOGGLEHIDE '.'

#define KEY_SHELL '!'

// View preview
#define KEY_PREVIEW '['

// Reload
#define KEY_UPDATE 'u'

// Quit application (not Esc — Esc returns to BROWSE mode)
#define KEY_QUIT 'q'
/* Legacy alias: was misnamed KEY_ESCAPE but is quit */
#define KEY_ESCAPE KEY_QUIT

/* Create empty regular file in current directory */
#define KEY_NEWFILE 'n'

/* Create directory in current directory (was 'n') */
#define KEY_DIR 'N'

// Fuzzy search files in the current directory (SEARCH /:)
#define KEY_SEARCH '/'

// Local-folder search (SEARCH s:)
#define KEY_SEARCH_LOCAL 's'

// Remove-search: filter + Tab multi-mark; confirm Enter/t=trash, d=purge (no pre-select)
#define KEY_DELETE_SEARCH 'D'

// Jump-to (legacy key; mcfl is always jump-first under $HOME)
#define KEY_JUMPTO 'J'

// Basenames skipped under $HOME scan (exact name match, any depth)
static const char *JUMP_OMIT_DIRS[] = {
	".pki",
	".steam",
	".cache",
	".local",
	NULL  /* sentinel */
};
