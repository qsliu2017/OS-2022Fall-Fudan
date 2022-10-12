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

extern NO_RETURN void idle_entry(u64);

extern struct proc *root_proc;

static struct proc *running_proc[NCPU];
static Queue runnable_proc;
static struct proc *idle_proc[NCPU];

define_init(sched)
{
    queue_init(&runnable_proc);
    running_proc[0] = root_proc;
    root_proc->state = RUNNING;
    for (auto i = 0; i < NCPU; i++)
    {
        idle_proc[i] = create_proc();
        idle_proc[i]->idle = true;

        /* idle_proc should not be child of root, otherwise root_proc will wait for it forever. */
        idle_proc[i]->parent = idle_proc[i];

        start_proc(idle_proc[i], idle_entry, 0);
        if (i != 0)
        {
            running_proc[i] = idle_proc[i];
            running_proc[i]->state = RUNNING;
        }
    }
}

struct proc *thisproc()
{
    return running_proc[cpuid()];
}

void init_schinfo(struct schinfo *p)
{
    init_list_node(&p->list);
}

void _acquire_sched_lock()
{
}

void _release_sched_lock()
{
}

bool is_zombie(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

// if the proc->state is RUNNING/RUNNABLE, do nothing
// if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
// else: panic
bool activate_proc(struct proc *p)
{
    // TODO
    switch (p->state)
    {
    case RUNNING:
    case RUNNABLE:
        return false;
    case SLEEPING:
    case UNUSED:
        p->state = RUNNABLE;
        if (!p->idle)
        {
            queue_lock(&runnable_proc);
            queue_push(&runnable_proc, &p->schinfo.list);
            queue_unlock(&runnable_proc);
        }
        return true;
    default:
        PANIC();
    }
bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

// update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    auto this = thisproc();
    switch (new_state)
    {
    case RUNNABLE:
        if (!this->idle)
        {
            queue_lock(&runnable_proc);
            queue_push(&runnable_proc, &this->schinfo.list);
            queue_unlock(&runnable_proc);
        }
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
    struct proc *next = idle_proc[cpuid()];
    queue_lock(&runnable_proc);
    if (!queue_empty(&runnable_proc))
    {
        next = container_of(queue_front(&runnable_proc), struct proc, schinfo.list);
        queue_pop(&runnable_proc);
    }
    queue_unlock(&runnable_proc);
    return next;
}

static void update_this_proc(struct proc *p)
{
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

void dump_sched()
{
    for (auto i = 0; i < NCPU; i++)
    {
        printk("running_proc[%d]:\n\t", i);
        dump_proc(running_proc[i]);
    }

    printk("runnable_proc:\n\t");
    queue_lock(&runnable_proc);
    if (queue_empty(&runnable_proc))
    {
        printk("empty\n\t");
    }
    else
    {
        dump_proc(container_of(runnable_proc.begin, struct proc, schinfo.list));
        _for_in_list(ptr, runnable_proc.begin)
        {
            dump_proc(container_of(ptr, struct proc, schinfo.list));
        }
    }
    queue_unlock(&runnable_proc);
}
