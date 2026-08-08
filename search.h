#ifndef CFL_SEARCH_H
#define CFL_SEARCH_H

/* listing helpers needing sort_dir / hiddenFlag */
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



/* Panel-based fuzzy search: left list + right preview (main cfl layout).
 * Filter algorithm matches mmenu (case-insensitive substring + refine-on-append). */

#define SEARCH_MAX_INPUT 128
#define SEARCH_FILT_CAP  128

typedef struct {
    int *indices;
    int cap;
    int count;
} search_filt;

static void search_filt_init(search_filt *f) {
    f->cap = SEARCH_FILT_CAP;
    f->indices = malloc((size_t)f->cap * sizeof(int));
    if (!f->indices) {
        displayAlert("Couldn't allocate memory!");
        exit(1);
    }
    f->count = 0;
}

static void search_filt_free(search_filt *f) {
    free(f->indices);
    f->indices = NULL;
    f->count = f->cap = 0;
}

static void search_filt_push(search_filt *f, int idx) {
    if (f->count == f->cap) {
        f->cap *= 2;
        int *grown = realloc(f->indices, (size_t)f->cap * sizeof(int));
        if (!grown) {
            displayAlert("Couldn't allocate memory!");
            exit(1);
        }
        f->indices = grown;
    }
    f->indices[f->count++] = idx;
}

static void search_filt_clear(search_filt *f) { f->count = 0; }

static const char *search_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return p;
        if (!p[i] && i < nlen) break;
    }
    return NULL;
}

static void search_filt_fill_all(search_filt *f, int n) {
    search_filt_clear(f);
    for (int i = 0; i < n; i++)
        search_filt_push(f, i);
}

/* Rank matches: shorter path first, then fewer '/', then earlier match, then index. */
typedef struct {
    int idx;
    int path_len;
    int depth;
    int match_pos;
} search_rank;

static int search_rank_cmp(const void *a, const void *b) {
    const search_rank *ra = a;
    const search_rank *rb = b;
    if (ra->path_len != rb->path_len)
        return ra->path_len - rb->path_len;
    if (ra->depth != rb->depth)
        return ra->depth - rb->depth;
    if (ra->match_pos != rb->match_pos)
        return ra->match_pos - rb->match_pos;
    return ra->idx - rb->idx;
}

static int search_path_depth(const char *p) {
    int d = 0;
    if (!p) return 0;
    for (; *p; p++)
        if (*p == '/') d++;
    return d;
}

static void search_filt_sort_short(search_filt *f, char **files, const char *q) {
    if (!f || f->count <= 1 || !files) return;

    search_rank *ranks = malloc((size_t)f->count * sizeof(search_rank));
    if (!ranks) return;

    for (int i = 0; i < f->count; i++) {
        int oidx = f->indices[i];
        const char *p = files[oidx];
        ranks[i].idx = oidx;
        ranks[i].path_len = p ? (int)strlen(p) : 0;
        ranks[i].depth = search_path_depth(p);
        ranks[i].match_pos = 0;
        if (q && q[0] && p) {
            const char *m = search_strcasestr(p, q);
            ranks[i].match_pos = m ? (int)(m - p) : 0x7fffffff;
        }
    }

    qsort(ranks, (size_t)f->count, sizeof(search_rank), search_rank_cmp);

    for (int i = 0; i < f->count; i++)
        f->indices[i] = ranks[i].idx;

    free(ranks);
}

static void search_filt_apply(search_filt *f, char **files, int n, const char *q) {
    search_filt_clear(f);
    if (!q || q[0] == '\0') {
        search_filt_fill_all(f, n);
        search_filt_sort_short(f, files, NULL);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (files[i] && search_strcasestr(files[i], q))
            search_filt_push(f, i);
    }
    search_filt_sort_short(f, files, q);
}

