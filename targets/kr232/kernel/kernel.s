; kr232 kernel: install vectors, enter the user program, and serve its calls.
.globl _start
.text
_start:
  ld r8, #0x0100
  ld r9, #syscall_handler
  st [r8+36], r9         ; vector 9, system call
  sub sp, #16
  ld r10, #0
  st [sp], r10
  st [sp+4], r2          ; user entry from --user
  st [sp+8], r10         ; flags: K clear
  ld r10, #0x0FFFFFF0
  st [sp+12], r10
  mov r12, sp
  iret

syscall_handler:
  ld r8, #0
  cmp r7, r8
  beq exit_call
  ld r8, #1
  cmp r7, r8
  beq write_call
  jmp bad_call

bad_call:
  ld r2, #0
  sub r2, #1
  iret

exit_call:
  ld r7, #0
  sys

write_call:
  ; R2 is a user pointer, R3 a length. Refuse anything below user RAM before
  ; touching it, so a bad pointer is an error instead of a kernel fault.
  ld r8, #user_low
  ld r8, [r8]
  cmp r2, r8
  bcc bad_call
  ld r8, #console
  ld r8, [r8]            ; console transmit register
  ld r9, #0
write_loop:
  cmp r9, r3
  bcs write_done
  mov r10, r2
  add r10, r9
  ldbu r11, [r10]
  st [r8], r11
  add r9, #1
  jmp write_loop
write_done:
  ld r2, #0
  iret
.data
user_low:
  .word 0x08000000
console:
  .word 0xFFF02000
