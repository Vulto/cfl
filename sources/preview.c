#include "common.h"
#include <magic.h>

void getMIME(const char *filepath, char mime[50]) {
    // Initialize magic cookie
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (magic == NULL) {
        fprintf(stderr, "magic_open failed\n");
        return;
    }

    // Load default magic database
    if (magic_load(magic, NULL) != 0) {
        fprintf(stderr, "magic_load failed: %s\n", magic_error(magic));
        magic_close(magic);
        return;
    }

    // Get MIME type of the file
    const char *mime_type = magic_file(magic, filepath);
    if (mime_type == NULL) {
        fprintf(stderr, "magic_file failed: %s\n", magic_error(magic));
        magic_close(magic);
        return;
    }

    // Copy MIME type or primary type
    if (strncmp(mime_type, "app", 3) == 0) {
        snprintf(mime, 50, "%s", mime_type);
    } else {
        char *slash_pos = strchr(mime_type, '/');
        if (slash_pos != NULL) {
            snprintf(mime, slash_pos - mime_type + 1, "%s", mime_type);
        } else {
            snprintf(mime, 50, "%s", mime_type);
        }
    }
}


void getFileType(char *filepath) {
    allocSize = snprintf(NULL,0,"%s",filepath);
    temp_dir = malloc(allocSize+1);
    if(temp_dir == NULL) {
        endwin();
        printf("%s\n", "Couldn't allocate memory!");
        exit(1);
    }
    snprintf(temp_dir,allocSize+1,"%s", filepath);
    getLastToken(".");
}

void getTextPreview(char *filepath, int maxx) {
    // Don't Generate Preview if file size > 50MB
    struct stat st;
    stat(filepath, &st);
    if(st.st_size > 10000000)
        return;

    char buf[250];

    char *preview_path = NULL;
    allocSize = snprintf(NULL, 0, "%s/preview", cache_path);
    preview_path = malloc(allocSize+1);
    if(preview_path == NULL) {
        endwin();
        printf("%s\n", "Couldn't allocate memory!");
        exit(1);
    }
    snprintf(preview_path, allocSize+1, "%s/preview", cache_path);

    char mime[50];
    getMIME(filepath, mime);
    if(strcasecmp(mime,"application/x-executable\n") == 0 ||
        strcasecmp(mime,"application/x-sharedlib\n") == 0 ||
        strcasecmp(mime,"application/x-pie-executable\n") == 0) {
        remove(preview_path);
        pid_t pid = fork();
        if(pid == 0) {
            execlp("hexdump","hexdump",filepath,(char*)0);
            exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
            FILE *fp = fopen(preview_path, "r");
            if(fp == NULL) {
                free(preview_path);
                return;
            }
            int t=0;
            while(fgets(buf, 250, (FILE*) fp)) {
                wmove(preview_win,t+1,2);
                wprintw(preview_win,"%.*s",maxx-4,buf);
                t++;
            }
            wrefresh(preview_win);
            fclose(fp);
        }
        free(preview_path);
        return;
    }

    pid_t pid = fork();
    if(pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd,2);
        execlp("cp","cp",filepath,preview_path,(char*)0);
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }

    FILE *fp = fopen(filepath,"r");
    if(fp == NULL) {
        wmove(preview_win,1,2);
        wprintw(preview_win,"%.*s",maxx-4,"Can't Access");
        wrefresh(preview_win);
        free(preview_path);
        return;
    }
    int t=0;
    while(fgets(buf, 250, (FILE*) fp)) {
        wmove(preview_win,t+1,2);
        wprintw(preview_win,"%.*s",maxx-4,buf);
        t++;
    }
    wrefresh(preview_win);
    free(preview_path);
    fclose(fp);
}

void getPreview(char *filepath, int maxy, int maxx) {
    getFileType(filepath);
    if (strcasecmp("jpg",last) == 0 ||
        strcasecmp("png",last) == 0 ||
        strcasecmp("gif",last) == 0 ||
        strcasecmp("jpeg",last) == 0) {
        GenImgPreview(filepath, maxy, maxx);
        clearFlagImg = 1;
    } else {
        getTextPreview(filepath, maxx);
    }
}

void GenImgPreview(char *filepath,  int maxy, int maxx) {
    pid_t pid;
    pid = fork();

    if (pid == 0) {
        char arg1[5];
        char arg2[5];
        char arg3[5];
        char arg4[5];
        snprintf(arg1,5,"%d",maxx);
        snprintf(arg2,5,"%d",2);
        snprintf(arg3,5,"%d",maxx-6);
        snprintf(arg4,5,"%d",maxy);
        execl(DISPLAYIMG,DISPLAYIMG,arg1,arg2,arg3,arg4,filepath,(char *)NULL);
        exit(1);
    }
}

void viewPreview(void) {
    FILE *file;
    char *preview_path = NULL;
    int allocSize;
    size_t file_size;
    char *buffer;
    
    // Calculate the size of the path string and allocate memory
    allocSize = snprintf(NULL, 0, "%s/preview", cache_path);
    if (allocSize < 0) {
        endwin();
        printf("Error calculating allocation size!\n");
        exit(EXIT_FAILURE);
    }

    preview_path = (char *)malloc(allocSize + 1);
    if (preview_path == NULL) {
        endwin();
        printf("Couldn't allocate memory!\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(preview_path, allocSize + 1, "%s/preview", cache_path) < 0) {
        endwin();
        free(preview_path);
        printf("Error formatting preview path!\n");
        exit(EXIT_FAILURE);
    }

    endwin();

    // Open the file for reading
    file = fopen(preview_path, "r");
    if (file == NULL) {
        printf("Error opening preview file: %s\n", preview_path);
        free(preview_path);
        exit(EXIT_FAILURE);
    }

    // Determine the file size
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer to hold the entire file contents
    buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        printf("Error allocating buffer memory\n");
        fclose(file);
        free(preview_path);
        exit(EXIT_FAILURE);
    }

    // Read the file into the buffer
    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0'; // Null-terminate the string

    // Print the contents to the console
    printf("%s\n", buffer);

    // Clean up
    fclose(file);
    free(buffer);
    free(preview_path);
    refresh();
}

void clearImg() {
    pid_t pid;
    pid = fork();

    if(pid == 0) {
        execl(CLEARIMG,CLEARIMG, (char *)NULL);
        exit(1);
    }
}

void openFile(char *filepath) {
    sigprocmask(SIG_BLOCK, &x, NULL);
    pid_t pid;
    pid = fork();
    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(null_fd,2);
        execlp(FILE_OPENER, FILE_OPENER, filepath);
        exit(1);
    } else {
        endwin();
        int status;
        waitpid(pid, &status, 0);
        return;
    }
}

void PreviewNextDir(char *next_dir, char **next_directories) {
    for(int i=0; i<len_preview; i++ ) {
        if(i == maxy - 1)
            break;
        wmove(preview_win,i+1,2);
        free(temp_dir);
        allocSize = snprintf(NULL,0,"%s/%s", next_dir, next_directories[i]);
        temp_dir = malloc(allocSize+1);
        if(temp_dir == NULL) {
            endwin();
            printf("%s\n", "Couldn't allocate memory!");
            exit(1);
        }
        snprintf(temp_dir, allocSize+1, "%s/%s", next_dir, next_directories[i]);
        if( isRegularFile(temp_dir) == 0 ){
            wattron(preview_win, A_BOLD);
        } else {
            wattroff(preview_win, A_BOLD);
        }
        wprintw(preview_win, "%.*s\n", maxx/2 - 3, next_directories[i]);
    }
}
