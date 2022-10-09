#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/init.h>
#include <kernel/slab.h>

static KCache cache_cache;

static void init_sizes_cache();

define_early_init(cache)
{
  cache_cache.objsize = round_up(sizeof(KCache), 8);
  cache_cache.num = (PAGE_SIZE - sizeof(Slab)) / cache_cache.objsize;
  init_spinlock(&cache_cache.spinlock);

  init_list_node(&cache_cache.slabs_free);
  init_list_node(&cache_cache.slabs_full);
  init_list_node(&cache_cache.slabs_partial);

  init_sizes_cache();
}

KCache *kcache_create(usize size)
{
  KCache *cachep = (KCache *)kcache_alloc(&cache_cache);
  cachep->objsize = round_up(size, 8); /* align to word size */
  cachep->num = (PAGE_SIZE - sizeof(Slab)) / cachep->objsize;
  init_spinlock(&cachep->spinlock);

  init_list_node(&cachep->slabs_free);
  init_list_node(&cachep->slabs_full);
  init_list_node(&cachep->slabs_partial);

  return cachep;
}

static inline u32 *next_freeobj(KCache *cachep, Slab *slabp, int index)
{
  return (u32 *)(index * cachep->objsize + round_up((u64)slabp->mem, 8));
}

const u32 BUFCTL_END = 0xffffffff;

void *kcache_alloc(KCache *cachep)
{
  setup_checker(kcache_alloc);
  acquire_spinlock(kcache_alloc, &cachep->spinlock);
  if (_empty_list(&cachep->slabs_partial))
  {
    if (_empty_list(&cachep->slabs_free))
    {
      void *pagep = kalloc_page();
      Slab *slabp = (Slab *)pagep;
      slabp->cache = cachep;
      _insert_into_list(&cachep->slabs_free, &slabp->list);
      slabp->inuse = 0;
      slabp->free = 0;
      for (u32 i = 0; i < cachep->num; i++)
      {
        *next_freeobj(cachep, slabp, i) = i + 1;
      }
      *next_freeobj(cachep, slabp, cachep->num - 1) = BUFCTL_END;
    }
    Slab *slabp = container_of(cachep->slabs_free.next, Slab, list);
    _detach_from_list(&slabp->list);
    _merge_list(&cachep->slabs_partial, &slabp->list);
  }
  Slab *slabp = container_of(cachep->slabs_partial.next, Slab, list);

  void *alloc = (void *)next_freeobj(cachep, slabp, slabp->free);
  slabp->free = *(u32 *)alloc;

  if (++slabp->inuse >= cachep->num)
  {
    _detach_from_list(&slabp->list);
    _merge_list(&cachep->slabs_full, &slabp->list);
  }
  release_spinlock(kcache_alloc, &cachep->spinlock);

  return alloc;
}

void kcache_free(KCache *cachep, void *objp)
{
  setup_checker(kcache_free);
  acquire_spinlock(kcache_free, &cachep->spinlock);
  u64 page_base = PAGE_BASE((u64)objp);

  ListNode *slabs_lists[] = {&cachep->slabs_full, &cachep->slabs_partial};
  for (usize i = 0; i < sizeof(slabs_lists) / sizeof(slabs_lists[0]); i++)
  {
    ListNode *slabs_list = slabs_lists[i];
    _for_in_list(ptr, slabs_list)
    {
      Slab *slabp = container_of(ptr, Slab, list);
      if (page_base == PAGE_BASE((u64)slabp->mem))
      {
        int index = ((u64)objp - (u64)slabp->mem) / cachep->objsize;
        *next_freeobj(cachep, slabp, index) = slabp->free;
        slabp->free = index;
        _detach_from_list(&slabp->list);
        if (--slabp->inuse > 0)
        {
          _merge_list(&cachep->slabs_partial, &slabp->list);
        }
        else
        {
          _merge_list(&cachep->slabs_free, &slabp->list);
        }
        goto done;
      }
    }
  }

done:
  release_spinlock(kcache_free, &cachep->spinlock);
}

int kcache_destroy(KCache *cachep)
{
  _acquire_spinlock(&cachep->spinlock);
  ListNode *slabs_lists[] = {&cachep->slabs_full, &cachep->slabs_partial, &cachep->slabs_free};

  for (usize i = 0; i < sizeof(slabs_lists) / sizeof(slabs_lists[0]); i++)
  {
    ListNode *slabs_list = slabs_lists[i];
    _for_in_list(ptr, slabs_list)
    {
      Slab *slabp = container_of(ptr, Slab, list);
      _detach_from_list(&slabp->list);
      kfree_page((void *)slabp);
    }
  }

  kcache_free(&cache_cache, (void *)cachep);

  return 0;
}

typedef struct SizesCache
{
  usize size;
  KCache *cachep;
} SizesCache;

static SizesCache cache_sizes[] = {
    {32, NULL},
    {64, NULL},
    {128, NULL},
    {256, NULL},
    {512, NULL},
    {1024, NULL},
    {2048, NULL},
};

void init_sizes_cache()
{
  for (usize i = 0; i < sizeof(cache_sizes) / sizeof(cache_sizes[0]); i++)
  {
    cache_sizes[i].cachep = kcache_create(cache_sizes[i].size);
  }
}

void *_kmalloc(usize size)
{
  for (usize i = 0; i < sizeof(cache_sizes) / sizeof(cache_sizes[0]); i++)
  {
    if (cache_sizes[i].size >= size)
    {
      return kcache_alloc(cache_sizes[i].cachep);
    }
  }
  return NULL;
}

void _kfree(void *objp)
{
  Slab *slabp = (void *)PAGE_BASE((u64)objp);
  kcache_free(slabp->cache, objp);
}
