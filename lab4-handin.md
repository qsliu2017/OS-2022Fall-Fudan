# Lab4

git commit hash: `git checkout 2d075aa6dc80f06c28fce57ed94d0552324e423e`

## Preemptive Scheduler

FCFS algorithm, but preemptive. Every `89` ms, sched_timer will preempt the cpu and `yield()`.

## Life is short, we need RAII

I hate release lock, especially when there are so many branches and early return.

Referring to Rust's `Drop` and C++'s `unique_ptr`, I wrap `SpinLock` with a gcc [\_\_cleanup\_\_](https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#Common-Variable-Attributes:~:text=cleanup%20(cleanup_function)). There comes the RAII lock:

```C
SpinLock *_raii_acquire_spinlock(SpinLock *lock)
{
    _acquire_spinlock(lock);
    return lock;
}
void _raii_release_spinlock(SpinLock **lock)
{
    _release_spinlock(*lock);
}

// Acquire a spinlock. Auto release after block scope.
#define raii_acquire_spinlock(lock, id) SpinLock *_raii_lock_##id __attribute__((__cleanup__(_raii_release_spinlock))) = _raii_acquire_spinlock(lock)
```

That really saves my life! Could you please consider adding this to the common library?
