#pragma once

#include "common/spinlock.h"
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <fs/defines.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <sys/stat.h>

typedef struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    int ref;
    char readable;
    char writable;
    union {
        struct pipe *pipe;
        Inode *ip;
    };
    usize off;
} File;

#define NFILE 65536 // Open files per system
struct ftable {
    SpinLock lock;
    File file[NFILE];
};

#define NR_OPEN_DEFAULT 64
struct oftable {
    SpinLock lock;
    File *file[NR_OPEN_DEFAULT];
};

void init_ftable();
void init_oftable(struct oftable *);
void clean_oftable(struct oftable *);

/*
 * Iterate the file table to get a file structure with ref == 0.
 * Set the ref to 1.
 */
struct file *filealloc();

/*
 * f->ref++
 * ref is to prevent the file from being closed while it is being holded by
 * others
 */
struct file *filedup(struct file *f);

/*
 * f->ref--
 * if (f->ref == 0) close the file and the inode
 */
void fileclose(struct file *f);

/*
 * Call stati
 */
int filestat(struct file *f, struct stat *st);

/*
 * Call inodes.read with range [f->off, f->off + n)
 * Increment f->off by the return value.
 */
isize fileread(struct file *f, char *addr, isize n);

/*
 * Call inodes.write with range [f->off, f->off + n)
 * Increment f->off by the return value.
 * There should be a maximum valid value of n.
 */
isize filewrite(struct file *f, char *addr, isize n);
