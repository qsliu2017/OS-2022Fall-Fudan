#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/rbtree.h>
#include <common/sem.h>
#include <kernel/container.h>
#include <kernel/pt.h>
#include <kernel/schinfo.h>

#define NPROC 128
#include <fs/file.h>
#include <fs/inode.h>

#define NOFILE 1024 /* open files per process */

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext {
    u64 spsr, elr, sp, tpidr;
    u64 x[32];
    u64 q0[2];
} UserContext;

typedef struct KernelContext {
    u64 lr0, lr, fp;
    u64 x[11];
} KernelContext;

struct proc {
    /* Constant fields */

    int localpid;

    /* Private fields */

    /*
     * Memory layout
     *
     * +----------+
     * |  Kernel  |
     * +----------+  KERNBASE
     * |  Stack   |
     * +----------+  KERNBASE - stksz
     * |   ....   |
     * |   ....   |
     * +----------+  base + sz
     * |   Heap   |
     * +----------+
     * |   Code   |
     * +----------+  base
     * | Reserved |
     * +----------+  0
     *
     */

    usize base, sz;
    usize stksz;

    struct pgdir pgdir;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;

    /* Owned by sched.c */

    struct schinfo schinfo;
    enum procstate state;
    u64 exec_start;

    /* Shared fields. Should hold proc lock first. */

    bool killed;
    int exitcode;
    Semaphore childexit;
    struct proc *parent;
    struct rb_root_ child_root;
    struct rb_root_ exit_root;
    struct rb_node_ node; // owned by free rb_tree or parent
    struct container *container;
    struct oftable oftable;
    Inode *cwd; // current working dictionary
};

extern struct proc procs[NPROC];

inline int get_pid(struct proc const *p) { return p - procs; }

inline struct proc *get_root_proc() { return &procs[0]; }

WARN_RESULT struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode, int *pid);
WARN_RESULT int kill(int pid);

void set_parent_to_this(struct proc *);

void dump_proc(struct proc const *p);
WARN_RESULT int fork();
