#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define MAIN "main.c"
#define FUNCTIONS "functions.c"
#define PREVIEW "preview.c"
#define BOOK "bookmark.c"

#define MAINOBJ "main.o"

#define BIN "cfl"
#define MAN "cfl.1"
#define PRIV_ESC "sudo"

#define PREFIX "/usr/local/bin/"

#define CFLAGS  "-Wall",                    \
                "-Wextra",                  \
                "-Wfatal-errors",           \
                "-pedantic",                \
                "-pedantic-errors",         \
                "-Wmissing-include-dirs",   \
                "-Wunused-variable",        \
                "-std=c23",                 \
                "-O3",                      \
                "-D_POSIX_C_SOURCE=200809L"

#define OLD "c.old"

#define LIBS "-lncursesw", "-lmagic"

char *cc(void) {
    char *result = getenv("CC");
    return result ? result : "cc";
}

void Compile(void) {
    CMD(PRIV_ESC, "mkdir", "-p", "/usr/share/man/man1/cfl");
    CMD(PRIV_ESC, "mkdir", "-p", "/usr/share/cfl");
    CMD(cc(), "-c", MAIN, CFLAGS, "-o", MAINOBJ);
}

void Link(void) {
    CMD(cc(), "-o", BIN, MAINOBJ, LIBS);
    return;
}

void Install(void) {
    CMD(PRIV_ESC, "cp", "-f", MAN, "/usr/share/man/man1");
    CMD(PRIV_ESC, "cp", "-f", BIN, PREFIX);
}

void Wipe(void) {
    CMD("rm", "-vr", BIN, OLD);
}

int main(int argc, char *argv[]) {
    GO_REBUILD_URSELF(argc, argv);

    if (argc < 2) {
        printf("Usage: %s [-c compile] [-l link] [-i install] [-w wipe]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (arg[0] == '-') {
            for (unsigned long int j = 1; j < strlen(arg); j++) {

                switch (arg[j]) {
                    case 'c': Compile(); break;
                    case 'l': Link();    break;
                    case 'i': Install(); break;
                    case 'w': Wipe();    break;
                        default: printf("Unknown option: %c\n", arg[j]);
                    break;
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
