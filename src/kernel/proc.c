#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc root_proc;

void kernel_entry();
void proc_entry();

void set_parent_to_this(struct proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    proc->parent = thisproc();
}

NO_RETURN void exit(int code)
{
    auto this = thisproc();
    // TODO
    // 1. set the exitcode
    this->exitcode = code;
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    _detach_from_list(&this->sibling);
    _merge_list(&root_proc.children, &this->sibling);
    _merge_list(&root_proc.children, &this->children);
    _detach_from_list(&this->children);
    // 4. sched(ZOMBIE)
    _sched(ZOMBIE);
    // NOTE: be careful of concurrency

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int *exitcode)
{
    auto this = thisproc();
    // TODO
    // 1. return -1 if no children
    if (_empty_list(&this->children))
        return -1;
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    if (wait_sem(&this->childexit))
    {
        auto child = this->exitchild;
        *exitcode = child->exitcode;
        kfree(child->kstack);
        return this->exitchild->pid;
    }

    return 0;
    // NOTE: be careful of concurrency
}

int start_proc(struct proc *p, void (*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    if (p->parent == NULL)
        p->parent = &root_proc;
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    p->kcontext = (KernelContext *)kalloc(sizeof(KernelContext));
    // 3. activate the proc and return its pid
    p->state = RUNNABLE;
    return p->pid;
    // NOTE: be careful of concurrency
}

const isize KSTACK_SIZE = 4096;

void init_proc(struct proc *p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    p->killed = false;
    p->idle = false;

    static int pid_cnt = 0;
    p->pid = ++pid_cnt;

    p->state = UNUSED;

    init_sem(&p->childexit, 0);

    p->exitchild = NULL;
    init_list_node(&p->children);
    init_list_node(&p->ptnode);

    p->parent = &root_proc;
    init_list_node(&p->sibling);
    p->kstack = kalloc(KSTACK_SIZE);
    p->ucontext = kalloc(sizeof(UserContext));
    p->kcontext = kalloc(sizeof(KernelContext));

    // NOTE: be careful of concurrency
}

struct proc *create_proc()
{
    struct proc *p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}
