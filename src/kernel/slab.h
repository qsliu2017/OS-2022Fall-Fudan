#pragma once

#include <common/defines.h>
#include <common/list.h>

/* One cache exists for each type of object that is to be cached. */
typedef struct KCache
{
  u32 objsize; /* the size of each object packed into the slab */
  u32 num;     /* the number of objects contained in each slab */
  SpinLock spinlock;

  ListNode slabs_full;
  ListNode slabs_partial;
  ListNode slabs_free;
} KCache;

KCache *kcache_create(usize size);
void *kcache_alloc(KCache *cachep);
void kcache_free(KCache *cachep, void *objp);
void *_kmalloc(usize size);
void _kfree(void *objp);
int kcache_destroy(KCache *cachep);

typedef struct Slab
{
  KCache *cache;
  ListNode list; /* used by KCache */
  u32 free;      /* the index of first free obj */
  u32 inuse;
  char mem[]; /* point to the first obj */
} Slab;
