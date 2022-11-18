#pragma once

#include <common/rbtree.h>
struct proc; // dont include proc.h here
struct scheduler_;
typedef struct scheduler_ const *scheduler;

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
    bool group;

    u64 vruntime;
    u64 start;
};

// embedded data for containers
struct schqueue
{
    scheduler scheduler;
    struct rb_root_ sch_root;
};
