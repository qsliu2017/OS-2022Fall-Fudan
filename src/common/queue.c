#include <common/queue.h>

void myqueue_init(MyQueue *q)
{
  init_spinlock(&q->_lock);
  auto dummy = &q->_dummy;
  dummy->_prev = dummy->_next = dummy;
}

// Push a node. Lock-free.
void myqueue_enqueue(MyQueue *q, MyQueueNode *node)
{
  raii_acquire_spinlock(&q->_lock, 0);
  auto dummy = &q->_dummy;
  auto last = dummy->_prev;
  dummy->_prev = last->_next = node;
  node->_prev = last;
  node->_next = dummy;
}

// Pop the first-come node, NULL if empty. Lock-free.
MyQueueNode *myqueue_dequeue(MyQueue *q)
{
  raii_acquire_spinlock(&q->_lock, 0);
  auto dummy = &q->_dummy;
  auto first = dummy->_next;
  if (dummy == first)
    return NULL;
  auto second = first->_next;
  dummy->_next = second;
  second->_prev = dummy;
  return first;
}

// True if no node in queue. Lock-free.
bool myqueue_empty(MyQueue *q)
{
  raii_acquire_spinlock(&q->_lock, 0);
  auto dummy = &q->_dummy;
  return dummy == dummy->_next;
}

void channel_init(Channel *c)
{
  auto dummy = &c->_dummy;
  dummy->_next = dummy->_prev = dummy;
  init_spinlock(&c->_lock);
  init_sem(&c->_sem, 0);
}

void channel_push(Channel *c, Message *m)
{
  raii_acquire_spinlock(&c->_lock, 0);
  auto dummy = &c->_dummy;
  auto last = dummy->_prev;
  dummy->_prev = last->_next = m;
  m->_prev = last;
  m->_next = dummy;
  post_sem(&c->_sem);
}

Message *channel_pop(Channel *c)
{
  wait_sem(&c->_sem);
  raii_acquire_spinlock(&c->_lock, 0);
  auto dummy = &c->_dummy;
  auto first = dummy->_next;
  ASSERT(dummy != first);
  auto second = first->_next;
  dummy->_next = second;
  second->_prev = dummy;
  return first;
}

Message *channel_peek(Channel *c)
{
  raii_acquire_spinlock(&c->_lock, 0);
  auto dummy = &c->_dummy;
  auto first = dummy->_next;
  ASSERT(dummy != first);
  return first;
}
