#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/sem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>

void *syscall_table[NR_SYSCALL];
#include <kernel/paging.h>
#include <kernel/pt.h>

typedef u64 syscall_func_t(u64, u64, u64, u64, u64, u64);

void syscall_entry(UserContext *context) {
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in
    // x0.
    u64 id = context->x[8];
    if (id < NR_SYSCALL) {
        syscall_func_t *sc = syscall_table[id];
        context->x[0] = sc(context->x[0], context->x[1], context->x[2],
                           context->x[3], context->x[4], context->x[5]);
    }
}

// check if the virtual address [start,start+size) is READABLE by the current
// user process
bool user_readable(const void *start, usize size) {
    for (usize va = PAGE_BASE((u64)start); va < (u64)start + size;
         va += PAGE_SIZE) {
        auto pte = get_pte(&thisproc()->pgdir, va, false);
        if (!pte)
            return false;
    }
    return true;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by
// the current user process
bool user_writeable(const void *start, usize size) {
    for (usize va = PAGE_BASE((u64)start); va < (u64)start + size;
         va += PAGE_SIZE) {
        auto pte = get_pte(&thisproc()->pgdir, va, false);
        if (!pte || ((*pte) & PTE_RW) != PTE_RW)
            return false;
    }
    return true;
}

// get the length of a string including tailing '\0' in the memory space of
// current user process return 0 if the length exceeds maxlen or the string is
// not readable by the current user process
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
