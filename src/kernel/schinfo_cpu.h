#pragma once

#include <kernel/cpu.h>
#include <common/list.h>
#include <kernel/proc.h>

// embedded data for cpus
struct sched
{
  struct proc *running;
  struct proc idle;
  struct timer sched_timer;
};
