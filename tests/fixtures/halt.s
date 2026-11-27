; halt.bin: the smallest flat image. It exits through SYS 0 with status 0, which
; is what the CLI tests use to check the frame hash and the instruction budget.
;
; The image is flat, not a container: `kr232 vm halt.bin --base 0x08000000`
; loads the file at the user base and starts at the base, so there is no reset
; word and the entry is the first byte. It runs with K set, which is why it uses
; SYS rather than SYSCALL: SYS is the emulator hypercall, and a container loaded
; at the same base runs in user mode where it would raise vector 9.
;
; The program is two instructions: `ld r2, #0` and `sys`. The assembler encodes
; the load in its three-byte form today (`c6 20 00`), so the image is four bytes;
; the original probe used the five-byte form (`52 00 00 00 00`) and was six. The
; behaviour is the same: SYS with R2 = 0 halts with status 0 and commits no
; frame, so the frame hash stays the FNV offset basis.
.globl _start
.text
_start:
  ld r2, #0
  sys
