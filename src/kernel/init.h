#pragma once

#include <common/defines.h>

// #define early_init_func(func) __attribute__((section(".init.early"), used)) static void* __initcall_##func = &func;
// #define init_func(func) __attribute__((section(".init"), used)) static void* __initcall_##func = &func;
// #define rest_init_func(func) __attribute__((section(".init.rest"), used)) static void* __initcall_##func = &func;

// BSS available
/*
#define define_early_init(name) \
static void init_##name(); \
static __attribute__((section(".init.early"), used)) volatile const void* __initcall_##name = &init_##name; \
static void init_##name()
*/
#define define_early_init(name) void early_init_##name()

define_early_init(interrupt);
define_early_init(uart);
define_early_init(clock_handler);
define_early_init(alloc_page_cnt);
define_early_init(printk);
define_early_init(root_proc);
define_early_init(cache);

// Allocator available
/*
#define define_init(name) \
static void init_##name(); \
static __attribute__((section(".init"), used)) volatile const void* __initcall_##name = &init_##name; \
static void init_##name()
*/
#define define_init(name) void init_##name()

define_init(sched);

// Scheduler available
/*
#define define_rest_init(name) \
static void init_##name(); \
static __attribute__((section(".init.rest"), used)) volatile const void* __initcall_##name = &init_##name; \
static void init_##name()
*/
#define define_rest_init(name) void rest_init_##name()

void do_early_init();
void do_init();
void do_rest_init();