/* Refine when the user appended a character. Returns 1 on success. */
static int search_filt_refine(search_filt *f, char **files, const char *q) {
    search_filt newf;
    newf.cap = f->count < SEARCH_FILT_CAP ? SEARCH_FILT_CAP : f->count;
    newf.indices = malloc((size_t)newf.cap * sizeof(int));
    if (!newf.indices) return 0;
    newf.count = 0;
    for (int k = 0; k < f->count; k++) {
        int oidx = f->indices[k];
        if (files[oidx] && search_strcasestr(files[oidx], q))
            search_filt_push(&newf, oidx);
    }
    free(f->indices);
    *f = newf;
    search_filt_sort_short(f, files, q);
    return 1;
}

static char *search_wc_to_mb(const wchar_t *w) {
    if (!w || !*w) return strdup("");
    size_t n = wcstombs(NULL, w, 0);
    if (n == (size_t)-1) return NULL;
    char *m = malloc(n + 1);
    if (m) wcstombs(m, w, n + 1);
    return m;
}

static void search_free_listing(char ***listing, int *n) {
    if (!listing || !*listing) {
        if (n) *n = 0;
        return;
    }
    int count = n ? *n : 0;
    for (int i = 0; i < count; i++)
        free((*listing)[i]);
    free(*listing);
    *listing = NULL;
    if (n) *n = 0;
}

/* Preview one absolute path via preview.h (PreviewNextDir / getPreview), same as main. */
static void search_preview_abs(const char *path,
                               char ***preview_listing, int *preview_n)
{
    if (!preview_win) return;

    search_free_listing(preview_listing, preview_n);
    wclear(preview_win);
    preview_scroll = 0;
    len_preview = -1;

    if (!path || !path[0]) {
        wmove(preview_win, 0, PREVIEW_TEXT_X);
        wprintw(preview_win, "No preview");
        wrefresh(preview_win);
        ueberzugpp_remove();
        return;
    }

    char path_mut[PATH_MAX];
    snprintf(path_mut, sizeof path_mut, "%s", path);

    /* Directory → PreviewNextDir; file → getPreview (main cfl flow) */
    if (isRegularFile(path_mut) == 0) {
        ueberzugpp_remove();
        int filled = getFiles(path_mut, preview_listing);
        if (filled < 0) {
            *preview_n = 0;
            wmove(preview_win, 0, PREVIEW_TEXT_X);
            wprintw(preview_win, "Cannot open directory");
            wrefresh(preview_win);
            return;
        }
        *preview_n = filled;
        len_preview = filled;
        if (filled > 0) {
            char saved_sort[PATH_MAX];
            snprintf(saved_sort, sizeof saved_sort, "%s", sort_dir);
            if (snprintf(sort_dir, sizeof sort_dir, "%s", path_mut) < (int)sizeof sort_dir)
                qsort(*preview_listing, (size_t)filled, sizeof(char *), compare);
            snprintf(sort_dir, sizeof sort_dir, "%s", saved_sort);
            /* PreviewNextDir refreshes preview_win (main used refreshWindows after) */
            PreviewNextDir(path_mut, *preview_listing);
        } else {
            wmove(preview_win, 0, PREVIEW_TEXT_X);
            wprintw(preview_win, "Empty directory");
            wrefresh(preview_win);
        }
    } else {
        getPreview(path_mut, maxy, maxx / 2 + 2);
    }
}

/* Browse: resolve dir + relative name, then preview. */
static void search_draw_preview(char **files, search_filt *filt, int sel,
                                char ***preview_listing, int *preview_n)
{
    if (!preview_win) return;

    if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count || !files) {
        search_preview_abs(NULL, preview_listing, preview_n);
        return;
    }

    int oidx = filt->indices[sel];
    char path[PATH_MAX];
    if (build_path(path, sizeof path, dir, files[oidx]) != 0) {
        search_free_listing(preview_listing, preview_n);
        wclear(preview_win);
        preview_scroll = 0;
        len_preview = -1;
        wmove(preview_win, 1, 0);
        wprintw(preview_win, "Path too long");
        wrefresh(preview_win);
        return;
    }
    search_preview_abs(path, preview_listing, preview_n);
}

