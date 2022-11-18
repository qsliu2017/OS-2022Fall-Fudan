#include <common/string.h>
#include <common/list.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

struct container root_container, idle_container;
extern struct proc root_proc;

void activate_group(struct container *group);

void set_container_to_this(struct proc *proc)
{
    proc->container = thisproc()->container;
}

void init_container(struct container *container)
{
    memset(container, 0, sizeof(struct container));
    container->parent = &root_container;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
}

struct container *create_container(void (*root_entry)(), u64 arg)
{
    struct container *c = kalloc(sizeof(struct container));
    init_container(c);
    c->parent = thisproc()->container;
    c->rootproc = create_proc();
    c->rootproc->container = c;
    start_proc(c->rootproc, root_entry, arg);
    return c;
}

int next_localpid(struct container *container)
{
    return __atomic_fetch_add(&container->localpid, 1, __ATOMIC_RELAXED);
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
}
