/* File descriptors */

#include "file.h"
#include "fs.h"
#include "fs/cache.h"
#include "fs/pipe.h"
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/spinlock.h>
#include <fs/inode.h>
#include <kernel/mem.h>

static struct ftable ftable;

void init_ftable() {
    for (int i = 0; i < NFILE; i++) {
        File *f = &ftable.file[i];
        f->type = FD_NONE;
        f->ref = 0;
    }
}

void init_oftable(struct oftable *oftable) {
    init_spinlock(&oftable->lock);
    for (int i = 0; i < NR_OPEN_DEFAULT; i++) {
        oftable->file[i] = NULL;
    }
}

/* Allocate a file structure. */
struct file *filealloc() {
    File *f = NULL;
    for (File *it = ftable.file; it < ftable.file + NFILE; it++) {
        if (it->ref == 0) {
            f = it;
            f->ref++;
            f->off = 0;
        }
    }
    return f;
}

/* Increment ref count for file f. */
struct file *filedup(struct file *f) {
    f->ref++;
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file *f) {
    if (--f->ref > 0) {
        return;
    }
    switch (f->type) {
    case FD_PIPE:
        pipeClose(f->pipe, f->writable);
        break;
    case FD_INODE: {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, f->ip);
        bcache.end_op(&ctx);
    } break;
    default:
        PANIC();
    }
}

/* Get metadata about file f. */
int filestat(struct file *f, struct stat *st) {
    stati(f->ip, st);
    return 0;
}

/* Read from file f. */
isize fileread(struct file *f, char *addr, isize n) {
    usize cnt = inodes.read(f->ip, (void *)addr, f->off, n);
    f->off += cnt;
    return cnt;
}

/* Write to file f. */
isize filewrite(struct file *f, char *addr, isize n) {
    OpContext ctx;
    bcache.begin_op(&ctx);
    usize cnt = inodes.write(&ctx, f->ip, (void *)addr, f->off, n);
    bcache.end_op(&ctx);
    f->off += cnt;
    return cnt;
}
