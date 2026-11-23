; A program that draws nothing and commits the surface: the frame hash it leaves
; is the blank committed frame, which no drawing golden may equal.
.globl _start
.text
_start:
  ld r8, #0xFFF04020
  ld r9, #1
  st [r8], r9
  ld r7, #0
  ld r2, #0
  sys
