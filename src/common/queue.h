#pragma once

#include <common/sem.h>
#include <common/spinlock.h>

typedef struct MyQueueNode
{
  struct MyQueueNode *_prev, *_next;
} MyQueueNode;

// Lock-free queue
typedef struct MyQueue
{
  MyQueueNode _dummy;
  SpinLock _lock;
} MyQueue;

void myqueue_init(MyQueue *);

// Push a node. Lock-free.
void myqueue_enqueue(MyQueue *, MyQueueNode *);

// Pop the first-come node, NULL if empty. Lock-free.
MyQueueNode *myqueue_dequeue(MyQueue *);

// True if no node in queue. Lock-free.
bool myqueue_empty(MyQueue *);

// Message is used for Channel
typedef struct Message
{
  struct Message *_prev, *_next;
} Message;

// Channel is a block queue.
typedef struct Channel
{
  Message _dummy;
  SpinLock _lock;
  Semaphore _sem;
} Channel;

void channel_init(Channel *);

// Non-block. Enqueue a message to channel.
void channel_push(Channel *, Message *);

// Dequeue a message from channel, block and wait if no message available. Never return NULL.
Message *channel_pop(Channel *);

// Return a message from channel but do NOT remove it, block and wait if no message available. Never return NULL.
Message *channel_peek(Channel *);
