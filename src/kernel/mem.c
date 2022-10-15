#include <common/list.h>
#include <common/rc.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/slab.h>

RefCount alloc_page_cnt;

static void kinit_page();

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    kinit_page();
}

typedef struct Page
{
    union
    {
        QueueNode freequeue; /* free page */
        char mem[0];         /* allocated page */
    };
} Page;

static QueueNode *freepages;

void kinit_page()
{
    freepages = NULL;
    extern char end[];
    for (u64 p = round_up((u64)end, PAGE_SIZE); p < P2K(PHYSTOP); p += PAGE_SIZE)
    {
        kfree_page((void *)p);
    }
}

void *kalloc_page()
{
    auto freepage = container_of(fetch_from_queue(&freepages), Page, freequeue);
    ASSERT(freepage != NULL);
    return freepage->mem;
}

void kfree_page(void *p)
{
    Page *page = (Page *)p;
    add_to_queue(&freepages, &page->freequeue);
}

void *kalloc(isize size)
{
    return _kmalloc((usize)size);
}

void kfree(void *p)
{
    _kfree(p);
}
