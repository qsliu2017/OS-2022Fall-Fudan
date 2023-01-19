#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

char *argv[] = {"sh", 0};
char *envp[] = {"TEST_ENV=FROM_INIT", 0};

void debug(char *file, int line, char *fmt) {
    asm("mov     x8, #511\n"
        "svc     #0\n"
        "ret\n");
}
#define DEBUG(fmt) debug(__FILE__, __LINE__, fmt)

int main() {
    int pid, wpid;

    DEBUG("");

    if (open("console", O_RDWR) < 0) {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    DEBUG("");
    dup(0); // stdout
    dup(0); // stderr

    DEBUG("");
    while (1) {
        printf("init: starting sh\n");
    DEBUG("");
        pid = fork();
    DEBUG("");
        if (pid < 0) {
            printf("init: fork failed\n");
            exit(1);
        }
    DEBUG("");
        if (pid == 0) {
            execve("sh", argv, envp);
            printf("init: exec sh failed\n");
            exit(1);
        }
        while ((wpid = wait(NULL)) >= 0 && wpid != pid)
            printf("zombie!\n");
    }

    return 0;
}
