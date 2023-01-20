#include <stdlib.h>

#include <common/sem.h>
#include <driver/uart.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r; // Read index
    usize w; // Write index
    usize e; // Edit index
    Semaphore sem;
} input;
static inline char *chptr(usize index) { return &input.buf[index % INPUT_BUF]; }

define_early_init(console) { init_sem(&input.sem, 0); }

#define BACKSPACE 0x08
#define DELETE 0x7F
#define C(x) ((x) - '@') // Control-x

static inline void backspace() {
    uart_put_char('\b');
    uart_put_char(' ');
    uart_put_char('\b');
}

isize console_write(Inode *ip, char *buf, isize n) {
    inodes.lock(ip);

    isize i;
    for (i = 0; i < n && buf[i]; i++) {
        uart_put_char(buf[i]);
    }

    inodes.unlock(ip);

    return i;
}

isize console_read(Inode *ip, char *dst, isize n) {
    inodes.lock(ip);

    isize i;

    for (i = 0; i < n; i++) {
        while (input.r == input.w) {
            bool r;
            if (r = wait_sem(&input.sem), !r) {
                inodes.unlock(ip);
                return -1;
            }
        }

        char c = *chptr(input.r++);
        if (c == C('D')) {
            break;
        }
        dst[i] = c;
        if (c == '\n') {
            i++;
            break;
        }
    }

    inodes.unlock(ip);

    return i;
}

void console_intr() {
    for (char c; (c = uart_get_char()) != (char)-1;) {
        if (c == '\r')
            c = '\n';

        switch (c) {
        case BACKSPACE:
        case DELETE:
            if (input.w < input.e) {
                input.e--;
                backspace();
            }
            break;
        case C('U'): /* Delete a line */
            while (input.w < input.e && *chptr(input.e - 1) != '\n') {
                input.e--;
                backspace();
            }
            break;
        case C('C'): /* Kill */
            exit(-1);
            break;
        default:
            if (input.e < input.r + INPUT_BUF) {
                uart_put_char(c);
                *chptr(input.e++) = c;

                if (c == '\n' || c == C('D') ||
                    input.e == input.r + INPUT_BUF) {
                    input.w = input.e;
                    post_all_sem(&input.sem);
                }
            }
        }
    }
}
