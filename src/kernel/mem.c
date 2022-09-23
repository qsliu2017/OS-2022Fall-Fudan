#include <common/list.h>
#include <common/rc.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>

RefCount alloc_page_cnt;

static void kinit_page();
static void kinit_block();

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    kinit_page();
    kinit_block();
}

typedef struct Page
{
    ListNode freepage;
} Page;

static Page dummy_page;
static SpinLock page_lock;

void kinit_page()
{
    init_list_node(&dummy_page.freepage);
    extern char end[];
    for (u64 p = round_up((u64)end, PAGE_SIZE); p < P2K(PHYSTOP); p += PAGE_SIZE)
        _insert_into_list(&dummy_page.freepage, &((Page *)p)->freepage);

    init_spinlock(&page_lock);
}

void *kalloc_page()
{
    setup_checker(kalloc_page);
    _increment_rc(&alloc_page_cnt);
    acquire_spinlock(kalloc_page, &page_lock);
    auto alloc = dummy_page.freepage.prev;
    if (!_detach_from_list(alloc))
        alloc = NULL;
    release_spinlock(kalloc_page, &page_lock);
    return alloc;
}

void kfree_page(void *p)
{
    setup_checker(kfree_page);
    Page *page = (Page *)p;
    init_list_node(&page->freepage);
    _decrement_rc(&alloc_page_cnt);
    acquire_spinlock(kfree_page, &page_lock);
    _merge_list(&dummy_page.freepage, &page->freepage);
    release_spinlock(kfree_page, &page_lock);
}

typedef struct Block
{
    usize order; // this field saved for both free and alloc block
    ListNode list;
} Block;

static INLINE Block *block_container_of(ListNode *p)
{
    return container_of(p, Block, list);
}

static Block *detach_block(usize order);
static void merge_block(Block *b);

const usize BLOCK_POWER_BASE = 5, BLOCK_SIZE_LOG2_MAX = 12;

#define NCPU 4

static Block dummy_block[NCPU][8];

void kinit_block()
{
    for (auto cpu = 0; cpu < NCPU; cpu++)
        for (auto order = 0; order < 8; order++)
        {
            dummy_block[cpu][order].order = order;
            init_list_node(&dummy_block[cpu][order].list);
        }
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
    auto order = MAX(log2ceil(size + sizeof(Block) - sizeof(ListNode)), BLOCK_POWER_BASE) - BLOCK_POWER_BASE;
    Block *alloc = detach_block(order);
    return (void *)&alloc->list;
}

Block *detach_block(usize order)
{
    if (_empty_list(&dummy_block[cpuid()][order].list))
    {
        if (order < BLOCK_SIZE_LOG2_MAX - BLOCK_POWER_BASE)
        {
            Block *first_block = detach_block(order + 1);
            Block *second_block = (Block *)((u64)first_block + (1 << (order + BLOCK_POWER_BASE)));
            first_block->order = second_block->order = order;
            init_list_node(&first_block->list);
            init_list_node(&second_block->list);
            _merge_list(&dummy_block[cpuid()][order].list, &second_block->list);
            return first_block;
        }
        else
        {
            Block *block = (Block *)kalloc_page();
            block->order = order;
            init_list_node(&block->list);
            return block;
        }
    }
    else
    {
        auto alloc = dummy_block[cpuid()][order].list.next;
        _detach_from_list(alloc);
        return block_container_of(alloc);
    }
}

void kfree(void *p)
{
    Block *block = block_container_of(p);
    init_list_node(&block->list);
    merge_block(block);
}

void merge_block(Block *b)
{
    auto order = b->order;
    if (order < BLOCK_SIZE_LOG2_MAX - BLOCK_POWER_BASE)
    {
        _for_in_list(list_ptr, &dummy_block[cpuid()][order].list)
        {
            Block *a = block_container_of(list_ptr);
            u64 a_addr = (u64)a, b_addr = (u64)b;
            if ((a_addr >> (order + BLOCK_POWER_BASE + 1)) == ((b_addr >> (order + BLOCK_POWER_BASE + 1))))
            {
                _detach_from_list(list_ptr);
                Block *block = (a_addr >> (order + BLOCK_POWER_BASE)) & 1 ? b : a;
                block->order++;
                init_list_node(&block->list);
                return merge_block(block);
            }
        }
    }
    else
    {
        int cnt = 0;
        _for_in_list(list_ptr, &dummy_block[cpuid()][order].list)
        {
            cnt++;
        }
        if (cnt > 0)
        {
            return kfree_page(b);
        }
    }

    _merge_list(&dummy_block[cpuid()][order].list, &b->list);
}
