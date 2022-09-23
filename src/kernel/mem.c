#include <common/list.h>
#include <common/rc.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>

RefCount alloc_page_cnt;

extern char end[]; // end address of kernel space

static void kinit_page();
static void kinit_block();

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    kinit_page();
    kinit_block();
}

typedef ListNode Page;

static Page dummy_page;
static SpinLock page_lock;

void kinit_page()
{
    init_spinlock(&page_lock);
    init_list_node(&dummy_page);
    for (u64 p = round_up((u64)end, PAGE_SIZE); p < P2K(PHYSTOP); p += PAGE_SIZE)
    {
        _insert_into_list(&dummy_page, (Page *)p);
    }
}

void *kalloc_page()
{
    setup_checker(kalloc_page);
    _increment_rc(&alloc_page_cnt);
    acquire_spinlock(kalloc_page, &page_lock);
    auto alloc = dummy_page.prev;
    if (!_detach_from_list(alloc))
        alloc = NULL;
    release_spinlock(kalloc_page, &page_lock);
    return alloc;
}

void kfree_page(void *p)
{
    setup_checker(kfree_page);
    Page *page = (Page *)p;
    init_list_node(page);
    _decrement_rc(&alloc_page_cnt);
    acquire_spinlock(kfree_page, &page_lock);
    _merge_list(&dummy_page, page);
    release_spinlock(kfree_page, &page_lock);
}

typedef struct Block
{
    isize size; // this field saved for both free and alloc block
    ListNode list;
} Block;

static INLINE Block *block_container_of(ListNode *p)
{
    return container_of(p, Block, list);
}

static Block dummy_block;
static SpinLock block_lock;

void kinit_block()
{
    init_spinlock(&block_lock);
    dummy_block.size = 0;
    init_list_node(&dummy_block.list);
}

usize log2ceil(u64 n)
{
    usize i = 0;
    n--;
    while (n >> i)
        i++;
    return i + 1;
}

void *kalloc(isize size)
{
    int align;
    if ((size & 7) == 0)
        align = 8;
    else if ((size & 3) == 0)
        align = 4;
    else if ((size & 1) == 0)
        align = 2;
    else
        align = 1;

    size = round_up(MAX(size, (isize)sizeof(ListNode)) + sizeof(Block) - sizeof(ListNode), align);

    setup_checker(kalloc);
    acquire_spinlock(kalloc, &block_lock);

    Block *alloc = NULL;
    ListNode *prev = NULL;
    _for_in_list(list_ptr, &dummy_block.list)
    {
        Block *block = block_container_of(list_ptr);
        if (block->size >= size)
        {
            alloc = block;
            prev = _detach_from_list(&alloc->list);
            break;
        }
    }
    if (!alloc)
    {
        alloc = kalloc_page();
        alloc->size = PAGE_SIZE;
        init_list_node(&alloc->list);
        prev = dummy_block.list.prev;
    }

    if (alloc->size - size >= (isize)sizeof(Block))
    {
        Block *rest = (Block *)((void *)alloc + size);
        rest->size = alloc->size - size;
        alloc->size = size;
        init_list_node(&rest->list);
        _merge_list(prev, &rest->list);
    }

    release_spinlock(kalloc, &block_lock);
    return (void *)round_up((u64)&alloc->list, align);
}

void kfree(void *p)
{
    Block *block = block_container_of(p);
    init_list_node(&block->list);

    setup_checker(kfree);
    acquire_spinlock(kfree, &block_lock);

    ListNode *prev = &dummy_block.list;
    _for_in_list(list_ptr, &dummy_block.list)
    {
        if ((u64)list_ptr < (u64)p)
        {
            prev = list_ptr;
        }
        else
        {
            break;
        }
    }
    _merge_list(prev, &block->list);

    release_spinlock(kfree, &block_lock);
}
