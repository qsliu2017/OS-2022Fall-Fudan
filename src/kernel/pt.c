#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

static inline u64 va_part(int n, u64 va)
{
    return (va >> (39 - n * 9)) & ((1 << 9) - 1);
}

// Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
// If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
// THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    u64 *pgt = pgdir->pt;
    for (int level = 0; level < 3; level++)
    {
        int i = va_part(level, va);
        if (pgt[i] == NULL)
        {
            if (!alloc)
                return NULL;
            auto p = kalloc_page();
            ASSERT(p != NULL);
            memset(p, 0, sizeof(PTEntries));
            pgt[i] = K2P((u64)p | PTE_TABLE);
        }
        pgt = (u64 *)P2K(PTE_ADDRESS(pgt[i]));
    }

    pgt = (u64 *)KSPACE((u64)&pgt[va_part(3, va)]);

    // printk("get_pte(va=0x%llx, alloc=%d)=0x%p\n", va, alloc, pgt);
    return pgt;
}

void init_pgdir(struct pgdir *pgdir)
{
    auto p = kalloc_page();
    memset(p, 0, sizeof(PTEntries));
    pgdir->pt = p;
}

typedef void pte_func(u64 pte);

static void walk_pgt(u64 *pgt, int level, pte_func *f)
{
    printk("walk_pgt(pt=0x%p, level=%d, f=0x%p)\n", pgt, level, f);
    if (level < 3)
    {
        for (int i = 0; i < N_PTE_PER_TABLE; i++)
        {
            if (pgt[i] == NULL)
                continue;
            walk_pgt((u64 *)P2K(PTE_ADDRESS(pgt[i])), level + 1, f);
        }
    }
    f((u64)pgt);
}

static void free_pgt(u64 pte)
{
    kfree_page((void *)P2K(PTE_ADDRESS(pte)));
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    walk_pgt(pgdir->pt, 0, free_pgt);
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void dump_pt(PTEntriesPtr pt)
{
    printk("Page table 0x%p:\n", pt);
    for (int i = 0; i < N_PTE_PER_TABLE; i++)
        if (pt[i] != NULL)
            printk("\tentry[%x]: 0x%llx\n", i, pt[i]);
}
