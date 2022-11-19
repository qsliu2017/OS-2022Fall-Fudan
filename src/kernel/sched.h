#pragma once

#include <kernel/cpu.h>
#include <kernel/proc.h>

#define RR_TIME 1000

struct scheduler_
{
  // update the state of current running entity
  void (*update_old)(struct schinfo *);

  // pick next running entity
  struct proc *(*pick_next)(struct schqueue *);

  void (*update_new)(struct schinfo *);

  // add the schinfo node to the schqueue of its parent
  void (*activacte)(struct schinfo *);
};

typedef struct scheduler_ const *scheduler;

extern struct scheduler_ cfs_scheduler, idle_scheduler;

void init_schinfo(struct schinfo *, bool group);
void init_schqueue(struct schqueue *);

bool _activate_proc(struct proc *, bool onalert);
#define activate_proc(proc) _activate_proc(proc, false)
#define alert_proc(proc) _activate_proc(proc, true)
WARN_RESULT bool is_zombie(struct proc *);
WARN_RESULT bool is_unused(struct proc *);
void _acquire_sched_lock();
#define lock_for_sched(checker) (checker_begin_ctx(checker), _acquire_sched_lock())
void _sched(enum procstate new_state);
// MUST call lock_for_sched() before sched() !!!
#define sched(checker, new_state) (checker_end_ctx(checker), _sched(new_state))
#define yield() (_acquire_sched_lock(), _sched(RUNNABLE))

WARN_RESULT struct proc *thisproc();

void sched_timer_handler(struct timer *);

void dump_sched();
