; kernel-draw.bin: a flat kernel image that pokes two words into the framebuffer
; aperture, writes the FLIP bit through the framebuffer register, and exits
; through SYS 0. The frame it commits is the golden the CLI test hashes, which
; is what makes the run reproducible across hosts.
;
; The image is flat and based at 0, so `kr232 vm kernel-draw.bin` reads its entry
; from the reset word at 0x0100. The program's code therefore lands at 0x0104 and
; the linker writes 0x0104 into that word; the file is otherwise zero up to it.
; Like halt.s, it uses SYS (0x73), the emulator hypercall, not SYSCALL.
;
; The ten instructions are the ones the hand-encoded probe carried:
;   ld r2, #0xfc1e0000   ; framebuffer aperture
;   ld r3, #0x112233     ; a pixel word
;   st [r2], r3
;   add r2, #16
;   st [r2], r3
;   ld r4, #0xfff04020   ; the framebuffer control register
;   ld r5, #1
;   st [r4], r5          ; FLIP
;   ld r2, #0
;   sys
.globl _start
.text
_start:
  ld r2, #0xfc1e0000
  ld r3, #0x112233
  st [r2], r3
  add r2, #16
  st [r2], r3
  ld r4, #0xfff04020
  ld r5, #1
  st [r4], r5
  ld r2, #0
  sys
