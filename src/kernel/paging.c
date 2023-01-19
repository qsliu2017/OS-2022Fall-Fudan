#include <aarch64/mmu.h>
#include <common/bitmap.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

#define _for_in_section(st, va)                                                \
    for (u64 va = st->begin; va < st->end; va += PAGE_SIZE)

// size is the number of page
u64 sbrk(i64 size) {
    extern struct pgdir *_curpgdir;
    struct section *st = NULL;
    _for_in_list(node, &_curpgdir->section_head) {
        if (node == &_curpgdir->section_head)
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
        auto pte = get_pte(pd, addr, false);
        ASSERT(pte);
        u64 new_pte = *pte & 0xFFF & ~PTE_VALID;
        if (st->fp) {

        } else {
            u32 bno = write_page_to_disk((void *)addr);
            ASSERT(bno);
            new_pte |= (u64)bno << 12;
        }
        kderef_page((void *)P2K(PAGE_BASE((u64)*pte)));
        *pte = new_pte;
    }
}
// Free 8 continuous disk blocks
void swapin(struct pgdir *pd, struct section *st) {
    ASSERT(st->flags & ST_SWAP);
    st->flags &= ~ST_SWAP;
    for (u64 va = st->begin; va < st->end; va += PAGE_SIZE) {
        auto pte = get_pte(pd, va, false);
        ASSERT(pte);
        auto p = kalloc_page();
        kref_page(p);
        if (st->fp) {
            fileread(st->fp, (char *)p, MIN(st->end - va, (u64)PAGE_SIZE));
        } else {
            u32 bno = P2N(*pte);
            read_page_from_disk(p, bno);
        }
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
    if (st->flags & ST_SWAP) {
        swapin(pd, st);
    }
    if (!pte || !*pte) {
        auto p = kalloc_page();
        kref_page(p);
        *get_pte(pd, addr, true) = K2P(p) | PTE_USER_DATA;
    }
    if (*pte & PTE_RO) {
        if (st->fp)
            return -1;
        void *new_p = kalloc_page();
        kref_page(new_p);
        void *old_p = (void *)P2K(PAGE_BASE(*pte));
        memcpy(new_p, old_p, PAGE_SIZE);
        *pte = K2P(new_p) | PTE_USER_DATA;
        kderef_page(old_p);
    }

    return 0;
}

struct section *mmap(struct pgdir *pd, u64 va, File *fp, u64 offset, u64 length,
                     u64 flags) {
    ASSERT(va % PAGE_SIZE == 0);
    struct section *st = kalloc(sizeof(struct section));
    st->flags = flags;
    init_sleeplock(&st->sleeplock);
    st->begin = va;
    st->end = va + length;
    insert_into_list(&pd->lock, &pd->section_head, &st->stnode);
    st->fp = fp;
    filedup(fp);
    st->offset = offset;
    st->length = length;

    return st;
}