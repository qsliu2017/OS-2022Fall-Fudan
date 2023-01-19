#pragma once

#include <aarch64/mmu.h>
#include <common/list.h>

struct pgdir {
    PTEntriesPtr pt;
    SpinLock lock;
    ListNode section_head;
    bool online;
};

void init_pgdir(struct pgdir *pgdir);
PTEntriesPtr _init_pt();
WARN_RESULT PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc);
WARN_RESULT PTEntriesPtr _get_pte(PTEntriesPtr pt, u64 va, bool alloc);
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags);
void _vmmap(PTEntriesPtr pt, u64 va, void *ka, u64 flags);
void free_pgdir(struct pgdir *pgdir);
void _free_pgdir(PTEntriesPtr pgdir);
void attach_pgdir(struct pgdir *pgdir);
void _attach_pgdir(PTEntriesPtr pgdir);
int copyout(struct pgdir *pd, void *va, void *p, usize len);
int uvmalloc(struct pgdir *pd, u64 base, u64 oldsz, u64 newsz);
void uvmcopy(struct pgdir *dst);