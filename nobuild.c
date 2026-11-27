#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define MAIN "main.c"
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
                "-O0",                      \
                "-D_POSIX_C_SOURCE=200809L", \
                "-D_DEFAULT_SOURCE",        \
                "-D_XOPEN_SOURCE=600",      \
                "-I/usr/include/ncursesw"

#define OLD "c.old"

char *cc(void) {
    char *result = getenv("CC");
    return result ? result : "cc";
}

/* Gentoo ships libtinfow; Debian/Ubuntu ship libtinfo. Probe once. */
static const char *tinfo_link_flag(void) {
    static const char *flag = NULL;
    const char *cands[] = {"-ltinfow", "-ltinfo", NULL};
    char cmd[512];
    int i;

    if (flag)
        return flag;
    for (i = 0; cands[i]; i++) {
        snprintf(cmd, sizeof cmd,
                 "echo 'int main(void){return 0;}' | %s -x c - -o /tmp/cfl-tinfo-probe %s >/dev/null 2>&1",
                 cc(), cands[i]);
        if (system(cmd) == 0) {
            unlink("/tmp/cfl-tinfo-probe");
            flag = cands[i];
            return flag;
        }
    }
    flag = "-ltinfo";
    return flag;
}

void Compile(void) {
    CMD(PRIV_ESC, "mkdir", "-p", "/usr/share/man/man1/cfl");
    CMD(PRIV_ESC, "mkdir", "-p", "/usr/share/cfl");
    CMD(cc(), "-c", MAIN, CFLAGS, "-o", MAINOBJ);
}

void Link(void) {
    CMD(cc(), "-o", BIN, MAINOBJ, "-lncursesw", tinfo_link_flag(), "-lmagic");
    return;
}

void Install(void) {
    CMD(PRIV_ESC, "cp", "-f", MAN, "/usr/share/man/man1");
    CMD(PRIV_ESC, "cp", "-f", BIN, PREFIX);
    CMD(PRIV_ESC, "mkdir", "-p", "/usr/share/cfl");
    CMD(PRIV_ESC, "cp", "-f", "cfl.sh", "/usr/share/cfl/cfl.sh");
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
