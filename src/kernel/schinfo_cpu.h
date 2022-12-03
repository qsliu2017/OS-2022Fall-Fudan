#pragma once

#include <common/list.h>
#include <kernel/cpu.h>
#include <kernel/proc.h>

// embedded data for cpus
struct sched {
    struct proc *running;
    struct proc idle;
    struct timer sched_timer;
};
