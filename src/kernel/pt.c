#include <aarch64/intrinsic.h>
#include <common/defines.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/pt.h>

static inline u64 va_part(int n, u64 va) {
    return (va >> (39 - n * 9)) & ((1 << 9) - 1);
}

// Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
// If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or
// return NULL if false. THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY
// PTE.
PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc) {
    return _get_pte(pgdir->pt, va, alloc);
}
PTEntriesPtr _get_pte(PTEntriesPtr pt, u64 va, bool alloc) {
    for (int level = 0; level < 3; level++) {
        int i = va_part(level, va);
        if (pt[i] == NULL) {
            if (!alloc)
                return NULL;
            auto p = kalloc_page();
            ASSERT(p != NULL);
            memset(p, 0, sizeof(PTEntries));
            pt[i] = K2P((u64)p | PTE_TABLE);
        }
        pt = (u64 *)P2K(PTE_ADDRESS(pt[i]));
    }

    pt = (u64 *)KSPACE((u64)&pt[va_part(3, va)]);

    // printk("get_pte(va=0x%llx, alloc=%d)=0x%p\n", va, alloc, pt);
    return pt;
}

void init_pgdir(struct pgdir *pgdir) {
    pgdir->pt = _init_pt();
    init_spinlock(&pgdir->lock);
    init_sections(&pgdir->section_head);
}

PTEntriesPtr _init_pt() {
    auto p = kalloc_page();
    memset(p, 0, sizeof(PTEntries));
    return p;
}

typedef void pte_func(u64 pte);

static void walk_pgt(u64 *pgt, int level, pte_func *f) {
    // printk("walk_pgt(pt=0x%p, level=%d, f=0x%p)\n", pgt, level, f);
    if (level < 3) {
        for (int i = 0; i < N_PTE_PER_TABLE; i++) {
            if (pgt[i] == NULL)
                continue;
            walk_pgt((u64 *)P2K(PTE_ADDRESS(pgt[i])), level + 1, f);
        }
    }
    f((u64)pgt);
}

static void free_pgt(u64 pte) { kfree_page((void *)P2K(PTE_ADDRESS(pte))); }

void free_pgdir(struct pgdir *pgdir) { _free_pgdir(pgdir->pt); }

void _free_pgdir(PTEntriesPtr pt) { walk_pgt(pt, 0, free_pgt); }

void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags) {
    _vmmap(pd->pt, va, ka, flags);
}

void _vmmap(PTEntriesPtr pt, u64 va, void *ka, u64 flags) {
    *_get_pte(pt, va, true) = K2P(ka) | flags;
    kref_page(ka);
    arch_tlbi_vmalle1is();
}

struct pgdir *_curpgdir = NULL;
void attach_pgdir(struct pgdir *pgdir) {
    _curpgdir = pgdir;
    _attach_pgdir(pgdir->pt);
}

void _attach_pgdir(PTEntriesPtr pt) {
    extern PTEntries invalid_pt;
    if (pt) {
        arch_set_ttbr0(K2P(pt));
    } else {
        arch_set_ttbr0(K2P(&invalid_pt));
    }
}

void dump_pt(PTEntriesPtr pt) {
    printk("Page table 0x%p:\n", pt);
    for (int i = 0; i < N_PTE_PER_TABLE; i++)
        if (pt[i] != NULL)
            printk("\tentry[%x]: 0x%llx\n", i, pt[i]);
}
/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len) {
    u64 n = 0;
    for (u64 a = round_down((u64)va, PAGE_SIZE); a < (u64)va + len;
         a += PAGE_SIZE) {
        void *page;
        if (!(page = get_pte(pd, a, false))) {
            page = kalloc_page();
            kref_page(page);
            *get_pte(pd, a, true) = K2P(page) | PTE_USER_DATA;
        }
        u64 _n = a + PAGE_SIZE - ((u64)va + n);
        memcpy(page + ((u64)va + n) % PAGE_SIZE, p + n, _n);
        n += _n;
    }
    return n;
}

int uvmalloc(struct pgdir *pd, u64 base, u64 oldsz, u64 newsz) {
    ASSERT(oldsz < newsz);
    for (u64 off = round_up(oldsz, PAGE_SIZE); off < newsz; off += PAGE_SIZE) {
        auto p = kalloc_page();
        kref_page(p);
        vmmap(pd, base + off, p, PTE_USER_DATA);
    }
    return newsz;
}