#include <kernel/sched.h>
#include <common/spinlock.h>
#include <common/string.h>
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
extern NO_RETURN void kernel_entry(u64);
extern NO_RETURN void proc_entry();

/* Public lock for sched */
static SpinLock sched_lock;

define_init(sched)
{
    init_spinlock(&sched_lock);

    idle_container.schqueue.scheduler = &idle_scheduler;
    for (auto i = 0; i < NCPU; i++)
    {
        auto sched = &cpus[i].sched;

        auto idle = &sched->idle;
        idle->container = &idle_container;
        idle->schinfo.state = RUNNING;
        cpus[i].sched.running = idle;

        auto timer = &sched->sched_timer;
        timer->elapse = 89;
        timer->handler = sched_timer_handler;
    }

    start_proc(get_root_proc(), kernel_entry, 0);
}

struct proc *thisproc()
{
    return cpus[cpuid()].sched.running;
}

void init_schinfo(struct schinfo *p, bool group)
{
    memset(p, 0, sizeof(*p));
    p->state = UNUSED;
    p->group = group;
}

void init_schqueue(struct schqueue *p)
{
    memset(p, 0, sizeof(*p));
    p->scheduler = &cfs_scheduler;
}

void _acquire_sched_lock()
{
    _acquire_spinlock(&sched_lock);
}

void _release_sched_lock()
{
    _release_spinlock(&sched_lock);
}

bool is_zombie(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->schinfo.state == ZOMBIE;
    _release_sched_lock();
    return r;
}

static inline scheduler __group_scheduler(struct container *c)
{
    return c->schqueue.scheduler;
}

static inline scheduler __proc_scheduler(struct proc *p)
{
    return __group_scheduler(p->container);
}

bool _activate_proc(struct proc *p, bool onalert)
{
    bool r;
    _acquire_sched_lock();
    switch (p->schinfo.state)
    {
    case RUNNABLE:
    case RUNNING:
    case ZOMBIE:
        r = false;
        break;
    case DEEPSLEEPING:
        r = !onalert;
        break;
    case SLEEPING:
    case UNUSED:
        r = true;
        break;
    default:
        PANIC();
    }
    if (r){
        __proc_scheduler(p)->activacte(&p->schinfo);
        p->schinfo.state = RUNNABLE;
    }
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->schinfo.state == UNUSED;
    _release_sched_lock();
    return r;
}

static void idle_update_old(struct schinfo *i)
{
    struct proc *p = container_of(i, struct proc, schinfo);
    ASSERT(&cpus[cpuid()].sched.idle == p);
}

static struct proc *idle_pick_next(struct schqueue *q)
{
    UNUSE(q);
    return &cpus[cpuid()].sched.idle;
}

static void idle_update_new(struct schinfo *p)
{
    UNUSE(p);
}

static void idle_activate(struct schinfo *p)
{
    UNUSE(p);
}

struct scheduler_ idle_scheduler = {
    .update_old = idle_update_old,
    .pick_next = idle_pick_next,
    .update_new = idle_update_new,
    .activacte = idle_activate,
};

static const scheduler sched_q[] = {&cfs_scheduler, &idle_scheduler};

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

    this->schinfo.state = new_state;
    __proc_scheduler(this)->update_old(&this->schinfo);
    if (new_state == RUNNABLE)
        __proc_scheduler(this)->activacte(&this->schinfo);

    struct proc *next = NULL;
    for (usize i = 0; i < sizeof(sched_q) / sizeof(sched_q[0]); i++)
    {
        if ((next = sched_q[i]->pick_next(&root_container.schqueue)) != NULL)
            break;
    }
    ASSERT(next);

    __proc_scheduler(next)->update_new(&next->schinfo);

    ASSERT(next->schinfo.state == RUNNABLE);
    next->schinfo.state = RUNNING;
    cpus[cpuid()].sched.running = next;

    if (next != this)
    {
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
