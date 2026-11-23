; One program that exercises every call the kernel serves: fill, line, copy,
; text, upload, read and flip, then exits 0.
.globl _start
.text
_start:
  ld r7, #16
  ld r2, #0
  ld r3, #0
  ld r4, #16
  ld r5, #16
  ld r6, #255
  syscall
  ld r7, #19
  ld r2, #line_block
  syscall
  ld r7, #18
  ld r2, #copy_block
  syscall
  ld r7, #21
  ld r2, #upload_block
  syscall
  ld r7, #20
  ld r2, #text_block
  syscall
  ld r7, #2
  ld r2, #read_buf
  ld r3, #4
  syscall
  ld r7, #17
  syscall
  ld r7, #0
  ld r2, #0
  syscall
.data
line_block:
  .word 0
  .word 0
  .word 15
  .word 15
  .word 0x0000FF00
copy_block:
  .word 0
  .word 0
  .word 0
  .word 8
  .word 8
text_block:
  .word 0
  .word 0
  .word greeting
  .word 4
  .word 0x00FFFFFF
  .word 0
upload_block:
  .word source
  .word 0
  .word 4
  .word 4
  .word 8
greeting:
  .word 0x6F6C6C65
read_buf:
  .word 0
source:
  .word 0x000000FF
  .word 0x000000FF
  .word 0x000000FF
  .word 0x000000FF
