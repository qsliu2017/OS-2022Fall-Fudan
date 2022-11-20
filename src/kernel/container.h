#pragma once

#include <common/bitmap.h>
#include <kernel/proc.h>
#include <kernel/schinfo.h>

#define NLOCAL 512

struct container
{
    struct container *parent;
    struct proc *rootproc;

    struct schinfo schinfo;
    struct schqueue schqueue;

    Bitmap(localids, NLOCAL);
};

extern struct container root_container, idle_container;

struct container *create_container(void (*root_entry)(), u64 arg);
void set_container_to_this(struct proc *);

int alloc_localpid(struct container *);
void free_localpid(struct container *, int localpid);
