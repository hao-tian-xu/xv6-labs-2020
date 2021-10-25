//
// Created by Haotian Xu on 10/25/21.
//
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "user.h"

void _find(char *path, char *fileName);
char* pathFileName(char *path);

int
main(int argc, char *argv[])
{
    // must have 3 arguments
    if (argc != 3) {
        printf("find error\n");
        exit(1);
    }
    // recursion function
    _find(argv[1], argv[2]);
    exit(0);
}

void
_find(char *path, char *fileName)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    // fd: file descriptor
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }
    // st: statistics struct
    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:
            if (strcmp(pathFileName(path), fileName) == 0) printf("%s\n", path);
            break;
        case T_DIR:
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
                printf("ls: path too long\n");
                break;
            }
            // buf: full path
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            // traverse (every size of de struct)
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if(de.inum == 0 || strcmp(de.name, "..") == 0 || strcmp(de.name, ".") == 0) continue;
                // buf: full path with sub dir
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                // recursion
                _find(buf, fileName);
            }
            break;
    }
    close(fd);
    return;
}

char*
pathFileName(char *path)
{
    char *p;
    // find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--) ;
    p++;
    return p;
}
