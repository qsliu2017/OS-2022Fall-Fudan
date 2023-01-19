#include <aarch64/intrinsic.h>
#include <aarch64/mmu.h>
#include <common/defines.h>
#include <driver/sd.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>
#include <test/test.h>

bool panic_flag;

void trap_return();

NO_RETURN void idle_entry() {
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap { arch_wfi(); }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry() {
    printk("hello world %d\n", (int)sizeof(struct proc));

    // proc_test();
    // user_proc_test();
    // container_test();
    // sd_test();

    do_rest_init();

    extern char icode[], eicode[];
    UNUSE(eicode);
    auto initp = create_proc();
    void *entry = (void *)0x400000;
    copyout(&initp->pgdir, entry, icode, eicode - icode);

    initp->ucontext->elr = (u64)entry;
    initp->ucontext->spsr = 0;
    initp->ucontext->sp = 0;
    start_proc(initp, trap_return, 0);

    for (;;)
        yield();
}

NO_INLINE NO_RETURN void _panic(const char *file, int line) {
    printk("=====%s:%d PANIC%d!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}
