#pragma once

#include <common/rbtree.h>
struct proc; // dont include proc.h here

// embeded data for procs
struct schinfo
{
    struct rb_node_ sch_node;

    bool group;

    u64 vruntime;
};

// embedded data for containers
struct schqueue
{
    struct rb_root_ sch_root;

    u64 sch_cnt;
};
