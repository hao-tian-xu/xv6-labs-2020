//
// Created by Haotian Xu on 10/23/21.
//
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("sleep error\n");
        exit(1);
    }
    sleep(atoi(argv[1]));
    exit(0);
}
