#ifndef CFL_BOOKMARK_H
#define CFL_BOOKMARK_H

static int bookmark_find(char key, char *out_path, size_t out_size) {
    FILE *f = fopen(bookmarks_path, "r");
    if (!f) return 0;

    char line[PATH_MAX + 8];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != key || line[1] != ':') continue;
        char *path_start = line + 2;
        path_start[strcspn(path_start, "\n")] = '\0';
        snprintf(out_path, out_size, "%s", path_start);
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

static void bookmark_set(char key, const char *target_path) {
    FILE *in = fopen(bookmarks_path, "r");
    FILE *out = fopen(temp_clipboard_path, "w");
    if (!out) {
        displayAlert("Failed to update bookmarks");
        if (in) fclose(in);
        return;
    }

    int replaced = 0;
    char line[PATH_MAX + 8];
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (line[0] == key && line[1] == ':') {
                fprintf(out, "%c:%s\n", key, target_path);
                replaced = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%c:%s\n", key, target_path);
    }

    fclose(out);
    rename(temp_clipboard_path, bookmarks_path);
    displayAlert("Bookmark saved");
}

static void bookmark_remove(char key) {
    FILE *in = fopen(bookmarks_path, "r");
    FILE *out = fopen(temp_clipboard_path, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        displayAlert("No bookmarks to remove");
        return;
    }

    int removed = 0;
    char line[PATH_MAX + 8];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == key && line[1] == ':') {
            removed = 1;
            continue;
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);
    rename(temp_clipboard_path, bookmarks_path);

    if (removed) {
        displayAlert("Bookmark removed");
    } else {
        displayAlert("Bookmark key not found");
    }
}

static void go_bookmark(void) {
	WINDOW *sub = newwin(3, 48, (LINES - 3) / 2, (COLS - 48) / 2);
	box(sub, 0, 0);
	mvwprintw(sub, 1, 1, "Jump to bookmark key: ");
	wrefresh(sub);

	int key = wgetch(sub);
	delwin(sub);

	char target[PATH_MAX];
	if (!bookmark_find((char)key, target, sizeof(target))) {
		displayAlert("Bookmark not found");
		return;
	}

	struct stat st;
	if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
		displayAlert("Bookmark target is not a directory");
		return;
	}

	snprintf(dir, sizeof(dir), "%s", target);
	selection = 0;
	start = 0;
}

static void add_bookmark(void) {
    WINDOW *sub = newwin(3, 48, (LINES - 3) / 2, (COLS - 48) / 2);
    box(sub, 0, 0);
    mvwprintw(sub, 1, 1, "Save current dir to key: ");
    wrefresh(sub);

    int key = wgetch(sub);
    delwin(sub);

    if (key == ERR) {
        displayAlert("Invalid bookmark key");
        return;
    }

    bookmark_set((char)key, dir);
}

static void remove_bookmark_prompt(void) {
    WINDOW *sub = newwin(3, 48, (LINES - 3) / 2, (COLS - 48) / 2);
    box(sub, 0, 0);
    mvwprintw(sub, 1, 1, "Remove bookmark key: ");
    wrefresh(sub);

    int key = wgetch(sub);
    delwin(sub);

    if (key == ERR) {
        displayAlert("Invalid bookmark key");
        return;
    }

    bookmark_remove((char)key);
}

#endif
