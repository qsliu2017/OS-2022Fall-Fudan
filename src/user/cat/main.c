#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
#define BUF_SZ 512
    char buf[BUF_SZ];
    char *s;

    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "-"))) {
        while ((s = fgets(buf, BUF_SZ, stdin)))
            printf("%s", s);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        for (int n; (n = read(fd, buf, BUF_SZ - 1));) {
            buf[n] = 0;
            printf("%s", buf);
        }

        close(fd);
    }
    return 0;
}
