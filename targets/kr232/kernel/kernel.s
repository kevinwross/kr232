; kr232 kernel: install vectors, build a user frame, IRET into user mode, and
; answer the user's exit through the syscall vector.
.globl _start
.text
_start:
  ld r8, #0x0100
  ld r9, #syscall_handler
  st [r8+36], r9         ; vector 9, system call
  ; The frame IRET restores: [EFP+0] previous EFP, +4 PC, +8 flags, +12 SP.
  ; K is bit 6 and stays clear, so the return lands in user mode.
  sub sp, #16
  ld r10, #0
  st [sp], r10           ; previous EFP
  st [sp+4], r2          ; user entry, which --user placed in R2
  st [sp+8], r10         ; flags: K and I clear
  ld r10, #0x0FFFFFF0
  st [sp+12], r10        ; user stack, top of user RAM
  mov r12, sp
  iret

syscall_handler:
  ; R7 selects the call; 0 is exit and returns R2 as the status.
  ld r7, #0
  sys
