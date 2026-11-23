; DRAW_TEXT is accepted and answers ERROR until the 10x20 atlas ships. This
; program exits 0 only when the error bit is set, so a future implementation
; that silently draws nothing -- or reports success without drawing -- fails.
.globl _start
.text
_start:
  ld r7, #20
  ld r2, #block
  syscall
  mov r6, r2
  ld r9, #1
  and r6, r9
  ld r9, #1
  sub r9, r6
  ld r7, #0
  mov r2, r9
  syscall
.data
block:
  .word 0
  .word 0
  .word text
  .word 4
  .word 0x00FFFFFF
  .word 0
text:
  .word 0x6C6C6568
