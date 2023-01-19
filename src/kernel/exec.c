#include <aarch64/trap.h>
#include <common/defines.h>
#include <common/string.h>
#include <elf.h>
#include <fs/cache.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>

// static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[]) {
    printk("%s:%d execve(%s)\n", __FILE__, __LINE__, path);
    UNUSE(envp);
    struct proc *this = thisproc();
    int argc;
    for (argc = 0; argv[argc]; argc++)
        ;
    OpContext ctx;
    bcache.begin_op(&ctx);
    Inode *ip = namei(path, &ctx);
    bcache.end_op(&ctx);
    Elf64_Ehdr ehdr;
    File *fp = filealloc();
    fp->type = FD_INODE;
    fp->ref = 0;
    fp->readable = 1;
    fp->writable = 0;
    fp->ip = ip;
    fp->off = 0;

    if (fileread(fp, (char *)&ehdr, sizeof(ehdr)) != sizeof(ehdr))
        goto on_error;

    if ((ehdr.e_ident[EI_MAG0] != ELFMAG0) ||
        (ehdr.e_ident[EI_MAG1] != ELFMAG1) ||
        (ehdr.e_ident[EI_MAG2] != ELFMAG2) ||
        (ehdr.e_ident[EI_MAG3] != ELFMAG3))
        goto on_error;

    struct pgdir *pd = &this->pgdir;

    Elf64_Phdr phdr;

    for (usize i = 0, off = ehdr.e_phoff; i < ehdr.e_phnum;
         i++, off += sizeof(Elf64_Phdr)) {
        if (fileread(fp, (char *)&phdr, sizeof(phdr)) != sizeof(phdr))
            goto on_error;
        if (phdr.p_type != PT_LOAD)
            continue;
        if (phdr.p_memsz < phdr.p_filesz)
            goto on_error;

        // a user could construct an ELF binary with a ph.vaddr that points to a
        // user-chosen address, and ph.memsz large enough that the sum overflows
        // to 0x1000, which will look like a valid value.
        if (phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr)
            goto on_error;

        if (phdr.p_vaddr % PAGE_SIZE)
            goto on_error;

        u64 flags;
        switch (phdr.p_flags) {
        case PT_LOAD:
            flags = ST_SWAP | ST_TEXT;
            break;
        case PT_DYNAMIC:
            flags = ST_SWAP | ST_DATA;
            break;
        default:
            goto on_error;
        }

        mmap(pd, phdr.p_vaddr, fp, phdr.p_offset, phdr.p_filesz, flags);
    }

    this->ucontext->x[0] = 0;

on_error:
    return -1;
}
