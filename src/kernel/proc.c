#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc *root_proc;

define_early_init(root_proc)
{
    root_proc = NULL;
    root_proc = create_proc();
    root_proc->parent = root_proc;
    // start_proc(root_proc, kernel_entry, 123456);
}

void kernel_entry();
void proc_entry();

void set_parent_to(struct proc *parent, struct proc *child)
{
    ASSERT(child->parent == NULL || child->parent == parent);
    ASSERT(_empty_list(&child->sibling));
    child->parent = parent;
    // setup_checker(0);
    // acquire_sleeplock(0, &parent->children_lock);
    if (child->state == ZOMBIE)
    {
        merge_list(&parent->children_lock, &parent->children_exit, &child->sibling);
        post_sem(&parent->childexit);
    }
    else
    {
        merge_list(&parent->children_lock, &parent->children_other, &child->sibling);
    }
    // release_sleeplock(0, &parent->children_lock);
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
    // TODO
    // 1. set the exitcode
    this->exitcode = code;
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // setup_checker(0);
    // acquire_sleeplock(0, &this->children_lock);
    for (auto ptr = this->children_other.next; ptr != &this->children_other; ptr = this->children_other.next)
    {
        struct proc *p = container_of(ptr, struct proc, sibling);
        detach_from_list(&this->children_lock, &p->sibling);
        p->parent = NULL;
        set_parent_to(root_proc, p);
    }
    for (auto ptr = this->children_exit.next; ptr != &this->children_exit; ptr = this->children_exit.next)
    {
        struct proc *p = container_of(ptr, struct proc, sibling);
        detach_from_list(&this->children_lock, &p->sibling);
        p->parent = NULL;
        set_parent_to(root_proc, p);
    }
    // release_sleeplock(0, &root_proc->children_lock);
    // 4. sched(ZOMBIE)
    detach_from_list(&this->parent->children_lock, &this->sibling);
    insert_into_list(&this->parent->children_lock, &this->parent->children_exit, &this->sibling);
    post_sem(&this->parent->childexit);
    _sched(ZOMBIE);
    // NOTE: be careful of concurrency

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int *exitcode)
{
    auto this = thisproc();
    int ret = 0;
    // TODO
    // 1. return -1 if no children
    // setup_checker(0);
    // acquire_sleeplock(0, &this->children_lock);
    if (_empty_list(&this->children_exit) && _empty_list(&this->children_other))
    {
        ret = -1;
        goto ret;
    }
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // release_sleeplock(0, &this->children_lock);
    ASSERT(wait_sem(&this->childexit));
    // acquire_sleeplock(0, &this->children_lock);
    auto ptr = this->children_exit.next;
    detach_from_list(&this->children_lock, ptr);
    struct proc *child = container_of(ptr, struct proc, sibling);
    *exitcode = child->exitcode;
    ret = child->pid;
    if (child->kcontext)
    {
        kfree_page(child->kcontext);
    }
    kfree(child);
ret:
    // release_sleeplock(0, &this->children_lock);
    return ret;
    // NOTE: be careful of concurrency
}

const isize KSTACK_SIZE = 4096;

int start_proc(struct proc *p, void (*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    if (p->parent == NULL)
        set_parent_to(root_proc, p);
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    p->kstack = kalloc_page();
    p->kcontext = p->kstack + KSTACK_SIZE - sizeof(KernelContext);
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;
    p->kcontext->x29 = (u64)p->kcontext;
    p->kcontext->x30 = (u64)proc_entry;

    // 3. activate the proc and return its pid
    activate_proc(p);
    return p->pid;
    // NOTE: be careful of concurrency
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
    init_list_node(&p->schinfo.list);
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
