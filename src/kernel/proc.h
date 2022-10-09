#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>

enum procstate
{
    UNUSED,
    RUNNABLE,
    RUNNING,
    SLEEPING,
    ZOMBIE
};

typedef struct UserContext
{
    // TODO: customize your trap frame

} UserContext;

typedef struct KernelContext
{
    // TODO: customize your context

    // x19-x30 are callee-saved register

    u64 x19; /* for new process, x19 is used as x30 */
    u64 x20; /* for new process, x19 is used as x0(agrc) */
    u64 x21; /* for new process, x19 is used as x1(agrv) */
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
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;

    Semaphore childexit;
    ListNode children_exit;
    ListNode children_other;
    SpinLock children_lock;

    struct proc *parent;
    ListNode sibling;

    ListNode ptnode;

    struct schinfo schinfo;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;
};

struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int *exitcode);

void dump_proc(struct proc const *p);
