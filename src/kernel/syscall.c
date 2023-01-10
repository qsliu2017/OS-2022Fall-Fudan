#include <common/sem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>

void *syscall_table[NR_SYSCALL];
#include <kernel/pt.h>
#include <kernel/paging.h>

typedef u64 syscall_func_t(u64, u64, u64, u64, u64, u64);

void syscall_entry(UserContext *context) {
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in
    // x0.
    u64 id = context->x8;
    if (id < NR_SYSCALL) {
        syscall_func_t *sc = syscall_table[id];
        context->x0 = sc(context->x0, context->x1, context->x2, context->x3,
                         context->x4, context->x5);
    }
}

// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    // TODO
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    // TODO
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
