#include <common/string.h>
#include <kernel/sched.h>

static bool cfs_cmp(rb_node lnode, rb_node rnode)
{
  struct schinfo *l = container_of(lnode, struct schinfo, sch_node),
                 *r = container_of(rnode, struct schinfo, sch_node);
  return l->vruntime == r->vruntime ? l < r : l->vruntime < r->vruntime;
}

static void update_old(struct schinfo *p)
{
  u64 delta = get_timestamp() - p->start;
  p->vruntime += delta;

  struct container *container;
  if (p->group)
  {
    struct container *child = container_of(p, struct container, schinfo),
                     *parent = child->parent;
    if (parent == child)
      return;
    if (_rb_lookup(&child->schinfo.sch_node, &parent->schqueue.sch_root, cfs_cmp))
    {
      _rb_erase(&child->schinfo.sch_node, &parent->schqueue.sch_root);
      ASSERT(!_rb_insert(&child->schinfo.sch_node, &parent->schqueue.sch_root, cfs_cmp));
    }
    container = parent;
  }
  else
  {
    struct proc *child = container_of(p, struct proc, schinfo);
    container = child->container;
  }

  container->schqueue.scheduler->update_old(&container->schinfo);
}

static struct proc *pick_next(struct schqueue *q)
{
  auto first = _rb_first(&q->sch_root);
  if (!first)
    return NULL;

  struct schinfo *p = container_of(first, struct schinfo, sch_node);

  struct proc *next;
  if (p->group)
  {
    struct container *child = container_of(p, struct container, schinfo);
    next = child->schqueue.scheduler->pick_next(&child->schqueue);
    if (!_rb_first(&child->schqueue.sch_root))
      _rb_erase(first, &q->sch_root);
  }
  else
  {
    next = container_of(p, struct proc, schinfo);
    _rb_erase(first, &q->sch_root);
  }
  return next;
}

static void update_new(struct schinfo *p)
{
  p->start = get_timestamp();

  struct container *container;
  if (p->group)
  {
    struct container *child = container_of(p, struct container, schinfo),
                     *parent = child->parent;
    if (parent == child)
      return;
    container = parent;
  }
  else
  {
    struct proc *child = container_of(p, struct proc, schinfo);
    container = child->container;
  }

  container->schqueue.scheduler->update_new(&container->schinfo);
}

// add the schinfo node of the group to the schqueue of its parent
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

// add the schinfo node of the proc to the schqueue of its parent
static void activacte(struct schinfo *p)
{
  struct container *parent;
  if (p->group)
  {
    struct container *child = container_of(p, struct container, schinfo);
    parent = child->parent;
    if (child == parent)
      return;
  }
  else
  {
    struct proc *child = container_of(p, struct proc, schinfo);
    parent = child->container;
  }
  bool recursive = !_rb_first(&parent->schqueue.sch_root);
  ASSERT(!_rb_insert(&p->sch_node, &parent->schqueue.sch_root, cfs_cmp));
  if (recursive)
    parent->parent->schqueue.scheduler->activacte(&parent->schinfo);
}

struct scheduler_ cfs_scheduler = {
    .update_old = update_old,
    .pick_next = pick_next,
    .update_new = update_new,
    .activacte = activacte,
};
