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
  ld r8, #16
  cmp r7, r8
  beq fill_call
  ld r8, #17
  cmp r7, r8
  beq flip_call
  jmp bad_call

bad_call:
  ; ISA §18: an unimplemented number is -2, and the caller resumes.
  ld r2, #0
  sub r2, #2
  iret

bad_argument:
  ; ISA §18: a refused argument is -1.
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
  bcc bad_argument
  ld r8, #console
  ld r8, [r8]            ; console transmit register
  ld r9, #0
write_loop:
  cmp r9, r3
  bcs write_done
  mov r10, r2
  add r10, r9
  ldbu r5, [r10]
  st [r8], r5
  add r9, #1
  jmp write_loop
write_done:
  ld r2, #0
  iret

fill_call:
  ; R2 x, R3 y, R4 w, R5 h, R6 colour: one FILL_RECT block on the graphics page.
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #3
  st [r8], r9            ; COMMAND = FILL_RECT
  st [r8+4], r2
  st [r8+8], r3
  st [r8+12], r4
  st [r8+16], r5
  st [r8+20], r6
  ld r9, #1
  st [r8+36], r9         ; DOORBELL
  ld r2, [r8+40]         ; STATUS: the device's verdict reaches the caller
  iret

flip_call:
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #7
  st [r8], r9            ; COMMAND = FLIP
  ld r9, #1
  st [r8+36], r9
  ld r2, [r8+40]
  iret

.data
user_low:
  .word 0x08000000
console:
  .word 0xFFF02000
gfx:
  .word 0xFFF09000
