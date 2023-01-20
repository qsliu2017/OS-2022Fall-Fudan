#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int ret = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, O_DIRECTORY);
    exit(ret);
}
