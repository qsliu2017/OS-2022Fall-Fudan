#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/rbtree.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>
#include <kernel/container.h>

#define NPROC 128

typedef struct UserContext
{
    /* When an exception causes a mode change, the core automatically
     * - saves the cpsr to the spsr of the exception mode
     * - saves the pc to the lr of the exception mode
     * - sets the cpsr to the exception mode
     * - sets pc to the address of the exception handler
     */

    u64 spsr; /* saved program state register */
    u64 elr;
    u64 sp;

    u64 x0;
    u64 x1;
    u64 x2;
    u64 x3;
    u64 x4;
    u64 x5;
    u64 x6;
    u64 x7;
    u64 x8;
    u64 x9;
    u64 x10;
    u64 x11;
    u64 x12;
    u64 x13;
    u64 x14;
    u64 x15;
    u64 x16;
    u64 x17;
    u64 x18;
    u64 x19;
    u64 x20;
    u64 x21;
    u64 x22;
    u64 x23;
    u64 x24;
    u64 x25;
    u64 x26;
    u64 x27;
    u64 x28;
    u64 x29;
    u64 x30;

} UserContext;

typedef struct KernelContext
{
    // x19-x30 are callee-saved register

    u64 x19; /* for new process, x19 is used as x30 */
    u64 x20; /* for new process, x20 is used as x0(argc) */
    u64 x21; /* for new process, x21 is used as x1(argv) */
    u64 x22;
    u64 x23;
    u64 x24;
    u64 x25;
    u64 x26;
    u64 x27;
    u64 x28;
    u64 x29; /* x29 is also used as frame pointer */
    u64 x30; /* x30 is also used as return address */

} KernelContext;

struct proc
{
    /* Constant fields */

    int localpid;

    /* Private fields */

    struct pgdir pgdir;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;

    /* Owned by sched.c */

    struct schinfo schinfo;

    /* Shared fields. Should hold proc lock first. */

    bool killed;
    int exitcode;
    Semaphore childexit;
    struct proc *parent;
    struct rb_root_ child_root;
    struct rb_root_ exit_root;
    struct rb_node_ node; // owned by free rb_tree or parent
    struct container *container;
};

extern struct proc procs[NPROC];

inline int get_pid(struct proc const *p)
{
    return p - procs;
}

inline struct proc *get_root_proc()
{
    return &procs[0];
}

WARN_RESULT struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode, int *pid);
WARN_RESULT int kill(int pid);

void set_parent_to_this(struct proc *);

void dump_proc(struct proc const *p);
