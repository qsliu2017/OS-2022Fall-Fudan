#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

static SleepLock sched_lock;

static struct proc *running_proc[NCPU];
static struct proc *idle[NCPU];
static Queue runnable_proc;

define_early_init(sched)
{
    init_sem(&sched_lock, 1);
    for (auto i = 0; i < NCPU; i++)
    {
        running_proc[i] = NULL;
        idle[i] = create_proc();
        idle[i]->idle = true;
    }
    queue_init(&runnable_proc);
}

struct proc *thisproc()
{
    // TODO: return the current process
    return running_proc[cpuid()];
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->list);
}

void _acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    wait_sem(&sched_lock);
}

void _release_sched_lock()
{
    // TODO: release the sched_lock if need
    post_sem(&sched_lock);
}

bool is_zombie(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc *p)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    switch (p->state)
    {
    case RUNNING:
    case RUNNABLE:
        return false;
    case SLEEPING:
    case UNUSED:
        p->state = RUNNABLE;
        queue_lock(&runnable_proc);
        queue_push(&runnable_proc, &p->schinfo.list);
        queue_unlock(&runnable_proc);
        return true;
    default:
        PANIC();
    }
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto this = thisproc();
    switch (new_state)
    {
    case RUNNABLE:
        queue_lock(&runnable_proc);
        queue_push(&runnable_proc, &this->schinfo.list);
        queue_unlock(&runnable_proc);
        this->state = new_state;
        running_proc[cpuid()] = NULL;
        return;
    case SLEEPING:
    case ZOMBIE:
        this->state = new_state;
        running_proc[cpuid()] = NULL;
        return;
    case RUNNING:
        return;
    case UNUSED:
    default:
        PANIC();
    }
}

static struct proc *pick_next()
{
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    struct proc *next = idle[cpuid()];
    queue_lock(&runnable_proc);
    if (queue_empty(&runnable_proc))
        goto ret;
    next = container_of(queue_front(&runnable_proc), struct proc, schinfo.list);
    queue_pop(&runnable_proc);
ret:
    queue_unlock(&runnable_proc);
    return next;
}

static void update_this_proc(struct proc *p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    ASSERT(running_proc[cpuid()] == NULL);
    running_proc[cpuid()] = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}
