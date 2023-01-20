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

extern int fdalloc(struct file *f);

int execve(const char *path, char *const argv[], char *const envp[]) {
    printk("%s:%d execve(%s)\n", __FILE__, __LINE__, path);

    OpContext ctx;
    bcache.begin_op(&ctx);
    Inode *ip = namei(path, &ctx);
    if (!ip)
        goto on_error;
    inodes.lock(ip);

    struct proc *this = thisproc();

    PTEntriesPtr oldpt = this->pgdir.pt;
    this->pgdir.pt = _init_pt();

    usize sp = 0x0001000000000000 /* Top address of user space. */ - PAGE_SIZE;
    int argc = 0;
    if (argv) {
        for (; argv[argc]; argc++) {
            usize len = strlen(argv[argc]);
            sp -= len + 1;
            copyout(&this->pgdir, (u8 *)sp, argv[argc], len + 1);
        }
    }
    int envc = 0;
    if (envp) {
        for (; envp[envc]; envc++) {
            usize len = strlen(envp[envc]);
            sp -= len + 1;
            copyout(&this->pgdir, (u8 *)sp, envp[envc], len + 1);
        }
    }

    usize newsp =
        round_down(sp - sizeof(void *) * (envc + 1)  /* envp pointers */
                       - sizeof(void *) * (argc + 1) /* argv pointers */
                       - 8                           /* argc */
                   ,
                   16);
    usize newargv = newsp + 8;
    usize newenvp = newargv + sizeof(void *) * (argc + 1);

    copyout(&this->pgdir, (u8 *)newsp, get_zero_page(), 0);

    attach_pgdir(&this->pgdir);
    _free_pgdir(oldpt);

    for (int i = envc - 1; i >= 0; i--) {
        ((u64 *)newenvp)[i] = sp;
        while (*(u8 *)(sp++))
            ;
    }
    for (int i = argc - 1; i >= 0; i--) {
        ((u64 *)newargv)[i] = sp;
        while (*(u8 *)(sp++))
            ;
    }

    sp = newsp;
    *(isize *)sp = argc;

    Elf64_Ehdr ehdr;
    if (inodes.read(ip, (u8 *)&ehdr, 0, sizeof(ehdr)) != sizeof(ehdr))
        goto on_error;

    this->ucontext->elr = ehdr.e_entry;
    this->ucontext->sp = sp;

    if ((ehdr.e_ident[EI_MAG0] != ELFMAG0) ||
        (ehdr.e_ident[EI_MAG1] != ELFMAG1) ||
        (ehdr.e_ident[EI_MAG2] != ELFMAG2) ||
        (ehdr.e_ident[EI_MAG3] != ELFMAG3))
        goto on_error;

    Elf64_Phdr phdr;
    u64 sz = 0, base = 0;
    for (usize i = 0, off = ehdr.e_phoff; i < ehdr.e_phnum;
         i++, off += sizeof(Elf64_Phdr)) {
        if (inodes.read(ip, (u8 *)&phdr, off, sizeof(phdr)) != sizeof(phdr))
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

        for (usize va = phdr.p_vaddr; va < phdr.p_vaddr + phdr.p_memsz;
             va += PAGE_SIZE) {
            auto p = kalloc_page();
            *get_pte(&this->pgdir, va, true) = K2P(p) | PTE_USER_DATA;
        }

        if (!sz) {
            sz = base = phdr.p_vaddr;
            if (base % PAGE_SIZE != 0)
                goto on_error;
        }

        usize newsz = phdr.p_vaddr + phdr.p_memsz;
        for (usize va = round_up(sz, PAGE_SIZE); va < newsz; va += PAGE_SIZE) {
            auto p = kalloc_page();
            *get_pte(&this->pgdir, va, true) = K2P(p) | PTE_USER_DATA;
        }
        sz = newsz;

        attach_pgdir(&this->pgdir);

        inodes.read(ip, (u8 *)phdr.p_vaddr, phdr.p_offset, phdr.p_filesz);

        memset((void *)phdr.p_vaddr + phdr.p_filesz, 0,
               phdr.p_memsz - phdr.p_filesz);
    }

    inodes.unlock(ip);
    bcache.end_op(&ctx);

    return 0;

on_error:
    printk("%s:%d\n", __FILE__, __LINE__);
    return -1;
}
