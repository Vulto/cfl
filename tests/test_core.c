#include "../cfl_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void TestBuildPath(void) {
    char path[64];

    assert(CflBuildPath(path, sizeof path, "/", "tmp") == 0);
    assert(strcmp(path, "/tmp") == 0);
    assert(CflBuildPath(path, sizeof path, "/tmp", "file name") == 0);
    assert(strcmp(path, "/tmp/file name") == 0);
    assert(CflBuildPath(path, sizeof path, "/tmp/", "file") == 0);
    assert(strcmp(path, "/tmp/file") == 0);

    assert(CflBuildPath(path, 5, "/tmp", "file") == -1);
    assert(path[0] == '\0');
    assert(CflBuildPath(NULL, sizeof path, "/", "tmp") == -1);
    assert(CflBuildPath(path, 0, "/", "tmp") == -1);
}

static void TestJsonEscape(void) {
    char escaped[128];
    const char input[] = "quote=\" slash=\\ line=\n tab=\t control=\001";
    const char expected[] = "quote=\\\" slash=\\\\ line=\\n tab=\\t control=\\u0001";

    assert(CflJsonEscape(input, escaped, sizeof escaped) == 0);
    assert(strcmp(escaped, expected) == 0);
    assert(CflJsonEscape("plain/utf8", escaped, sizeof escaped) == 0);
    assert(strcmp(escaped, "plain/utf8") == 0);

    char small[4];
    assert(CflJsonEscape("four", small, sizeof small) == -1);
    assert(small[0] == '\0');
    assert(CflJsonEscape(NULL, escaped, sizeof escaped) == -1);
    assert(CflJsonEscape("x", escaped, 0) == -1);
}

static void TestReadLine(void) {
    FILE *stream = tmpfile();
    char line[16];

    assert(stream != NULL);
    assert(fputs("first\n\nlast", stream) >= 0);
    rewind(stream);

    assert(CflReadLine(stream, line, sizeof line) == 1);
    assert(strcmp(line, "first") == 0);
    assert(CflReadLine(stream, line, sizeof line) == 1);
    assert(strcmp(line, "") == 0);
    assert(CflReadLine(stream, line, sizeof line) == 1);
    assert(strcmp(line, "last") == 0);
    assert(CflReadLine(stream, line, sizeof line) == 0);
    fclose(stream);

    stream = tmpfile();
    assert(stream != NULL);
    assert(fputs("1234\\n", stream) >= 0);
    rewind(stream);
    char exact[5];
    assert(CflReadLine(stream, exact, sizeof exact) == 1);
    assert(strcmp(exact, "1234") == 0);
    fclose(stream);

    stream = tmpfile();
    assert(stream != NULL);
    assert(fputs("this line is too long\nnext\n", stream) >= 0);
    rewind(stream);
    char small[5];
    assert(CflReadLine(stream, small, sizeof small) == -1);
    assert(CflReadLine(stream, line, sizeof line) == 1);
    assert(strcmp(line, "next") == 0);
    fclose(stream);

    assert(CflReadLine(NULL, line, sizeof line) == -1);
    assert(CflReadLine(stdin, NULL, sizeof line) == -1);
    assert(CflReadLine(stdin, line, 1) == -1);
}

int main(void) {
    TestBuildPath();
    TestJsonEscape();
    TestReadLine();
    puts("CFL core tests passed");
    return 0;
}
