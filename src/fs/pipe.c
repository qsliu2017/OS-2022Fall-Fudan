#include "common/defines.h"
#include "common/sem.h"
#include "common/spinlock.h"
#include "fs/file.h"
#include <common/string.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

int pipeAlloc(File **f0, File **f1) {
    Pipe *pipe = 0;
    *f0 = *f1 = 0;
    if (!(*f0 = filealloc())) {
        goto cleanup;
    }
    if (!(*f1 = filealloc())) {
        goto cleanup;
    }
    if (!(pipe = kalloc(sizeof(Pipe)))) {
        goto cleanup;
    }

    init_spinlock(&pipe->lock);
    init_sem(&pipe->wlock, PIPESIZE);
    init_sem(&pipe->rlock, 0);
    pipe->nread = pipe->nwrite = 0;
    pipe->readopen = 1;
    pipe->writeopen = 1;

    (*f0)->type = (*f1)->type = FD_PIPE;
    (*f0)->readable = 1, (*f0)->writable = 0;
    (*f1)->readable = 0, (*f1)->writable = 1;
    (*f0)->pipe = (*f1)->pipe = pipe;
    (*f0)->off = (*f1)->off = 0;

    return 0;

cleanup:
    if (*f0) {
        fileclose(*f0);
        *f0 = NULL;
    }
    if (*f1) {
        fileclose(*f1);
        *f1 = NULL;
    }
    if (pipe) {
        kfree(pipe);
    }
    return -1;
}

void pipeClose(Pipe *pi, int writable) {
    raii_acquire_spinlock(&pi->lock, pipeClose);
    if (writable) {
        pi->writeopen--;
    } else {
        pi->readopen--;
    }

    if (!pi->readopen && !pi->writeopen) {
        kfree(pi);
    }
}

int pipeWrite(Pipe *pi, u64 addr, int n) {
    int cnt;
    for (cnt = 0; cnt < n; cnt++) {
        unalertable_wait_sem(&pi->wlock);

        pi->data[pi->nwrite++ % PIPESIZE] = *(char *)(addr + cnt);
        _post_sem(&pi->rlock);
    }
    return cnt;
}

int pipeRead(Pipe *pi, u64 addr, int n) {
    int cnt;
    for (cnt = 0; cnt < n; cnt++) {
        unalertable_wait_sem(&pi->rlock);

        pi->data[pi->nread++ % PIPESIZE] = *(char *)(addr + cnt);
        _post_sem(&pi->wlock);
    }
    return cnt;
}