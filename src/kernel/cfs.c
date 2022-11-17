#include <kernel/sched.h>

static void update_old(struct proc *p, enum procstate new_state)
{
  ASSERT(p->schinfo.state == RUNNING);
  ASSERT(p->container->schqueue.scheduler == &cfs_scheduler);

  p->schinfo.state = new_state;

  switch (new_state)
  {
  case RUNNABLE:
    break;
  case SLEEPING:
  case ZOMBIE:
    break;
  default:
    PANIC();
  }
}

static struct proc *pick_next()
{
  return NULL;
}

static void update_new(struct proc *p)
{
  UNUSE(p);
}

static bool activacte_proc(struct proc *p, bool onalert)
{
  UNUSE(p), UNUSE(onalert);
  return 0;
}

static void activacte_group(struct container *c)
{
  UNUSE(c);
}

const struct scheduler cfs_scheduler = {
    .update_old = update_old,
    .pick_next = pick_next,
    .update_new = update_new,
    .activacte_proc = activacte_proc,
    .activacte_group = activacte_group,
};
