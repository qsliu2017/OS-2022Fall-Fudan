#include <common/spinlock.h>
#include <common/string.h>
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

extern NO_RETURN void kernel_entry(u64);

/* Public lock for sched */
static SpinLock sched_lock;

define_init(sched)
{
    init_spinlock(&sched_lock);

    for (auto i = 0; i < NCPU; i++)
    {
        auto sched = &cpus[i].sched;

        auto idle = &sched->idle;
        idle->state = RUNNING;
        cpus[i].sched.running = idle;

        auto timer = &sched->sched_timer;
        timer->elapse = 1;
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
    p->group = group;
}

void init_schqueue(struct schqueue *q)
{
    memset(q, 0, sizeof(*q));
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
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc *p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

static bool cfs_cmp(rb_node lnode, rb_node rnode)
{
    struct schinfo *l = container_of(lnode, struct schinfo, sch_node),
                   *r = container_of(rnode, struct schinfo, sch_node);
    return l->vruntime == r->vruntime ? l < r : l->vruntime < r->vruntime;
}

// add the schinfo node of the group to the schqueue of its parent
void activate_group(struct container *group)
{
    auto parent = group->parent;
    if (parent == group)
        return;
    ASSERT(!_rb_insert(&group->schinfo.sch_node, &parent->schqueue.sch_root, cfs_cmp));
    if (parent->schqueue.sch_cnt++ == 0)
        activate_group(parent);
}

// if the proc->state is RUNNING/RUNNABLE, do nothing and return false
// if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
// if the proc->state is DEEPSLEEING, do nothing if onalert or activate it if else, and return the corresponding value.
bool _activate_proc(struct proc *p, bool onalert)
{
    bool r = false;
    _acquire_sched_lock();
    switch (p->state)
    {
    case RUNNING:
    case RUNNABLE:
        r = false;
        break;
    case SLEEPING:
    case UNUSED:
        r = true;
        break;
    case DEEPSLEEPING:
        r = !onalert;
        break;
    default:
        break;
    }
    if (r)
    {
        p->state = RUNNABLE;
        auto parent = p->container;
        ASSERT(!_rb_insert(&p->schinfo.sch_node, &parent->schqueue.sch_root, cfs_cmp));
        // if group was empty, activate it
        if (parent->schqueue.sch_cnt++ == 0)
            activate_group(parent);
    }
    _release_sched_lock();
    return r;
}

// update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
static void update_this_state(enum procstate new_state)
{
    auto this = thisproc();
    this->state = new_state;

    // simply update state for idle
    if (this == &cpus[cpuid()].sched.idle)
        return;

    // calculate runtime delta and update process
    u64 delta = get_timestamp() - this->exec_start;
    this->schinfo.vruntime += delta;

    // update ancestor container
    for (struct container *group = this->container; group != group->parent; group = group->parent)
    {
        rb_node child = &group->schinfo.sch_node;
        rb_root parent = &group->parent->schqueue.sch_root;

        // if container already in its parent's schqueue, erase and re-insert for maintaining rbtree
        bool update = !!_rb_lookup(child, parent, cfs_cmp);
        group->schinfo.vruntime += delta;
        if (update)
        {
            _rb_erase(child, parent);
            ASSERT(!_rb_insert(child, parent, cfs_cmp));
        }
    }

    // add it to sch queue if RUNNABLE
    if (new_state == RUNNABLE)
    {
        auto parent = this->container;
        ASSERT(!_rb_insert(&this->schinfo.sch_node, &parent->schqueue.sch_root, cfs_cmp));
        // if group was empty, activate it
        if (parent->schqueue.sch_cnt++ == 0)
            activate_group(parent);
    }
}

// choose the next process to run, and return idle if no runnable process
static struct proc *pick_next()
{
    struct proc *next = NULL;

    // try to find the leftmost node of CFS tree
    for (auto container = &root_container;;)
    {
        auto first = _rb_first(&container->schqueue.sch_root);
        if (!first)
            break;
        struct schinfo *entity = container_of(first, struct schinfo, sch_node);
        if (!entity->group)
        {
            next = container_of(entity, struct proc, schinfo);
            break;
        }
        container = container_of(entity, struct container, schinfo);
    }

    // return idle if no runnable process
    if (!next)
        return &cpus[cpuid()].sched.idle;

    // remove proc from its container schqueue
    _rb_erase(&next->schinfo.sch_node, &next->container->schqueue.sch_root);

    // remove empty container from its parent schqueue
    // bottom-up, recursive
    for (struct container *child = next->container, *parent = child->parent; --child->schqueue.sch_cnt == 0 && child != parent; child = parent, parent = parent->parent)
    {
        _rb_erase(&child->schinfo.sch_node, &parent->schqueue.sch_root);
    }

    return next;
}

// update thisproc to the choosen process, and reset the clock interrupt if need
static void update_this_proc(struct proc *p)
{
    cpus[cpuid()].sched.running = p;
    p->exec_start = get_timestamp();
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
