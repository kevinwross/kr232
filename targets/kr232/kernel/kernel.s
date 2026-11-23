; kr232 kernel entry. The vector table sits at 0x0100, which the loader places
; first, so the trap entry is the image's entry point. This stage answers a
; syscall by halting with its number as the status; the handler shapes below
; grow the table one vector at a time.
.globl _start
.text
_start:
  ld r7, #0
  sys
