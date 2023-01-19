// Do kernel-mode context switch
// x0 (first parameter): new context ptr
// x1 (second parameter): addr to save old context ptr

#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

.globl swtch
swtch:
  pushp(x19, xzr)
  pushp(x21, x20)
  pushp(x23, x22)
  pushp(x25, x24)
  pushp(x27, x26)
  pushp(x29, x28)
  pushp(x30, x30)
  mov x15, sp
  str x15, [x1]

  mov sp, x0
  popp(x15, x30)
  popp(x29, x28)
  popp(x27, x26)
  popp(x25, x24)
  popp(x23, x22)
  popp(x21, x20)
  popp(x19, xzr)

  ret x15
