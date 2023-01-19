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

    if (open("console", O_RDWR) < 0) {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    DEBUG("");
    int cnt=0;
    while (1) {

        printf("init: starting sh(cnt=%d)\n", cnt++);
        pid = fork();
        printf("%s:%d pid=%d\n", __FILE__,__LINE__,pid);
        if (pid < 0) {
            printf("init: fork failed\n");
            exit(1);
        }
        printf("%s:%d pid=%d\n", __FILE__,__LINE__,pid);
        if (pid == 0) {
        DEBUG("");
            execve("sh", argv, envp);
            printf("init: exec sh failed\n");
            exit(1);
        }
        printf("%s:%d pid=%d\n", __FILE__,__LINE__,pid);
        while ((wpid = wait(NULL)) >= 0 && wpid != pid)
            printf("zombie!\n");
        printf("%s:%d pid=%d\n", __FILE__,__LINE__,pid);
    }

    return 0;
}
