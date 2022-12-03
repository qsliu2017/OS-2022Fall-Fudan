#include <common/list.h>
#include <common/string.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

struct container root_container, idle_container;

void activate_group(struct container *group);

void set_container_to_this(struct proc *proc) {
    proc->container = thisproc()->container;
}

void init_container(struct container *container) {
    memset(container, 0, sizeof(struct container));
    container->parent = &root_container;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // init_bitmap(container->localids, NLOCAL);
}

struct container *create_container(void (*root_entry)(), u64 arg) {
    struct container *c = kalloc(sizeof(struct container));
    init_container(c);
    c->parent = thisproc()->container;
    c->rootproc = create_proc();
    c->rootproc->container = c;
    start_proc(c->rootproc, root_entry, arg);
    return c;
}

int alloc_localpid(struct container *container) {
    for (int i = 0; i < NLOCAL; i++) {
        if (bitmap_get(container->localids, i))
            continue;
        bitmap_set(container->localids, i);
        return i;
    }
    PANIC();
}

void free_localpid(struct container *container, int localpid) {
    ASSERT(bitmap_get(container->localids, localpid));
    bitmap_clear(container->localids, localpid);
}

define_early_init(root_container) {
    init_container(&root_container);
    root_container.rootproc = get_root_proc();
}
