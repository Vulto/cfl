//config.h

/* ============================================================
 * UI — panel borders (recompile after changes)
 * ============================================================ */

/* Panel borders: 0 = off, 1 = sharp ACS box on file list and/or preview only.
 * Status bar and mode line never have borders. */

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

// Program used to open files
#define FILE_OPENER "nopen"

// Tab: focus sticky preview panels (help/create)
#define KEY_VIEWSEL '\t'

// Move-to: multi-mark sources in current dir, then pick destination
#define KEY_MOVETO 'x'

// Copy-to: multi-mark sources, then pick destination (T is KEY_TOP)
#define KEY_COPYTO 'c'

// Rename-to mode: filter current dir, Enter, type new basename (dual-pane)
#define KEY_RENAME 'r'

// View file info
#define KEY_INFO 'i'

// Quit application (not Esc — Esc returns to BROWSE mode)
#define KEY_QUIT 'q'
/* Legacy alias: was misnamed KEY_ESCAPE but is quit */
#define KEY_ESCAPE KEY_QUIT

/* Create empty regular file in current directory */
#define KEY_NEWFILE 'n'

// jump to mode
#define KEY_JUMPTO 'J'

// Basenames skipped under $HOME scan (exact name match, any depth)
static const char *JUMP_OMIT_DIRS[] = {
	".pki",
	".steam",
	".cache",
	".local",
	NULL  /* sentinel */
};
