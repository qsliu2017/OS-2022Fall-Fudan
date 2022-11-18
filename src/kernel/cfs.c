#include <common/string.h>
#include <kernel/sched.h>

static void _init_schinfo(struct schinfo *p, bool group)
{
  memset(p, 0, sizeof(*p));
  p->state = UNUSED;
  p->group = group;
}

static void _init_schqueue(struct schqueue *p)
{
  memset(p, 0, sizeof(*p));
  p->scheduler = &cfs_scheduler;
}

static bool cfs_cmp(rb_node lnode, rb_node rnode)
{
  struct schinfo *l = container_of(lnode, struct schinfo, sch_node),
                 *r = container_of(rnode, struct schinfo, sch_node);
  return l->vruntime == r->vruntime ? l < r : l->vruntime < r->vruntime;
}

static void update_old(struct proc *p, enum procstate new_state)
{
  ASSERT(p->schinfo.state == RUNNING);
  ASSERT(p->container->schqueue.scheduler == &cfs_scheduler);

  u64 delta = get_timestamp() - p->schinfo.start;
  p->schinfo.vruntime += delta;
  p->schinfo.state = new_state;

  for (auto group = p->container; group != group->parent; group = group->parent)
  {
    group->schinfo.vruntime += delta;

    auto child = &group->schinfo.sch_node;
    auto parent = &group->parent->schqueue.sch_root;
    if (_rb_lookup(child, parent, cfs_cmp))
    {
      _rb_erase(child, parent);
      ASSERT(!_rb_insert(child, parent, cfs_cmp));
    }
  }

  switch (new_state)
  {
  case RUNNABLE:
    break;
  case DEEPSLEEPING:
  case SLEEPING:
  case ZOMBIE:
    return;
  default:
    PANIC();
  }

  rb_node child = &p->schinfo.sch_node;
  rb_root parent = &p->container->schqueue.sch_root;
  ASSERT(!_rb_insert(child, parent, cfs_cmp));
}

static struct proc *pick_next()
{
  struct proc *next = NULL;
  for (struct container *group = &root_container; !next;)
  {
    auto first = _rb_first(&group->schqueue.sch_root);
    ASSERT(group == &root_container || first);
    if (!first)
      return NULL;

    struct schinfo *entity = container_of(first, struct schinfo, sch_node);
    if (entity->group)
      group = container_of(entity, struct container, schinfo);
    else
      next = container_of(entity, struct proc, schinfo);
  };

  _rb_erase(&next->schinfo.sch_node, &next->container->schqueue.sch_root);

  for (struct container *group = next->container; group != group->parent; group = group->parent)
  {
    if (_rb_first(&group->schqueue.sch_root))
      break;
    _rb_erase(&group->schinfo.sch_node, &group->parent->schqueue.sch_root);
  }

  return next;
}

static void update_new(struct proc *p)
{
  p->schinfo.start = get_timestamp();
}

static inline void __activate_group(struct container *c, bool recursive)
{
  if (c == c->parent)
    return;
  struct schinfo *child = &c->schinfo;
  struct schqueue *parent = &c->parent->schqueue;

  recursive &= _rb_first(&parent->sch_root) == NULL;

  ASSERT(!_rb_insert(&child->sch_node, &parent->sch_root, cfs_cmp));

  if (recursive)
    __activate_group(c->parent, true);
}

static bool activacte_proc(struct proc *p, bool onalert)
{
  switch (p->schinfo.state)
  {
  case RUNNABLE:
  case RUNNING:
  case ZOMBIE:
    return false;
  case DEEPSLEEPING:
    if (onalert)
      return false;
    break;
  case SLEEPING:
  case UNUSED:
    break;
  default:
    PANIC();
  }

  p->schinfo.state = RUNNABLE;

  bool should_activate_group = _rb_first(&p->container->schqueue.sch_root) == NULL;

  ASSERT(!_rb_insert(&p->schinfo.sch_node, &p->container->schqueue.sch_root, cfs_cmp));

  if (should_activate_group)
    __activate_group(p->container, true);

  return true;
}

static inline void activacte_group(struct container *c)
{
  __activate_group(c, false);
}

struct scheduler_ cfs_scheduler = {
    .init_schinfo = _init_schinfo,
    .init_schqueue = _init_schqueue,
    .update_old = update_old,
    .pick_next = pick_next,
    .update_new = update_new,
    .activacte_proc = activacte_proc,
    .activacte_group = activacte_group,
};
