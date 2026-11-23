; kr232 kernel, first stage: install a vector, take a trap, return from it.
; The reset word at 0x0100 is the image's entry, so the kernel runs in kernel
; mode from the first instruction and fills the table it will need.
.globl _start
.text
_start:
  ld r8, #0x0100
  ld r9, #breakpoint
  st [r8+4], r9          ; vector 1, breakpoint
  brk
  ld r7, #0
  sys                    ; status 0 only if the handler returned

breakpoint:
  ; The saved PC is the faulting instruction, so resume one byte later.
  ld r9, [r12+4]
  add r9, #1
  st [r12+4], r9
  iret
