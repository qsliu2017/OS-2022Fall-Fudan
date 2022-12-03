#include <aarch64/mmu.h>
#include <common/bitmap.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <driver/sd.h>
#include <fs/block_device.h>
#include <fs/buf.h>
#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

#define N_SWAP_BLOCK (SWAP_END - SWAP_START)
#define BLOCKS_PER_PAGE (PAGE_SIZE / BLOCK_SIZE)
#define N_SWAP_PAGE (N_SWAP_BLOCK / BLOCKS_PER_PAGE)
static Bitmap(swap_bitmap, N_SWAP_PAGE);

define_rest_init(paging) {}

extern struct pgdir *current_pgdir();

// size is the number of page
u64 sbrk(i64 size) {
    auto pd = current_pgdir();
    struct section *st = NULL;
    _for_in_list(node, &pd->section_head) {
        if (node == &pd->section_head)
            continue;
        st = container_of(node, struct section, stnode);
        if (st->flags & ST_HEAP)
            break;
    }
    ASSERT(st);
    setup_checker(sbrk);
    unalertable_acquire_sleeplock(sbrk, &st->sleeplock);
    const u64 ret = st->end;
    if (size > 0) {
        st->end += (u64)(size)*PAGE_SIZE;
    } else if (size < 0) {
        ASSERT((u64)(-size) * PAGE_SIZE <= st->end);
        st->end -= (u64)(-size) * PAGE_SIZE;
    }
    release_sleeplock(sbrk, &st->sleeplock);
    return ret;
}

void *alloc_page_for_user() {
    while (left_page_cnt() <= REVERSED_PAGES) { // this is a soft limit
                                                // TODO
    }
    return kalloc_page();
}

// caller must have the pd->lock
void swapout(struct pgdir *pd, struct section *st) {
    ASSERT(!(st->flags & ST_SWAP));
    st->flags |= ST_SWAP;

    for (u64 addr = st->begin; addr < st->end; addr += PAGE_SIZE) {
        for (usize i = 0; i < N_SWAP_PAGE; i++) {
            if (!bitmap_get(swap_bitmap, i)) {
                bitmap_set(swap_bitmap, i);
                for (int j = 0; j < BLOCKS_PER_PAGE; j++) {
                    block_device.write(SWAP_START + i * BLOCKS_PER_PAGE + j,
                                       (u8 *)addr + j * BLOCK_SIZE);
                }
                auto pte = get_pte(pd, addr, false);
                ASSERT(pte);
                kderef_page((void *)P2K(PAGE_BASE((u64)*pte)));
                *pte &= 0xFFF;
                *pte |= i << 12;
                *pte &= ~PTE_VALID;
                break;
            }
        }
    }
}
// Free 8 continuous disk blocks
void swapin(struct pgdir *pd, struct section *st) {
    ASSERT(st->flags & ST_SWAP);
    st->flags &= ~ST_SWAP;
    for (u64 va = st->begin; va < st->end; va += PAGE_SIZE) {
        auto pte = get_pte(pd, va, false);
        ASSERT(pte);
        u32 idx = P2N(*pte);
        auto p = kalloc_page();
        kref_page(p);
        for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
            block_device.read(SWAP_START + idx * BLOCKS_PER_PAGE + i,
                              (u8 *)p + i * BLOCK_SIZE);
        }
        bitmap_clear(swap_bitmap, idx);
        *pte = K2P(p) | (*pte & 0xFFF) | PTE_VALID;
    }
}

void init_sections(ListNode *section_head) {
    init_list_node(section_head);
    struct section *section = kalloc(sizeof(struct section));
    memset(section, 0, sizeof(*section));
    section->flags = ST_HEAP;
    init_sleeplock(&section->sleeplock);
    _insert_into_list(section_head, &section->stnode);
}

int pgfault(u64 iss) {
    UNUSE(iss);
    struct proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 addr = arch_get_far();

    struct section *st = NULL;
    _for_in_list(node, &pd->section_head) {
        if (node == &pd->section_head)
            continue;
        st = container_of(node, struct section, stnode);
        if (st->begin <= addr && addr < st->end)
            break;
    }
    ASSERT(st);

    auto pte = get_pte(pd, addr, false);
    if (!pte || !*pte) {
        auto p = kalloc_page();
        kref_page(p);
        *get_pte(pd, addr, true) = K2P(p) | PTE_USER_DATA;
    } else if (st->flags & ST_SWAP) {
        swapin(pd, st);
    } else if (*pte & PTE_RO) {
        void *new_p = kalloc_page();
        kref_page(new_p);
        void *old_p = (void *)P2K(PAGE_BASE(*pte));
        memcpy(new_p, old_p, PAGE_SIZE);
        *pte = K2P(new_p) | PTE_USER_DATA;
        kderef_page(old_p);
    }

    return 0;
}
