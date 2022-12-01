#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock *sblock;
static const BlockDevice *device;

static SpinLock lock;        // protects block cache.
static ListNode head;        // the list of all allocated in-memory block, sort by LRU.
static struct rb_root_ root; // the rbtree of all allocated in-memory block, sort by block_no.
static usize count;          // the number of all allocated in-memory block.
static LogHeader header;     // in-memory copy of log header block.

static usize rm;             // number of remain op
static usize uncommitted;    // number of uncommitted op
static Semaphore remain_ops; // remain ops till next sync

static bool block_cmp(rb_node lnode, rb_node rnode)
{
    return container_of(lnode, Block, rb_node)->block_no < container_of(rnode, Block, rb_node)->block_no;
}

// hint: you may need some other variables. Just add them here.
struct LOG
{
    struct rb_root_ staged; // the list of all staged block, order by block_no, distinct
} log;

typedef struct
{
    Block *block;
    struct rb_node_ node;
} BlockLog;

static bool block_log_cmp(rb_node lnode, rb_node rnode)
{
    return container_of(lnode, BlockLog, node)->block->block_no < container_of(rnode, BlockLog, node)->block->block_no;
}

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
    block->pinned = 0;

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

    auto node = _rb_lookup(&d.rb_node, &root, block_cmp);
    if (node)
    {
        block = container_of(node, Block, rb_node);
        while (block->acquired)
        {
            block->pinned++;
            _release_spinlock(&lock);
            _acquire_spinlock(&lock);
            block->pinned--;
        }
        unalertable_wait_sem(&block->lock);
    }

    if (!block)
    {
        if (count >= EVICTION_THRESHOLD)
        {
            _for_in_reverse_dlist(ptr, &head)
            {
                Block *b = container_of(ptr, Block, node);
                if (!b->acquired && !b->pinned)
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

        unalertable_wait_sem(&block->lock);
        block->block_no = block_no;
        device_read(block);
        block->valid = true;
        ASSERT(!_rb_insert(&block->rb_node, &root, block_cmp));
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
    if (count > EVICTION_THRESHOLD && !block->pinned)
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
    root.rb_node = NULL;
    count = 0;
    {
        read_header();
        for (usize i = 0; i < header.num_blocks; i++)
        {
            Block b;
            b.block_no = sblock->log_start + 1 + i;
            device_read(&b);
            b.block_no = header.block_no[i];
            device_write(&b);
        }
        header.num_blocks = 0;
        write_header();
    }

    rm = MIN(sblock->num_log_blocks, LOG_MAX_SIZE);
    uncommitted = 0;
    init_sem(&remain_ops, rm / OP_MAX_NUM_BLOCKS);
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx)
{

    unalertable_wait_sem(&remain_ops);

    setup_checker(bcache);
    acquire_spinlock(bcache, &lock);

    ctx->rm = OP_MAX_NUM_BLOCKS;
    rm -= OP_MAX_NUM_BLOCKS;
    ctx->synced.rb_node = NULL;
    uncommitted++;

    release_spinlock(bcache, &lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block)
{
    if (ctx == NULL)
    {
        device_write(block);
        return;
    }

    setup_checker(bcache);
    acquire_spinlock(bcache, &lock);

    block->pinned++;
    BlockLog *blog = kalloc(sizeof(BlockLog));
    blog->block = block;

    if (!_rb_lookup(&blog->node, &ctx->synced, block_log_cmp))
    {
        ASSERT(_rb_insert(&blog->node, &ctx->synced, block_log_cmp) == 0);
        ASSERT(ctx->rm-- > 0);
    }
    else
    {
        kfree(blog);
    }

    release_spinlock(bcache, &lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx)
{
    setup_checker(bcache);
    acquire_spinlock(bcache, &lock);

    ctx->rm = OP_MAX_NUM_BLOCKS;
    for (rb_node first; (first = _rb_first(&ctx->synced));)
    {
        _rb_erase(first, &ctx->synced);
        if (!_rb_lookup(first, &log.staged, block_log_cmp))
        {
            ASSERT(_rb_insert(first, &log.staged, block_log_cmp) == 0);
            ctx->rm--;
        }
    }

    if ((rm + ctx->rm) / OP_MAX_NUM_BLOCKS > rm / OP_MAX_NUM_BLOCKS)
        post_sem(&remain_ops);
    rm += ctx->rm;

    if (--uncommitted == 0)
    {
        Block *block[LOG_MAX_SIZE];
        ASSERT(header.num_blocks == 0);
        for (rb_node first; (first = _rb_first(&log.staged));)
        {
            _rb_erase(first, &log.staged);
            auto i = header.num_blocks++;
            BlockLog *blog = container_of(first, BlockLog, node);
            block[i] = blog->block;
            header.block_no[i] = block[i]->block_no;
            device->write(sblock->log_start + 1 + i, block[i]->data);
            kfree(blog);
        }
        write_header();
        for (usize i = 0; i < header.num_blocks; i++)
        {
            device_write(block[i]);
            block[i]->pinned--;
        }
        header.num_blocks = 0;
        write_header();

        for (auto i = rm / OP_MAX_NUM_BLOCKS; i < MIN(sblock->num_log_blocks, LOG_MAX_SIZE) / OP_MAX_NUM_BLOCKS; i++)
            post_sem(&remain_ops);
        rm = MIN(sblock->num_log_blocks, LOG_MAX_SIZE);
    }

    release_spinlock(bcache, &lock);
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext *ctx)
{
    for (usize i = sblock->num_blocks - sblock->num_data_blocks; i < sblock->num_blocks;)
    {
        Block *b = cache_acquire(sblock->bitmap_start + i / BIT_PER_BLOCK);
        usize j = (i / BIT_PER_BLOCK + 1) * BIT_PER_BLOCK;

        for (; i < j && i < sblock->num_blocks; i++)
        {
            if (!bitmap_get((BitmapCell *)b->data, i % BIT_PER_BLOCK))
            {
                bitmap_set((BitmapCell *)b->data, i % BIT_PER_BLOCK);
                cache_sync(ctx, b);
                cache_release(b);
                b = cache_acquire(i);
                memset(b->data, 0, BLOCK_SIZE);
                cache_sync(ctx, b);
                cache_release(b);
                return i;
            }
        }

        cache_release(b);
    }

    PANIC();
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext *ctx, usize block_no)
{
    Block *b = cache_acquire(sblock->bitmap_start + block_no / BIT_PER_BLOCK);
    ASSERT(bitmap_get((BitmapCell *)b->data, block_no % BIT_PER_BLOCK));
    bitmap_clear((BitmapCell *)b->data, block_no % BIT_PER_BLOCK);
    cache_sync(ctx, b);
    cache_release(b);
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
