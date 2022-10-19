#pragma once

#include <common/rbtree.h>

#define NCPU 4

struct timer
{
    bool triggered;
    int elapse;
    u64 _key;
    struct rb_node_ _node;
    void (*handler)(struct timer *);
    u64 data;
};

#include <kernel/schinfo_cpu.h>

struct cpu
{
    bool online;
    struct rb_root_ timer;
    struct sched sched;
};

extern struct cpu cpus[NCPU];

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);

// CpuLock is spinlock hold by cpu
typedef struct
{
    struct cpu *volatile holder;
} CpuLock;

void init_cpulock(CpuLock *);

// Non-block acquire cpu lock, return true on success. Panic if already hold by this cpu.
bool _try_acq_cpulock(CpuLock *);
// Block acquire cpu lock. Panic if already hold by this cpu.
void _acq_cpulock(CpuLock *);
// Release cpu lock. Panic if not hold by this cpu.
void _rel_cpulock(CpuLock *);

// Return true if this cpu is holding lock
bool holding_cpulock(CpuLock *);
