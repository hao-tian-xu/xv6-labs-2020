//
// Created by Haotian Xu on 10/25/21.
//
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    char argExtra[512];
    char* argvExec[argc];
    int i;

    for (i=1; i<argc; i++) argvExec[i-1] = argv[i];

    while (1) {
        for (i=0;; i++) if (read(0, &argExtra[i], 1) == 0 || argExtra[i] == '\n') break;
        if (i == 0) break;
        argExtra[i] = 0;

        argvExec[argc-1] = argExtra;

        if (fork() == 0) {
            exec(argvExec[0], argvExec);
            exit(0);
        } else {
            wait(0);
        }
    }
    exit(0);
}