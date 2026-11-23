; Echo whatever is queued on the console: read up to eight bytes, write back
; exactly as many as arrived. It proves the kernel's read call moves bytes into
; user memory, because the echoed text comes back out of the buffer.
.globl _start
.text
_start:
  ld r7, #2
  ld r2, #buffer
  ld r3, #8
  syscall
  mov r6, r2
  ld r7, #1
  ld r2, #buffer
  mov r3, r6
  syscall
  ld r7, #0
  ld r2, #0
  syscall
.data
buffer:
  .word 0
  .word 0
