#include <common/list.h>
#include <common/rbtree.h>
#include <common/rc.h>
#include <common/string.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/slab.h>

RefCount alloc_page_cnt;

static void kinit_page();

void init_slab();

static void init_zero_page();

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
    kinit_page();
    init_slab();
    init_zero_page();
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

static struct page __page_cnt;

void kinit_page()
{
    init_rc(&__page_cnt.ref);
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
    _decrement_rc(&__page_cnt.ref);
    return freepage->mem;
}

void kfree_page(void *p)
{
    Page *page = (Page *)p;
    add_to_queue(&freepages, &page->freequeue);
    _increment_rc(&__page_cnt.ref);
}

struct __page_ref
{
    RefCount ref;
    void *p;
    struct rb_node_ node;
};

static bool page_cmp(rb_node lnode, rb_node rnode)
{
    return container_of(lnode, struct __page_ref, node)->p < container_of(rnode, struct __page_ref, node)->p;
}

static struct rb_root_ ref_root;

static inline struct __page_ref *__page_ref_lookup(void *p)
{
    struct __page_ref r = {.p = p};
    auto node = _rb_lookup(&r.node, &ref_root, page_cmp);
    if (!node)
        return NULL;
    return container_of(node, struct __page_ref, node);
}

void kref_page(void *p)
{
    auto rp = __page_ref_lookup(p);
    if (!rp)
    {
        rp = kalloc(sizeof(struct __page_ref));
        memset(rp, 0, sizeof(*rp));
        rp->p = p;
        ASSERT(!_rb_insert(&rp->node, &ref_root, page_cmp));
    }
    _increment_rc(&rp->ref);
}

void kderef_page(void *p)
{
    auto rp = __page_ref_lookup(p);
    ASSERT(rp);
    if (_decrement_rc(&rp->ref))
    {
        kfree_page(rp->p);
        _rb_erase(&rp->node, &ref_root);
        kfree(rp);
    }
}

void *kalloc(isize size)
{
    return _kmalloc((usize)size);
}

void kfree(void *p)
{
    _kfree(p);
}

u64 left_page_cnt()
{
    return (u64)__page_cnt.ref.count;
}

static void *__zero_page;

static void init_zero_page()
{
    __zero_page = kalloc_page();
    memset(__zero_page, 0, PAGE_SIZE);
    kref_page(__zero_page);
}

void *get_zero_page()
{
    return __zero_page;
}

bool check_zero_page()
{
    for (u8 *p = __zero_page; p < (u8 *)__zero_page + PAGE_SIZE; p++)
    {
        if (*p)
            return false;
    }
    return true;
}

u32 write_page_to_disk(void *ka)
{
    UNUSE(ka);
    return 0;
}

void read_page_from_disk(void *ka, u32 bno)
{
    UNUSE(ka);
    UNUSE(bno);
}
