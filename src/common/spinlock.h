#pragma once

#include <common/defines.h>
#include <aarch64/intrinsic.h>
#include <common/checker.h>

typedef struct {
    volatile bool locked;
} SpinLock;

WARN_RESULT bool _try_acquire_spinlock(SpinLock*);
void _acquire_spinlock(SpinLock*);
void _release_spinlock(SpinLock*);

// Init a spinlock. It's optional for static objects.
void init_spinlock(SpinLock*);

// Try to acquire a spinlock. Return true on success.
#define try_acquire_spinlock(checker, lock) (_try_acquire_spinlock(lock) && checker_begin_ctx(checker))

// Acquire a spinlock. Spin until success.
#define acquire_spinlock(checker, lock) checker_begin_ctx_before_call(checker, _acquire_spinlock, lock)

// Release a spinlock
#define release_spinlock(checker, lock) checker_end_ctx_after_call(checker, _release_spinlock, lock)

SpinLock *_raii_acquire_spinlock(SpinLock *lock);
void _raii_release_spinlock(SpinLock **lock);

// Acquire a spinlock. Auto release after block scope.
#define raii_acquire_spinlock(lock, id) SpinLock *_raii_lock_##id __attribute__((__cleanup__(_raii_release_spinlock))) = _raii_acquire_spinlock(lock)
