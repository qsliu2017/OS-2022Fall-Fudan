#include <common/list.h>
#include <common/rbtree.h>
#include <common/string.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

/* Public lock for proc tree */
static SpinLock proc_lock;

struct proc procs[NPROC];

/* UNUSED procs rbtree root */
static struct rb_root_ free_root;

static bool __proc_cmp(rb_node lnode, rb_node rnode) { return lnode < rnode; }

define_early_init(root_proc) {
    init_spinlock(&proc_lock);
    for (int i = 0; i < NPROC; i++) {
        ASSERT(!_rb_insert(&procs[i].node, &free_root, __proc_cmp));
    }

    auto rp = create_proc();
    ASSERT(rp == get_root_proc());

    rp->parent = rp;
}

struct proc *create_proc() {
    raii_acquire_spinlock(&proc_lock, create_proc);

    auto first = _rb_first(&free_root);
    ASSERT(first);
    _rb_erase(first, &free_root);
    struct proc *p = container_of(first, struct proc, node);

    memset(p, 0, sizeof(*p));

    /* init private fields */

    init_pgdir(&p->pgdir);
    p->kstack = kalloc_page();
    p->ucontext = p->kstack + PAGE_SIZE - sizeof(UserContext);
    p->kcontext = (void *)p->ucontext - sizeof(KernelContext);

    /* init shared fields */

    p->killed = false;
    init_sem(&p->childexit, 0);
    p->parent = NULL;
    p->container = &root_container;

    return p;
}

void set_parent_to_this(struct proc *proc) {
    raii_acquire_spinlock(&proc_lock, set_parent_to_this);

    auto this = thisproc();
    proc->parent = this;
    ASSERT(!_rb_insert(&proc->node, &this->child_root, __proc_cmp));
}

void proc_entry() {
    asm("bl _release_sched_lock\n"
        "mov x30, x19\n"
        "mov x0, x20\n"
        "mov x1, x21\n");
}

int start_proc(struct proc *p, void (*entry)(u64), u64 arg) {
    raii_acquire_spinlock(&proc_lock, start_proc);

    if (!p->parent) {
        auto root = get_root_proc();
        p->parent = root;
        ASSERT(!_rb_insert(&p->node, &root->child_root, __proc_cmp));
    }

    /* init sched.c */
    init_schinfo(&p->schinfo, false);

    // setup the kcontext to make the proc start with proc_entry(entry, arg)
    ASSERT(p->kstack != NULL);
    ASSERT(p->kcontext != NULL);
    p->kcontext->x19 = (u64)entry;
    p->kcontext->x20 = arg;
    p->kcontext->x29 = (u64)p->kcontext;
    p->kcontext->x30 = (u64)proc_entry;

    p->localpid = alloc_localpid(p->container);

    // activate the proc and return its pid
    activate_proc(p);

    return p->localpid;
}

NO_RETURN void exit(int code) {
    _acquire_spinlock(&proc_lock);
    auto this = thisproc();
    ASSERT(this->container->rootproc != this);
    auto root = this->container->rootproc;

    ASSERT(this != root);

    for (rb_node first; (first = _rb_first(&this->child_root));) {
        struct proc *child = container_of(first, struct proc, node);
        _rb_erase(first, &this->child_root);
        child->parent = root;
        ASSERT(!_rb_insert(first, &root->child_root, __proc_cmp));
    }

    for (rb_node first; (first = _rb_first(&this->exit_root));) {
        struct proc *child = container_of(first, struct proc, node);
        _rb_erase(first, &this->exit_root);
        child->parent = root;
        ASSERT(!_rb_insert(first, &root->exit_root, __proc_cmp));
        post_sem(&root->childexit);
    }

    auto parent = this->parent;
    _rb_erase(&this->node, &parent->child_root);
    ASSERT(!_rb_insert(&this->node, &parent->exit_root, __proc_cmp));
    this->exitcode = code;
    post_sem(&parent->childexit);

    setup_checker(_exit);
    lock_for_sched(_exit);
    _release_spinlock(&proc_lock);
    sched(_exit, ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int *exitcode, int *pid) {
    raii_acquire_spinlock(&proc_lock, wait);

    auto this = thisproc();

    if (!_rb_first(&this->child_root) && !_rb_first(&this->exit_root))
        return -1;

    _release_spinlock(&proc_lock);
    ASSERT(wait_sem(&this->childexit));
    _acquire_spinlock(&proc_lock);

    auto first = _rb_first(&this->exit_root);
    ASSERT(first);
    struct proc *child = container_of(first, struct proc, node);
    ASSERT(is_zombie(child));

    _rb_erase(&child->node, &this->exit_root);
    *exitcode = child->exitcode;
    free_pgdir(&child->pgdir);
    ASSERT(child->kstack);
    kfree_page(child->kstack);
    *pid = get_pid(child);
    free_localpid(child->container, child->localpid);

    ASSERT(!_rb_insert(&child->node, &free_root, __proc_cmp));

    return child->localpid;
}

int kill(int pid) {
    ASSERT(0 < pid && pid < NPROC);
    raii_acquire_spinlock(&proc_lock, kill);

    auto p = &procs[pid];
    ASSERT(p->container->rootproc != p);

    if (_rb_lookup(&p->node, &free_root, __proc_cmp))
        return -1;

    p->killed = true;
    alert_proc(p);
    return 0;
}

void dump_proc(struct proc const *p) {
    printk("struct proc\n"
           "{\n"
           "    bool killed = %d;\n"
           "    int pid = %d;\n"
           "    int exitcode = %d;\n"
           "    struct proc *parent = %p;\n"
           "    void *kstack = %p;\n"
           "    UserContext *ucontext = %p;\n"
           "    KernelContext *kcontext = %p;\n"
           "};\n",
           p->killed, get_pid(p), p->exitcode, p->parent, p->kstack,
           p->ucontext, p->kcontext);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    /* TODO: Your code here. */
}