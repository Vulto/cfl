#define _XOPEN_SOURCE 700
#include "../functions.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void MakeFile(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(close(fd) == 0);
}

static void FreeEntries(char **entries, int count) {
    for (int i = 0; i < count; ++i)
        free(entries[i]);
    free(entries);
}

int main(void) {
    char root[] = "/tmp/cfl-test-XXXXXX";
    assert(mkdtemp(root) != NULL);

    char path[PATH_MAX];
    assert(build_path(path, sizeof path, root, "alpha") == 0);
    MakeFile(path);
    assert(build_path(path, sizeof path, root, ".hidden") == 0);
    MakeFile(path);
    assert(build_path(path, sizeof path, root, "folder") == 0);
    assert(mkdir(path, 0700) == 0);
    assert(isDirectory(path) == 1);

    char extraNames[20][32];
    for (int i = 0; i < 20; ++i) {
        snprintf(extraNames[i], sizeof extraNames[i], "entry-%02d", i);
        assert(build_path(path, sizeof path, root, extraNames[i]) == 0);
        MakeFile(path);
    }

    char filePath[PATH_MAX];
    assert(build_path(filePath, sizeof filePath, root, "alpha") == 0);
    assert(isDirectory(filePath) == 0);

    char linkPath[PATH_MAX];
    assert(build_path(linkPath, sizeof linkPath, root, "dirlink") == 0);
    assert(symlink(path, linkPath) == 0);
    assert(isDirectory(linkPath) == 1);
    assert(unlink(linkPath) == 0);

    assert(build_path(linkPath, sizeof linkPath, root, "brokenlink") == 0);
    assert(symlink("/nonexistent/cfl-target", linkPath) == 0);
    assert(isDirectory(linkPath) == 0);
    assert(unlink(linkPath) == 0);

    char **entries = NULL;
    hiddenFlag = 1;
    int count = getFiles(root, &entries);
    assert(count == 23);
    assert(entries != NULL);
    sortFiles(entries, count, root);
    assert(strcmp(entries[0], "folder") == 0);
    FreeEntries(entries, count);

    entries = NULL;
    hiddenFlag = 0;
    count = getFiles(root, &entries);
    assert(count == 22);
    FreeEntries(entries, count);

    assert(build_path(path, sizeof path, root, "folder") == 0);
    entries = NULL;
    count = getFiles(path, &entries);
    assert(count == 0);
    assert(entries != NULL);
    FreeEntries(entries, count);

    entries = NULL;
    assert(getFiles("/this/path/should/not/exist/cfl", &entries) == -1);
    assert(entries == NULL);

    assert(build_path(path, sizeof path, root, "alpha") == 0);
    assert(unlink(path) == 0);
    assert(build_path(path, sizeof path, root, ".hidden") == 0);
    assert(unlink(path) == 0);
    for (int i = 0; i < 20; ++i) {
        assert(build_path(path, sizeof path, root, extraNames[i]) == 0);
        assert(unlink(path) == 0);
    }
    assert(build_path(path, sizeof path, root, "folder") == 0);
    assert(rmdir(path) == 0);
    assert(rmdir(root) == 0);

    return 0;
}
