#pragma once

#include <common/rbtree.h>
struct proc; // dont include proc.h here

enum procstate
{
    UNUSED,
    RUNNABLE,
    RUNNING,
    SLEEPING,
    DEEPSLEEPING,
    ZOMBIE
};

// embeded data for procs
struct schinfo
{
    struct rb_node_ sch_node;

    enum procstate state;
    u64 vruntime;
    bool group;
};

// embedded data for containers
struct schqueue
{
    struct scheduler *scheduler;
    struct rb_root_ sch_root;
};
