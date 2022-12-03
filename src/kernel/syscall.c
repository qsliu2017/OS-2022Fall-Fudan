#include <common/sem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>

void *syscall_table[NR_SYSCALL];

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
