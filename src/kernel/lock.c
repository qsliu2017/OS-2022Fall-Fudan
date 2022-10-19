#include <kernel/cpu.h>
#include <kernel/sched.h>
#include <kernel/proc.h>

void init_proclock(ProcLock *l)
{
  l->holder = NULL;
}

static inline bool __try_acq_proclock(ProcLock *l)
{
  auto this = thisproc();
  struct proc *expected = NULL;
  bool acq = __atomic_compare_exchange_n(&l->holder, &expected, this, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
  return acq;
}

bool _try_acq_proclock(ProcLock *l)
{
  ASSERT(!holding_proclock(l));
  return __try_acq_proclock(l);
}

void _acq_proclock(ProcLock *l)
{
  ASSERT(!holding_proclock(l));
  while (!_try_acq_proclock(l))
    arch_yield();
}

void _rel_proclock(ProcLock *l)
{
  ASSERT(holding_proclock(l));
  auto this = thisproc();
  ASSERT(__atomic_compare_exchange_n(&l->holder, &this, NULL, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

ProcLock *__raii_acq_proclock(ProcLock *l)
{
  _acq_proclock(l);
  return l;
}

void __raii_rel_proclock(ProcLock **l)
{
  return _rel_proclock(*l);
}

bool holding_proclock(ProcLock *l)
{
  return thisproc() == l->holder;
}

void init_cpulock(CpuLock *l)
{
  l->holder = NULL;
}

// Do not check holding
static inline bool __try_acq_cpulock(CpuLock *l)
{
  struct cpu *expected = NULL;
  auto this = &cpus[cpuid()];
  return __atomic_compare_exchange_n(&l->holder, &expected, this, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

bool _try_acq_cpulock(CpuLock *l)
{
  ASSERT(!holding_cpulock(l));
  return __try_acq_cpulock(l);
}

void _acq_cpulock(CpuLock *l)
{
  ASSERT(!holding_cpulock(l));
  while (!_try_acq_cpulock(l))
    arch_yield();
}

void _rel_cpulock(CpuLock *l)
{
  ASSERT(holding_cpulock(l));
  auto this = &cpus[cpuid()];
  ASSERT(__atomic_compare_exchange_n(&l->holder, &this, NULL, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

bool holding_cpulock(CpuLock *l)
{
  return &cpus[cpuid()] == l->holder;
}
