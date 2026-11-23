; The gate's drawing program: one character to the terminal, an 8x8 green
; rectangle through the graphics commands, and a flip, so the frame hash is
; something a device that draws nothing cannot produce.
.globl _start
.text
_start:
  ld r2, #0xFFF08000
  ld r3, #0x4B
  st [r2], r3
  ld r4, #0xFFF09000
  ld r5, #3
  st [r4+0], r5
  ld r2, #0
  st [r4+4], r2
  st [r4+8], r2
  ld r5, #8
  st [r4+12], r5
  st [r4+16], r5
  ld r6, #0xFF00
  st [r4+20], r6
  ld r5, #1
  st [r4+36], r5
  ld r2, #0xFFF04020
  ld r5, #1
  st [r2], r5
  ld r2, #0
  sys
