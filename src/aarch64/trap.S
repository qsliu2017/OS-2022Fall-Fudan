#define pushp(a, b) stp a, b, [sp, #-0x10]!
#define popp(a, b) ldp a, b, [sp], #0x10 

/* `exception_vector.S` send all traps here. */
.global trap_entry
trap_entry:
  str q0, [sp, #-16]!

  pushp (x30, xzr)
  pushp (x28, x29)
  pushp (x26, x27)
  pushp (x24, x25)
  pushp (x22, x23)
  pushp (x20, x21)
  pushp (x18, x19)
  pushp (x16, x17)
  pushp (x14, x15)
  pushp (x12, x13)
  pushp (x10, x11)
  pushp (x8, x9)
  pushp (x6, x7)
  pushp (x4, x5)
  pushp (x2, x3)
  pushp (x0, x1)

  mrs x12, tpidr_el0
  mrs x11, sp_el0
  mrs x10, elr_el1
  mrs x9, spsr_el1

  pushp (x11, x12)
  pushp (x9, x10)

  mov x0, sp
  bl trap_global_handler

.global trap_return
trap_return:
  popp (x9, x10)
  popp (x11, x12)

  msr spsr_el1, x9
  msr elr_el1, x10
  msr sp_el0, x11
  msr tpidr_el0, x12

  popp (x0, x1)
  popp (x2, x3)
  popp (x4, x5)
  popp (x6, x7)
  popp (x8, x9)
  popp (x10, x11)
  popp (x12, x13)
  popp (x14, x15)
  popp (x16, x17)
  popp (x18, x19)
  popp (x20, x21)
  popp (x22, x23)
  popp (x24, x25)
  popp (x26, x27)
  popp (x28, x29)
  popp (x30, xzr)

  ldr q0, [sp], #16

  ic  iallu
  dsb sy
  isb
  eret

