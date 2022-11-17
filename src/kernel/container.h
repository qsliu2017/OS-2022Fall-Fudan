#pragma once

#include <kernel/proc.h>
#include <kernel/schinfo.h>

struct container
{
    struct container *parent;
    struct proc *rootproc;

    struct schinfo schinfo;
    struct schqueue schqueue;

    int localpid;
};

extern struct container root_container;

struct container *create_container(void (*root_entry)(), u64 arg);
void set_container_to_this(struct proc *);
int next_localpid(struct container *);
