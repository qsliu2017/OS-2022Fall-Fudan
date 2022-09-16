#include <aarch64/intrinsic.h>
#include "common/string.h"
#include "kernel/init.h"
#include "driver/uart.h"

static char hello[16];
extern u8 *const edata;
extern u8 *const end;

NO_RETURN void main()
{
    // exit cpu other than 0
    if (cpuid())
        goto stop_cpu;

    memset(edata, 0, end - edata);

    do_early_init();

    do_init();

stop_cpu:
    arch_stop_cpu();
}

define_early_init(fill_hello)
{
    char content[] = "Hello world!";
    strncpy_fast(hello, content, sizeof(content));
}

define_init(output_hello)
{
    for (char *ptr = hello; *ptr; ptr++)
        uart_put_char(*ptr);
}
