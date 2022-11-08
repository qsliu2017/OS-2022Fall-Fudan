#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock *sblock;
static const BlockDevice *device;

static SpinLock lock; // protects block cache.
static ListNode head; // the list of all allocated in-memory block.
static struct rb_root_ root;
static usize count;      // the number of all allocated in-memory block.
static LogHeader header; // in-memory copy of log header block.

static bool block_cmp(rb_node lnode, rb_node rnode)
{
    return container_of(lnode, Block, rb_node)->block_no < container_of(rnode, Block, rb_node)->block_no;
}

// hint: you may need some other variables. Just add them here.
struct LOG
{
} log;

// read the content from disk.
static INLINE void device_read(Block *block)
{
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block)
{
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header()
{
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header()
{
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block)
{
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks()
{
    return count;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no)
{
    setup_checker(bcache);
    acquire_spinlock(bcache, &lock);

    Block *block = NULL;

    Block d = {
        .block_no = block_no,
    };

    block = _rb_lookup(&d.rb_node, &root, block_cmp);
    if (block)
    {
        block = container_of(block, Block, rb_node);
        wait_sem(&block->lock);
    }

    if (!block)
    {
        if (count >= EVICTION_THRESHOLD)
        {
            _for_in_reverse_dlist(ptr, &head)
            {
        
                Block *b = container_of(ptr, Block, node);
                if (!b->acquired)
                {
                    block = b;
                    break;
                }
            }
        }

        if (block == NULL)
        {
            block = kalloc(sizeof(Block));
            init_block(block);
            count++;
        }
        else
        {
            _rb_erase(&block->rb_node, &root);
        }

        wait_sem(&block->lock);
        block->block_no = block_no;
        device_read(block);
        block->valid = true;
        _rb_insert(&block->rb_node, &root, block_cmp);
    }

    _detach_from_list(&block->node);
    _insert_into_list(&head, &block->node);

    block->acquired = true;

    release_spinlock(bcache, &lock);
    return block;
}

// see `cache.h`.
static void cache_release(Block *block)
{
    setup_checker(bcache);
    acquire_spinlock(bcache, &lock);

    block->acquired = false;
    post_sem(&block->lock);
    if (count > EVICTION_THRESHOLD)
    {
        _detach_from_list(&block->node);
        _rb_erase(&block->rb_node, &root);
        kfree(block);
        count--;
    }

    release_spinlock(bcache, &lock);
}

// initialize block cache.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device)
{
    sblock = _sblock;
    device = _device;

    init_spinlock(&lock);
    init_list_node(&head);
    count = 0;
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block)
{
    if (ctx == NULL)
    {
        device_write(block);
        return;
    }
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext *ctx, usize block_no)
{
    // TODO
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};
