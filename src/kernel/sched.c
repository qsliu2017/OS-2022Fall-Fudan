#include <kernel/sched.h>
#include <common/spinlock.h>
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
extern void proc_entry();

static Queue runnable_proc_queue;

/* Public lock for sched */
static SpinLock sched_lock;

define_init(sched)
{
    queue_init(&runnable_proc_queue);

    init_spinlock(&sched_lock);

    for (auto i = 0; i < NCPU; i++)
    {
        auto idle = &cpus[i].sched.idle;
        idle->idle = true;
        idle->pid = -1;
        idle->state = RUNNING;
        idle->pgdir.pt = NULL;

        cpus[i].sched.running = idle;

        auto timer = &cpus[i].sched.sched_timer;
        timer->elapse = 89;
        timer->handler = sched_timer_handler;
    }
    {
        // Ugly trick to fix idle0
        auto idle = &cpus[0].sched.idle;
        idle->state = RUNNABLE;
        idle->kstack = kalloc_page();
        idle->ucontext = idle->kstack + PAGE_SIZE - sizeof(UserContext);
        idle->kcontext = (void *)idle->ucontext - sizeof(KernelContext);
        idle->kcontext->x19 = (u64)idle_entry;
        idle->kcontext->x29 = (u64)idle->kcontext;
        idle->kcontext->x30 = (u64)proc_entry;
    }
    root_proc.state = RUNNING;
    cpus[0].sched.running = &root_proc;
    cpus[0].sched.idle.state = RUNNABLE;
}

static inline void enqueue_runnable_proc(struct proc *p)
{
    queue_lock(&runnable_proc_queue);
    queue_push(&runnable_proc_queue, &p->schinfo.list);
    queue_unlock(&runnable_proc_queue);
}

static inline struct proc *dequeue_runnable_proc()
{
    struct proc *p = NULL;
    queue_lock(&runnable_proc_queue);
    if (!queue_empty(&runnable_proc_queue))
    {
        p = container_of(queue_front(&runnable_proc_queue), struct proc, schinfo.list);
        queue_pop(&runnable_proc_queue);
    }
    queue_unlock(&runnable_proc_queue);
    return p;
}

struct proc *thisproc()
{
    return cpus[cpuid()].sched.running;
}

void init_schinfo(struct schinfo *p)
{
    init_list_node(&p->list);
}

volatile bool SCHED_DEBUG = false;

void _acquire_sched_lock()
{
    _acquire_spinlock(&sched_lock);
    if (SCHED_DEBUG)
        printk("cpu %d/proc %d _acquire_sched_lock\n", cpuid(), thisproc()->pid);
}

void _release_sched_lock()
{
    if (SCHED_DEBUG)
        printk("cpu %d/proc %d _release_sched_lock\n", cpuid(), thisproc()->pid);
    _release_spinlock(&sched_lock);
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
    _acquire_sched_lock();
    bool ret;
    switch (p->state)
    {
    case SLEEPING:
    case UNUSED:
        p->state = RUNNABLE;
        if (!p->idle)
            enqueue_runnable_proc(p);
        ret = true;
        break;
    case RUNNING:
    case RUNNABLE:
        ret = true;
        break;
    case ZOMBIE:
        ret = false;
        break;
    default:
        PANIC();
    }
    _release_sched_lock();
    return ret;
}

bool is_unused(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();

    if (this->killed && new_state != ZOMBIE)
    {
        _release_sched_lock();
        return;
    }

    ASSERT(this->state == RUNNING);
    this->state = new_state;
    switch (new_state)
    {
    case RUNNABLE:
        if (!this->idle)
            enqueue_runnable_proc(this);
        break;
    case SLEEPING:
    case ZOMBIE:
        break;
    default:
        PANIC();
    }
    auto next = dequeue_runnable_proc();
    if (next == NULL)
    {
        next = &cpus[cpuid()].sched.idle;
    }
    cpus[cpuid()].sched.running = next;

    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;

    if (next != this)
    {
        // printk("cpu %d switch from %d to %d\n", cpuid(), this->pid, next->pid);
        // dump_sched();
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

void sched_timer_handler(struct timer *t)
{
    set_cpu_timer(t);
    yield();
}

void dump_sched()
{
    for (auto i = 0; i < NCPU; i++)
    {
        printk("running_proc[%d]:\n", i);
        dump_proc(cpus[i].sched.running);
    }

    printk("runnable_proc:\n");
    queue_lock(&runnable_proc_queue);
    if (queue_empty(&runnable_proc_queue))
    {
        printk("empty\n");
    }
    else
    {
        dump_proc(container_of(runnable_proc_queue.begin, struct proc, schinfo.list));
        _for_in_list(ptr, runnable_proc_queue.begin)
        {
            dump_proc(container_of(ptr, struct proc, schinfo.list));
        }
    }
    queue_unlock(&runnable_proc_queue);
}
