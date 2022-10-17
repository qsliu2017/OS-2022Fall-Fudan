#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
  pushp(x17, x18)
  pushp(x15, x16)
  pushp(x13, x14)
  pushp(x11, x12)
  pushp(x9, x10)
  pushp(x7, x8)
  pushp(x5, x6)
  pushp(x3, x4)
  pushp(x1, x2)

  mrs x3, spsr_el1
  mrs x2, elr_el1
  mrs x1, sp_el0

  pushp(x1, x0)
  pushp(x3, x2)

  mov x0, sp
  bl trap_global_handler

.global trap_return
trap_return:
  popp(x3, x2)
  popp(x1, x0)

  msr spsr_el1, x3
  msr elr_el1, x2
  msr sp_el0, x1

  popp(x1, x2)
  popp(x3, x4)
  popp(x5, x6)
  popp(x7, x8)
  popp(x9, x10)
  popp(x11, x12)
  popp(x13, x14)
  popp(x15, x16)
  popp(x17, x18)

  eret

