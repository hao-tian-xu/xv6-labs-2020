//
// Created by Haotian Xu on 10/25/21.
//
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user.h"

const int INT_BYTE = 4;
void _primes(int p[2]);

int
main(int argc, char *argv[])
{
    int p[2];
    int i;

    pipe(p);

    if (fork() == 0) {
        _primes(p);
    } else {
        close(p[0]);
        for (i=2; i<=35; i++){
            write(p[1], &i, INT_BYTE);
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}

void
_primes(int p[2])
{
    int sub_p[2];
    int num;
    int prime;

    close(p[1]);
    if (read(p[0], &prime, INT_BYTE) == 0) {
        close(p[0]);
        exit(0);
    }
    else{
        pipe(sub_p);
        printf("prime %d\n", prime);
        if (fork() == 0){
            close(p[0]);
            _primes(sub_p);
        } else {
            close(sub_p[0]);
            while (read(p[0], &num, INT_BYTE) != 0) {
                if (num % prime != 0) write(sub_p[1], &num, INT_BYTE);
            }
            close(sub_p[1]);
            close(p[0]);
            wait(0);
        }
    }
    exit(0);
}