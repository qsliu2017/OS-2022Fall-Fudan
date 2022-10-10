#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

static void destroy_proc(struct proc *p);

struct proc *root_proc;

define_early_init(root_proc)
{
    root_proc = NULL;
    root_proc = create_proc();
    root_proc->parent = root_proc;
    // start_proc(root_proc, kernel_entry, 123456);
}

static void proc_entry();

/*
 * Set proc's parent, add it to parent's children and notify new parent
 * if proc is ZOMBIE.
 *
 * It is parent's responsibility to remove child from children list.
 */
static inline void set_parent_to(struct proc *parent, struct proc *child)
{
    ASSERT(child->parent == NULL);
    ASSERT(_empty_list(&child->sibling));
    child->parent = parent;
    if (child->state == ZOMBIE)
    {
        merge_list(&parent->children_lock, &parent->children_exit, &child->sibling);
        post_sem(&parent->childexit);
    }
    else
    {
        merge_list(&parent->children_lock, &parent->children_other, &child->sibling);
    }
}

void set_parent_to_this(struct proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    set_parent_to(thisproc(), proc);
}

NO_RETURN void exit(int code)
{
    auto this = thisproc();

    this->exitcode = code;

    {
        // transfer children to the root_proc, and notify the root_proc if there is zombie
        setup_checker(0);
        acquire_spinlock(0, &this->children_lock);
        while (!_empty_list(&this->children_other))
        {
            struct proc *p = container_of(this->children_other.next, struct proc, sibling);
            _detach_from_list(&p->sibling);
            p->parent = NULL;
            set_parent_to(root_proc, p);
        }
        while (!_empty_list(&this->children_exit))
        {
            struct proc *p = container_of(this->children_exit.next, struct proc, sibling);
            _detach_from_list(&p->sibling);
            p->parent = NULL;
            set_parent_to(root_proc, p);
        }
        release_spinlock(0, &this->children_lock);
    }

    {
        // move self from children_other to children_exit; notify parent.
        detach_from_list(&this->parent->children_lock, &this->sibling);
        merge_list(&this->parent->children_lock, &this->parent->children_exit, &this->sibling);
        post_sem(&this->parent->childexit);
    }

    _sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int *exitcode)
{
    auto this = thisproc();

    setup_checker(0);
    acquire_spinlock(0, &this->children_lock);
    if (_empty_list(&this->children_exit) && _empty_list(&this->children_other))
    {
        // return -1 if no children
        release_spinlock(0, &this->children_lock);
        return -1;
    }

    // release children_lock before wait, so child is posible to move in children_exit list
    release_spinlock(0, &this->children_lock);
    // wait for childexit
    ASSERT(wait_sem(&this->childexit));

    // if any child exits, clean it up and return its pid and exitcode
    setup_checker(1);
    acquire_spinlock(1, &this->children_lock);
    auto ptr = this->children_exit.next;
    _detach_from_list(ptr);
    release_spinlock(1, &this->children_lock);

    struct proc *child = container_of(ptr, struct proc, sibling);
    *exitcode = child->exitcode;
    int pid = child->pid;
    destroy_proc(child);
    return pid;
}

int start_proc(struct proc *p, void (*entry)(u64), u64 arg)
{
    // set the parent to root_proc if NULL
    if (p->parent == NULL)
        set_parent_to(root_proc, p);

    // setup the kcontext to make the proc start with proc_entry(entry, arg)
    p->kstack = kalloc_page();
    p->kcontext = p->kstack + PAGE_SIZE - sizeof(KernelContext);
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;
    p->kcontext->x29 = (u64)p->kcontext;
    p->kcontext->x30 = (u64)proc_entry;

    // activate the proc and return its pid
    activate_proc(p);
    return p->pid;
}

void destroy_proc(struct proc *p)
{
    if (p->kstack)
    {
        kfree_page(p->kstack);
    }
    kfree(p);
}

static inline int next_pid()
{
    static volatile int pid_cnt = 0;
    return __atomic_fetch_add(&pid_cnt, 1, __ATOMIC_RELAXED);
}

struct proc *create_proc()
{
    struct proc *p = kalloc(sizeof(struct proc));
    p->killed = false;
    p->idle = false;

    p->pid = next_pid();

    p->state = UNUSED;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children_exit);
    init_list_node(&p->children_other);
    init_spinlock(&p->children_lock);

    init_list_node(&p->ptnode);

    p->parent = NULL;
    init_list_node(&p->sibling);
    init_schinfo(&p->schinfo);
    p->kstack = NULL;
    p->ucontext = NULL;
    p->kcontext = NULL;

    return p;
}

void proc_entry()
{
    asm(
        "mov x30, x19\n\t"
        "mov x0, x20\n\t"
        "mov x1, x21\n\t");
}

void dump_proc(struct proc const *p)
{
    printk(
        "struct proc\n\t"
        "{\n\t"
        "    bool killed = %d;\n\t"
        "    bool idle = %d;\n\t"
        "    int pid = %d;\n\t"
        "    int exitcode = %d;\n\t"
        "    enum procstate state = %d;\n\t"
        "    struct proc *parent = %p;\n\t"
        "    void *kstack = %p;\n\t"
        "    UserContext *ucontext = %p;\n\t"
        "    KernelContext *kcontext = %p;\n\t"
        "};\n\t",
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
