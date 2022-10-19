#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc root_proc;

/* Public lock for proc tree */
static SpinLock proc_lock;

static inline void init_proc(struct proc *p)
{
    /* init constant fields */

    static volatile int pid = 0;
    p->pid = __atomic_fetch_add(&pid, 1, __ATOMIC_RELAXED); /* atomic next pid */
    p->idle = false;

    /* init private fields */

    init_pgdir(&p->pgdir);
    p->kstack = NULL;
    p->ucontext = NULL;
    p->kcontext = NULL;

    /* init sched.c */

    init_schinfo(&p->schinfo);

    /* init shared fields */

    p->killed = false;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    p->parent = NULL;
    init_list_node(&p->sibling);
}

define_early_init(root_proc)
{
    init_spinlock(&proc_lock);

    auto rp = &root_proc;

    init_proc(rp);

    rp->parent = rp;
    rp->state = RUNNING;
}

struct proc *create_proc()
{
    struct proc *p = kalloc(sizeof(struct proc));

    init_proc(p);

    p->kstack = kalloc_page();
    p->ucontext = p->kstack + PAGE_SIZE - sizeof(UserContext);
    p->kcontext = (void *)p->ucontext - sizeof(KernelContext);

    return p;
}

void set_parent_to_this(struct proc *proc)
{
    raii_acquire_spinlock(&proc_lock, 0);
    auto this = thisproc();
    proc->parent = this;
    _merge_list(&this->children, &proc->sibling);
}

static void proc_entry()
{
    asm(
        "bl _release_sched_lock\n"
        "mov x30, x19\n"
        "mov x0, x20\n"
        "mov x1, x21\n");
}

int start_proc(struct proc *p, void (*entry)(u64), u64 arg)
{
    raii_acquire_spinlock(&proc_lock, 0);
    if (p->parent == NULL)
    {
        p->parent = &root_proc;
        _merge_list(&root_proc.children, &p->sibling);
    }
    else
    {
        ASSERT(p->parent == thisproc());
    }

    // setup the kcontext to make the proc start with proc_entry(entry, arg)
    ASSERT(p->kstack != NULL);
    ASSERT(p->kcontext != NULL);
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;
    p->kcontext->x29 = (u64)p->kcontext;
    p->kcontext->x30 = (u64)proc_entry;

    // activate the proc and return its pid
    activate_proc(p);

    return p->pid;
}

NO_RETURN void exit(int code)
{
    _acquire_spinlock(&proc_lock);
    auto this = thisproc();

    while (!_empty_list(&this->children))
    {
        struct proc *child = container_of(this->children.next, struct proc, sibling);
        _detach_from_list(&child->sibling);
        _merge_list(&root_proc.children, &child->sibling);
        child->parent = &root_proc;
        if (child->state == ZOMBIE)
            post_sem(&root_proc.childexit);
    }

    auto parent = this->parent;
    this->exitcode = code;
    post_sem(&parent->childexit);

    setup_checker(_exit);
    lock_for_sched(_exit);
    _release_spinlock(&proc_lock);
    sched(_exit, ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int *exitcode)
{
    raii_acquire_spinlock(&proc_lock, 0);
    auto this = thisproc();

    if (_empty_list(&this->children))
        return -1;

    _release_spinlock(&proc_lock);
    ASSERT(wait_sem(&this->childexit));
    _acquire_spinlock(&proc_lock);

    struct proc *child = NULL;
    _for_in_list(ptr, &this->children)
    {
        struct proc *_child = container_of(ptr, struct proc, sibling);
        if (is_zombie(_child))
        {
            child = _child;
            break;
        }
    }

    ASSERT(child != NULL);

    _detach_from_list(&child->sibling);
    *exitcode = child->exitcode;
    free_pgdir(&child->pgdir);
    ASSERT(&child->kstack);
    kfree_page(&child->kstack);
    int pid = child->pid;
    kfree(child);

    return pid;
}

int kill(int pid)
{
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    raii_acquire_spinlock(&proc_lock, 0);
    auto this = thisproc();
    _for_in_list(ptr, &this->children)
    {
        struct proc *child = container_of(ptr, struct proc, sibling);
        if (child->pid == pid)
        {
            child->killed = true;
            activate_proc(child);
            return 0;
        }
    }
    return -1;
}

void dump_proc(struct proc const *p)
{
    printk(
        "struct proc\n"
        "{\n"
        "    bool killed = %d;\n"
        "    bool idle = %d;\n"
        "    int pid = %d;\n"
        "    int exitcode = %d;\n"
        "    enum procstate state = %d;\n"
        "    struct proc *parent = %p;\n"
        "    void *kstack = %p;\n"
        "    UserContext *ucontext = %p;\n"
        "    KernelContext *kcontext = %p;\n"
        "};\n",
        p->killed,
        p->idle,
        p->pid,
        p->exitcode,
        p->state,
        p->parent,
        p->kstack,
        p->ucontext,
        p->kcontext);
}
