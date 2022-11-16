#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>
#include <kernel/container.h>

enum procstate
{
    UNUSED,
    RUNNABLE,
    RUNNING,
    SLEEPING,
    DEEPSLEEPING,
    ZOMBIE
};

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

// ProcLock is spinlock hold by process(thread)
typedef struct
{
    struct proc *volatile holder;
} ProcLock;

struct proc
{
    /* Constant fields */

    int pid;
    bool idle;

    /* Private fields */

    struct pgdir pgdir;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;

    /* Owned by sched.c */

    struct schinfo schinfo;

    /* Shared fields. Should hold proc lock first. */

    bool killed;
    int localpid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    struct proc *parent;
    ListNode sibling;
    struct container *container;
};

extern struct proc root_proc;

WARN_RESULT struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode, int *pid);
WARN_RESULT int kill(int pid);

void set_parent_to_this(struct proc *);

void dump_proc(struct proc const *p);

void init_proclock(ProcLock *);

// Non-block acquire proc lock, return true on success. Panic if already hold by thisproc().
bool _try_acq_proclock(ProcLock *);
// Block acquire proc lock. Panic if already hold by thisproc().
void _acq_proclock(ProcLock *);
// Release proc lock. Panic if not hold by thisproc().
void _rel_proclock(ProcLock *);

// Do NOT use this. Use raii_acq_proclock instead.
ProcLock *__raii_acq_proclock(ProcLock *);
// Do NOT use this.
void __raii_rel_proclock(ProcLock **);
// Block acquire proc lock, release when the scope goes out.
#define raii_acq_proclock(lock, id) ProcLock *__proclock_##id __attribute__((__cleanup__(__raii_rel_proclock))) = __raii_acq_proclock(lock)

// Return true if thisproc() is holding lock
bool holding_proclock(ProcLock *);