/* Jump / dest-pick: paths[] are absolute. */
static void search_draw_preview_paths(char **paths, search_filt *filt, int sel,
                                      char ***preview_listing, int *preview_n)
{
    if (!preview_win) return;

    if (!filt || filt->count <= 0 || sel < 0 || sel >= filt->count || !paths) {
        search_preview_abs(NULL, preview_listing, preview_n);
        return;
    }
    int oidx = filt->indices[sel];
    const char *path = paths[oidx] ? paths[oidx] : "";
    search_preview_abs(path, preview_listing, preview_n);
}


static int search_count_marked(const unsigned char *marked, int n)
{
	int c = 0;
	if (!marked || n <= 0) return 0;
	for (int i = 0; i < n; i++)
		if (marked[i]) c++;
	return c;
}

/* File list with Tab multi-mark (× prefix, pair 3). Used by mcfl browse. */
static void search_draw_marked_list(char **files, search_filt *filt, int sel, int top,
				    const unsigned char *marked)
{
	if (!current_win) return;
	wclear(current_win);
	int rows, cols;
	getmaxyx(current_win, rows, cols);
	int visible = rows > 0 ? rows : 1;
	/*
	 * List layout: content | 1 blank | bar | 1 blank
	 * (bar inset; trailing blank restored on the right of the list scrollbar).
	 * 1-col mark gutter remains in the entry string ("×" / " ").
	 */
	int trunc = cols - 3;
	if (trunc < 1) trunc = 1;
	if (filt->count == 0) {
		wmove(current_win, 0, 0);
		wprintw(current_win, "No matches.");
	} else {
		for (int v = 0; v < visible && top + v < filt->count; v++) {
			int fidx = top + v;
			int oidx = filt->indices[fidx];
			const char *name = files[oidx] ? files[oidx] : "";
			int is_marked = marked && marked[oidx];
			char full[PATH_MAX];
			int path_ok = (build_path(full, sizeof full, dir, name) == 0);
			int is_dir = path_ok && (isRegularFile(full) == 0);
			wattrset(current_win, A_NORMAL);
			if (is_marked && has_colors())
				wattron(current_win, COLOR_PAIR(3));
			if (fidx == sel) {
				if (has_colors() && !is_marked)
					wattron(current_win, A_REVERSE);
				wattron(current_win, A_BOLD);
			}
			if (is_dir) wattron(current_win, A_BOLD);
			wmove(current_win, v, 0);
			char entry_display[PATH_MAX * 2 + 32];
			const char *prefix = is_marked ? "×" : " ";
			if (path_ok && isSymlink(full)) {
				char target[PATH_MAX];
				if (readSymlinkTarget(full, target, sizeof target) == 0)
					snprintf(entry_display, sizeof entry_display, "%s%s → %s", prefix, name, target);
				else
					snprintf(entry_display, sizeof entry_display, "%s%s → (unreadable)", prefix, name);
			} else {
				snprintf(entry_display, sizeof entry_display, "%s%s", prefix, name);
			}
			if ((int)strlen(entry_display) > trunc)
				entry_display[trunc] = '\0';
			wprintw(current_win, "%s", entry_display);
			if (fidx == sel || is_marked) {
				int used = (int)strlen(entry_display);
				/* fill content only; leave blank before bar (and bar/trail for drawScrollbar) */
				for (int c = used; c < cols - 3; c++)
					mvwaddch(current_win, v, c, ' ');
			}
			wattrset(current_win, A_NORMAL);
		}
	}
	drawScrollbar(current_win, filt->count > 0 ? filt->count : 1, top, false);
	wrefresh(current_win);
}

#endif /* CFL_SEARCH_H */
