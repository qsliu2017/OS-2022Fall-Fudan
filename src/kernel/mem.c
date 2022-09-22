#include <common/list.h>
#include <common/rc.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>

RefCount alloc_page_cnt;

extern char end[]; // end address of kernel space

static void kinit_page();

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    kinit_page();
}

typedef ListNode Page;

static Page dummy_page;

void kinit_page()
{
    init_list_node(&dummy_page);
    for (u64 p = round_up((u64)end, PAGE_SIZE); p < P2K(PHYSTOP); p += PAGE_SIZE)
    {
        _insert_into_list(&dummy_page, (Page *)p);
    }
}

void *kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    auto alloc = dummy_page.prev;
    _detach_from_list(alloc);
    return alloc;
}

void kfree_page(void *p)
{
    _decrement_rc(&alloc_page_cnt);
    _merge_list(&dummy_page, (Page *)p);
}

// TODO: kalloc kfree
void *kalloc(isize _size)
{
    _size += 1;
    return NULL;
}
void kfree(void *_p)
{
    _p += 1;
}
