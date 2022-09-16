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

typedef void *Page;
static ALWAYS_INLINE Page *get_prev_page_slot(Page p)
{
    return ((Page *)p) + 0;
}
static ALWAYS_INLINE Page *get_next_page_slot(Page p)
{
    return ((Page *)p) + 1;
}
static ALWAYS_INLINE Page get_prev_page(Page p)
{
    return *get_prev_page_slot(p);
}
static ALWAYS_INLINE void set_prev_page(Page p, Page prev)
{
    *get_prev_page_slot(p) = prev;
}
static ALWAYS_INLINE Page get_next_page(Page p)
{
    return *get_next_page_slot(p);
}
static ALWAYS_INLINE void set_next_page(Page p, Page next)
{
    *get_next_page_slot(p) = next;
}

static Page head_page;

void kinit_page()
{
    head_page = (Page)round_up(P2K(end), PAGE_SIZE);

    Page prev = head_page;
    for (Page current = head_page + PAGE_SIZE; (u64)current < PHYSTOP; current += PAGE_SIZE, prev += PAGE_SIZE)
    {
        set_next_page(prev, current);
        set_prev_page(current, prev);
    }

    // now prev is the tail page
    set_next_page(prev, head_page);
    set_prev_page(head_page, prev);
}

void *kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    // TODO
    Page alloc = get_next_page(head_page);
    if (alloc == head_page)
    {
        // PANIC();
        return NULL;
    }
    else
    {
        Page next = get_next_page(alloc);
        set_next_page(head_page, next);
        set_prev_page(next, head_page);
        return alloc;
    }
}

void kfree_page(void *p)
{
    _decrement_rc(&alloc_page_cnt);
    // TODO
    Page next = get_next_page(head_page);
    set_next_page(head_page, p);
    set_prev_page(p, head_page);
    set_next_page(p, next);
    set_prev_page(next, p);
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
