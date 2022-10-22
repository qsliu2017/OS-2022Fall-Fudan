#include <driver/sd.h>
#include <kernel/init.h>

extern char early_init[], rest_init[], init[], einit[];

void do_early_init()
{
    early_init_interrupt();
    early_init_uart();
    early_init_clock_handler();
    early_init_printk();
    early_init_alloc_page_cnt();
    early_init_cache();
    early_init_root_proc();
    early_init___syscall_myreport();
}

void do_rest_init()
{
}

void do_init()
{
    init_sched();
    sd_init();
}