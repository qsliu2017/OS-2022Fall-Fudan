#include <common/defines.h>
#include <common/list.h>
#include <common/rbtree.h>
#include <common/rc.h>
#include <common/sem.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <sys/stat.h>

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;
static struct rb_root_ root;

static const SuperBlock *sblock;
static const BlockCache *cache;

static bool inode_cmp(rb_node lnode, rb_node rnode) {
    return container_of(lnode, Inode, rb_node)->inode_no <
           container_of(rnode, Inode, rb_node)->inode_no;
}

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry *get_entry(Block *block, usize inode_no) {
    return ((InodeEntry *)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32 *get_addrs(Block *block) {
    return ((IndirectBlock *)block->data)->addrs;
}

// initialize in-memory inode.
static void init_inode(Inode *inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// initialize inode tree.
void init_inodes(const SuperBlock *_sblock, const BlockCache *_cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    for (usize inode_no = ROOT_INODE_NO; inode_no < sblock->num_inodes;
         inode_no++) {
        Block *block = cache->acquire(to_block_no(inode_no));
        InodeEntry *entry = get_entry(block, inode_no);
        Inode *inode = kalloc(sizeof(Inode));
        init_inode(inode);
        inode->inode_no = inode_no;
        if (entry->type == INODE_INVALID) {
            _insert_into_list(&head, &inode->node);
        } else {
            inode->valid = true;
            memcpy(&inode->entry, entry, sizeof(InodeEntry));
            auto _r = _rb_insert(&inode->rb_node, &root, inode_cmp);
            ASSERT(!_r);
        }
        cache->release(block);
    }

    if (ROOT_INODE_NO < sblock->num_inodes) {
        inodes.root = inodes.get(ROOT_INODE_NO);
        inodes.share(inodes.root);
    } else
        printk("(warn) init_inodes: no root inode.\n");
}

// see `inode.h`.
static usize inode_alloc(OpContext *ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);
    _acquire_spinlock(&lock);
    ASSERT(!_empty_list(&head));
    auto node = head.next;
    _detach_from_list(node);
    Inode *inode = container_of(node, Inode, node);
    auto __r = _rb_insert(&inode->rb_node, &root, inode_cmp);
    ASSERT(!__r);
    memset(&inode->entry, 0, sizeof(InodeEntry));
    inode->entry.type = type;
    inode->valid = true;
    inodes.sync(ctx, inode, true);
    _release_spinlock(&lock);
    return inode->inode_no;
}

// see `inode.h`.
static void inode_lock(Inode *inode) {
    ASSERT(inode->rc.count > 0);
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode *inode) {
    ASSERT(inode->rc.count > 0);
    post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext *ctx, Inode *inode, bool do_write) {
    if (inode->valid && do_write) {
        Block *block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry *entry = get_entry(block, inode->inode_no);
        memcpy(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, block);
        cache->release(block);
        return;
    }
    if (!inode->valid) {
        Block *block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry *entry = get_entry(block, inode->inode_no);
        memcpy(&inode->entry, entry, sizeof(InodeEntry));
        cache->release(block);
        return;
    }
}

// see `inode.h`.
static Inode *inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    Inode target = {.inode_no = inode_no};
    auto node = _rb_lookup(&target.rb_node, &root, inode_cmp);
    ASSERT(node);
    Inode *inode = container_of(node, Inode, rb_node);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}
// see `inode.h`.
static void inode_clear(OpContext *ctx, Inode *inode) {
    for (usize direct = 0; direct < INODE_NUM_DIRECT; direct++) {
        u32 *block_no = &inode->entry.addrs[direct];
        if (*block_no) {
            cache->free(ctx, *block_no);
            *block_no = 0;
        }
    }
    if (inode->entry.indirect) {
        Block *block = cache->acquire(inode->entry.indirect);
        IndirectBlock *indirect_block = (IndirectBlock *)block->data;
        for (usize indirect = 0; indirect < INODE_NUM_INDIRECT; indirect++) {
            u32 *block_no = &indirect_block->addrs[indirect];
            if (*block_no) {
                cache->free(ctx, *block_no);
            }
        }
        cache->release(block);
        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    inode->entry.num_bytes = 0;
    inodes.sync(ctx, inode, true);
}

// see `inode.h`.
static Inode *inode_share(Inode *inode) {
    _increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext *ctx, Inode *inode) {
    _decrement_rc(&inode->rc);
    if (inode->rc.count == 0 && inode->entry.num_links == 0) {
        inodes.clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode->valid = true;
        inodes.sync(ctx, inode, true);
        _acquire_spinlock(&lock);
        _rb_erase(&inode->rb_node, &root);
        _insert_into_list(&head, &inode->node);
        _release_spinlock(&lock);
    }
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext *ctx, Inode *inode, usize offset,
                       bool *modified) {
    usize block_idx = offset / BLOCK_SIZE;
    if (block_idx < INODE_NUM_DIRECT) {
        u32 *block_no = &inode->entry.addrs[block_idx];
        if (*block_no == 0) {
            *block_no = cache->alloc(ctx);
            inodes.sync(ctx, inode, true);
            *modified = true;
        }
        return *block_no;
    } else {
        u32 *indirect = &inode->entry.indirect;
        if (*indirect == 0) {
            *indirect = cache->alloc(ctx);
            *modified = true;
            inodes.sync(ctx, inode, true);
        }
        Block *block = cache->acquire(*indirect);
        IndirectBlock *indirect_block = (IndirectBlock *)block->data;
        u32 *addr = &indirect_block->addrs[block_idx - INODE_NUM_DIRECT];
        if (*addr == 0) {
            *addr = cache->alloc(ctx);
            *modified = true;
            cache->sync(ctx, block);
        }
        cache->release(block);
        return *addr;
    }
}

// see `inode.h`.
static usize inode_read(Inode *inode, u8 *dest, usize offset, usize count) {
    InodeEntry *entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_read(inode, (char *)dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    usize read = 0;
    while (count > 0) {
        u32 block_no = inode_map(NULL, inode, offset, NULL);
        Block *block = cache->acquire(block_no);
        usize to_read = MIN(count, BLOCK_SIZE - offset % BLOCK_SIZE);
        memcpy(dest + read, block->data + offset % BLOCK_SIZE, to_read);
        cache->release(block);
        read += to_read;
        offset += to_read;
        count -= to_read;
    }
    return read;
}

// see `inode.h`.
static usize inode_write(OpContext *ctx, Inode *inode, u8 *src, usize offset,
                         usize count) {
    InodeEntry *entry = &inode->entry;
    usize end = offset + count;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_write(inode, (char *)src, count);
    }
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    usize write = 0;
    while (count > 0) {
        bool modified;
        u32 block_no = inode_map(ctx, inode, offset, &modified);
        Block *block = cache->acquire(block_no);
        usize to_write = MIN(count, BLOCK_SIZE - offset % BLOCK_SIZE);
        memcpy(block->data + offset % BLOCK_SIZE, src + write, to_write);
        cache->sync(ctx, block);
        cache->release(block);
        write += to_write;
        offset += to_write;
        count -= to_write;
    }
    if (end > entry->num_bytes) {
        entry->num_bytes = end;
        inodes.sync(ctx, inode, true);
    }
    return write;
}

// see `inode.h`.
static usize inode_lookup(Inode *inode, const char *name, usize *index) {
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry direntry;
    for (usize offset = 0; offset < entry->num_bytes;
         offset += sizeof(DirEntry)) {
        inodes.read(inode, (u8 *)&direntry, offset, sizeof(DirEntry));
        if (!strncmp(direntry.name, name, FILE_NAME_MAX_LENGTH) &&
            direntry.inode_no) {
            if (index)
                *index = offset / sizeof(DirEntry);
            return direntry.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext *ctx, Inode *inode, const char *name,
                          usize inode_no) {
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry direntry;
    for (usize offset = 0; offset < entry->num_bytes;
         offset += sizeof(DirEntry)) {
        inodes.read(inode, (u8 *)&direntry, offset, sizeof(DirEntry));
        if (!strncmp(direntry.name, name, FILE_NAME_MAX_LENGTH))
            return -1;
    }
    direntry.inode_no = inode_no;
    strncpy(direntry.name, name, FILE_NAME_MAX_LENGTH);
    inodes.write(ctx, inode, (u8 *)&direntry, entry->num_bytes,
                 sizeof(DirEntry));
    return entry->num_bytes / sizeof(DirEntry);
}

// see `inode.h`.
static void inode_remove(OpContext *ctx, Inode *inode, usize index) {
    DirEntry direntry = {.inode_no = 0, .name = {'\0'}};
    inodes.write(ctx, inode, (u8 *)&direntry, index * sizeof(DirEntry),
                 sizeof(DirEntry));
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
/* Paths. */

/* Copy the next path element from path into name.
 *
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static const char *skipelem(const char *path, char *name) {
    const char *s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/* Look up and return the inode for a path name.
 *
 * If parent != 0, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Must be called inside a transaction since it calls iput().
 */
static Inode *namex(const char *path, int nameiparent, char *name,
                    OpContext *ctx) {
    Inode *ip, *next;
    if (*path == '/')
        ip = inodes.share(inodes.root);
    else
        ip = inodes.share(thisproc()->cwd);

    while ((path = skipelem(path, name)) != 0) {
        inodes.lock(ip);
        if (ip->entry.type != INODE_DIRECTORY) {
            inodes.unlock(ip);
            inodes.put(ctx, ip);
            return 0;
        }
        if (nameiparent && *path == '\0') {
            inodes.unlock(ip);
            return ip;
        }
        usize ino;
        if (!(ino = inodes.lookup(ip, name, 0))) {
            inodes.unlock(ip);
            inodes.put(ctx, ip);
            return 0;
        }
        next = inodes.get(ino);
        inodes.unlock(ip);
        inodes.put(ctx, ip);
        ip = next;
    }
    if (nameiparent) {
        inodes.put(ctx, ip);
        return 0;
    }
    return ip;
}

Inode *namei(const char *path, OpContext *ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, 0, name, ctx);
}

Inode *nameiparent(const char *path, char *name, OpContext *ctx) {
    return namex(path, 1, name, ctx);
}

/*
 * Copy stat information from inode.
 * Caller must hold ip->lock.
 */
void stati(Inode *ip, struct stat *st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
    case INODE_REGULAR:
        st->st_mode = S_IFREG;
        break;
    case INODE_DIRECTORY:
        st->st_mode = S_IFDIR;
        break;
    case INODE_DEVICE:
        st->st_mode = 0;
        break;
    default:
        PANIC();
    }
}
