#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define MAIN "main.c"
#define FUNCTIONS "functions.c"
#define PREVIEW "preview.c"
#define KEYBOARD "keyboard.c"

#define MAINOBJ "obj/main.o"
#define FUNCOBJ "obj/functions.o"
#define PREVOBJ "obj/preview.o"
#define KEYBOARDOBJ "obj/keyboard.o"

#define BIN "../cfl"

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

#define LIBS "-lncurses", "-lmagic"

char *cc(void) {
    char *result = getenv("CC");
    return result ? result : "cc";
}

void Compile(void) {
    CMD(cc(), "-c", MAIN, CFLAGS, "-o", MAINOBJ);
    CMD(cc(), "-c", FUNCTIONS, CFLAGS, "-o", FUNCOBJ);
    CMD(cc(), "-c", PREVIEW, CFLAGS, "-o", PREVOBJ);
    CMD(cc(), "-c", KEYBOARD, CFLAGS, "-o", KEYBOARDOBJ);
}

void Link(void) {
    CMD(cc(), "-o", BIN, MAINOBJ, FUNCOBJ, PREVOBJ, KEYBOARDOBJ, LIBS);
    return;
}

void Install(void) {
    CMD("doas", "cp", "-f", BIN, PREFIX);
}

void Wipe(void) {
    CMD("rm", "-v", BIN);
    CMD("rm", OLD);
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
                    case 'c': Compile();  break;
                    case 'l': Link();     break;
                    case 'i': Install();	break;
                    case 'w': Wipe();     break;
                    default: printf("Unknown option: %c\n", arg[j]);
                    break;
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
