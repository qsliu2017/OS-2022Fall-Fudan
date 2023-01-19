#include <stdlib.h>

#include <common/spinlock.h>
#include <driver/uart.h>
#include <kernel/console.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r; // Read index
    usize w; // Write index
    usize e; // Edit index
} input;
static inline char *chptr(usize index) { return &input.buf[index % INPUT_BUF]; }
#define BACKSPACE 0x08
#define C(x) ((x) - '@') // Control-x

static void console_putc(char c) {
    if (c == BACKSPACE) {
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
        return;
    }
    uart_put_char(c);
}

isize console_write(Inode *ip, char *buf, isize n) {
    inodes.lock(ip);

    isize i;
    for (i = 0; i < n; i++) {
        if (buf[i] == 0) {
            break;
        }
        console_putc(buf[i]);
    }

    inodes.unlock(ip);

    return i;
}

isize console_read(Inode *ip, char *dst, isize n) {
    inodes.lock(ip);

    isize i;

    for (i = 0; i < n && input.r < input.w; i++) {
        if (!dst[i]) {
            break;
        }
        dst[i] = *chptr(input.r++);
    }

    inodes.unlock(ip);

    return i;
}

void console_intr() {
    char c = uart_get_char();
    switch (c) {
    case BACKSPACE:
        if (input.w < input.e) {
            input.e--;
            console_putc(BACKSPACE);
        }
        break;
    case C('U'): /* Delete a line */
        while (input.w < input.e && *chptr(input.e - 1) != '\n') {
            input.e--;
            console_putc(BACKSPACE);
        }
        break;
    case C('C'): /* Kill */
        exit(-1);
        break;
    case C('D'): /* EOF */
    case '\n':
        if (input.e - input.r < INPUT_BUF) {
            console_putc(0);
            *chptr(input.e++) = 0;
            input.w = input.e;
        }
        break;
    default:
        if (input.e - input.r < INPUT_BUF) {
            console_putc(c);
            *chptr(input.e++) = c;
        }
        input.w++;
    }
}
