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
extern void proc_entry();

/* Public lock for sched */
static SpinLock sched_lock;

define_init(sched)
{
    init_spinlock(&sched_lock);

    for (auto i = 0; i < NCPU; i++)
    {
        auto idle = &cpus[i].sched.idle;
        idle->pid = -1;
        idle->pgdir.pt = NULL;

        cpus[i].sched.running = idle;

        auto timer = &cpus[i].sched.sched_timer;
        timer->elapse = 89;
        timer->handler = sched_timer_handler;
    }
    {
        // Ugly trick to fix idle0
        auto idle = &cpus[0].sched.idle;
        idle->kstack = kalloc_page();
        idle->ucontext = idle->kstack + PAGE_SIZE - sizeof(UserContext);
        idle->kcontext = (void *)idle->ucontext - sizeof(KernelContext);
        idle->kcontext->x19 = (u64)idle_entry;
        idle->kcontext->x29 = (u64)idle->kcontext;
        idle->kcontext->x30 = (u64)proc_entry;
    }
    cpus[0].sched.running = &root_proc;
}

struct proc *thisproc()
{
    return cpus[cpuid()].sched.running;
}

void init_schinfo(struct schinfo *p, bool group)
{
    memset(p, 0, sizeof(*p));
    p->group = group;
}

void init_schqueue(struct schqueue *p)
{
    memset(p, 0, sizeof(*p));
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

static inline struct scheduler *__group_scheduler(struct container *c)
{
    return c->schqueue.scheduler;
}

static inline struct scheduler *__proc_scheduler(struct proc *p)
{
    return __group_scheduler(p->container);
}

void activate_group(struct container *group)
{
    _acquire_sched_lock();
    __group_scheduler(group)->activacte_group(group);
    _release_sched_lock();
}

bool _activate_proc(struct proc *p, bool onalert)
{
    bool r;
    _acquire_sched_lock();
    r = __proc_scheduler(p)->activacte_proc(p, onalert);
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

static void idle_update_old(struct proc *p, enum procstate new_state)
{
    ASSERT(&cpus[cpuid()].sched.idle == p);
    ASSERT(new_state == RUNNABLE);
    p->schinfo.state = RUNNABLE;
}

static struct proc *idle_pick_next()
{
    return &cpus[cpuid()].sched.idle;
}

static void idle_update_new(struct proc *p)
{
    UNUSE(p);
}

static bool idle_activate_proc(struct proc *p, bool onalert)
{
    UNUSE(p);
    UNUSE(onalert);
    PANIC();
}

static void idle_activacte_group(struct container *c)
{
    UNUSE(c);
    PANIC();
}

static const struct scheduler idle_scheduler = {
    .update_old = idle_update_old,
    .pick_next = idle_pick_next,
    .update_new = idle_update_new,
    .activacte_proc = idle_activate_proc,
    .activacte_group = idle_activacte_group,
};

static const struct scheduler *const sched_q[] = {&cfs_scheduler, &idle_scheduler};

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

    __proc_scheduler(this)->update_old(this, new_state);

    struct proc *next = NULL;
    for (usize i = 0; i < sizeof(sched_q) / sizeof(sched_q[0]); i++)
    {
        if ((next = sched_q[i]->pick_next()) != NULL)
            break;
    }
    ASSERT(next);

    __proc_scheduler(next)->update_new(next);

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
