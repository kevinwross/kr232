; kr232 kernel: install the vectors, enter the user program, and serve its calls.
;
; The table is the one kr232-devices.md ?14 records: 0 exit, 1 write, 2 read,
; 16 fill_rect, 17 flip, 18 copy_rect, 19 draw_line, 20 draw_text, 21 upload.
; Calls that need more than four arguments take a pointer to a word block in
; user memory -- one command block through SYSCALL, as ?11.1 says.
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
  ld r8, #2
  cmp r7, r8
  beq read_call
  ld r8, #16
  cmp r7, r8
  beq fill_call
  ld r8, #17
  cmp r7, r8
  beq flip_call
  ld r8, #18
  cmp r7, r8
  beq copy_call
  ld r8, #19
  cmp r7, r8
  beq line_call
  ld r8, #20
  cmp r7, r8
  beq text_call
  ld r8, #21
  cmp r7, r8
  beq upload_call
  ld r8, #22
  cmp r7, r8
  beq fb_info_call
  ld r8, #23
  cmp r7, r8
  beq input_next_call
  ld r8, #24
  cmp r7, r8
  beq input_pending_call
  jmp bad_call

bad_call:
  ; An unimplemented number is -2 and the caller resumes (ISA ?18).
  ld r2, #0
  sub r2, #2
  iret

bad_argument:
  ; A refused argument is -1.
  ld r2, #0
  sub r2, #1
  iret

exit_call:
  ld r7, #0
  sys

write_call:
  ; R2 user pointer, R3 length: refuse a range that leaves user RAM before
  ; touching it, so a long length is an argument error and not a trap halfway.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  mov r5, r2
  add r5, r3
  cmp r5, r2             ; a wrapped sum lands below the pointer
  bcc bad_argument
  ld r10, #user_high
  ld r10, [r10]
  cmp r5, r10
  bhi bad_argument
  ld r8, #console
  ld r8, [r8]
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
  mov r2, r9
  iret

read_call:
  ; R2 user pointer, R3 length: 0 bytes means end of input, not an error.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #console
  ld r8, [r8]
  ld r9, #0
  ld r10, #0
  sub r10, #1            ; the empty-queue sentinel
read_loop:
  cmp r9, r3
  bcs read_done
  ld r5, [r8+4]          ; console IN
  cmp r5, r10
  beq read_done
  mov r6, r2
  add r6, r9
  stb [r6], r5
  add r9, #1
  jmp read_loop
read_done:
  mov r2, r9
  iret

fill_call:
  ; R2 x, R3 y, R4 w, R5 h, R6 colour.
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #3
  st [r8], r9            ; COMMAND = FILL_RECT
  st [r8+4], r2
  st [r8+8], r3
  st [r8+12], r4
  st [r8+16], r5
  st [r8+20], r6
  jmp submit

flip_call:
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #7
  st [r8], r9            ; COMMAND = FLIP
  jmp submit

copy_call:
  ; R2 points at source offset, x, y, w, h.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #4
  st [r8], r9            ; COMMAND = COPY_RECT
  ld r5, [r2]
  st [r8+4], r5
  ld r5, [r2+4]
  st [r8+8], r5
  ld r5, [r2+8]
  st [r8+12], r5
  ld r5, [r2+12]
  st [r8+16], r5
  ld r5, [r2+16]
  st [r8+20], r5
  jmp submit

line_call:
  ; R2 points at x0, y0, x1, y1, colour.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #5
  st [r8], r9            ; COMMAND = DRAW_LINE
  ld r5, [r2]
  st [r8+4], r5
  ld r5, [r2+4]
  st [r8+8], r5
  ld r5, [r2+8]
  st [r8+12], r5
  ld r5, [r2+12]
  st [r8+16], r5
  ld r5, [r2+16]
  st [r8+20], r5
  jmp submit

text_call:
  ; R2 points at x, y, pointer, length, fg, bg.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #6
  st [r8], r9            ; COMMAND = DRAW_TEXT
  ld r5, [r2]
  st [r8+4], r5
  ld r5, [r2+4]
  st [r8+8], r5
  ld r5, [r2+8]
  st [r8+12], r5
  ld r5, [r2+12]
  st [r8+16], r5
  ld r5, [r2+16]
  st [r8+20], r5
  ld r5, [r2+20]
  st [r8+24], r5
  jmp submit

upload_call:
  ; R2 points at source address, surface offset, w, h, source stride.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #gfx
  ld r8, [r8]
  ld r9, #10
  st [r8], r9            ; COMMAND = UPLOAD
  ld r5, [r2]
  st [r8+4], r5
  ld r5, [r2+4]
  st [r8+8], r5
  ld r5, [r2+8]
  st [r8+12], r5
  ld r5, [r2+12]
  st [r8+16], r5
  ld r5, [r2+16]
  st [r8+20], r5
  jmp submit

fb_info_call:
  ; R2 points at three words that receive width, height and stride.
  ld r9, #user_low
  ld r9, [r9]
  cmp r2, r9
  bcc bad_argument
  ld r8, #fb
  ld r8, [r8]
  ld r5, [r8+4]          ; WIDTH: +0 is CONTROL
  st [r2], r5
  ld r5, [r8+8]          ; HEIGHT
  st [r2+4], r5
  ld r5, [r8+12]         ; STRIDE
  st [r2+8], r5
  ld r2, #0
  iret

input_next_call:
  ; One keyboard event, or 0xFFFFFFFF when the queue is empty.
  ld r8, #keyboard
  ld r8, [r8]
  ld r5, [r8]
  mov r2, r5
  iret

input_pending_call:
  ; The queue length is the low byte of the status word's upper half.
  ld r8, #keyboard
  ld r8, [r8]
  ld r5, [r8+4]
  shr r5, #8
  ld r9, #0xFF
  and r5, r9
  mov r2, r5
  iret

submit:
  ; Ring the doorbell and answer the device's own verdict.
  ld r9, #1
  st [r8+36], r9
  ld r2, [r8+40]         ; STATUS
  iret
.data
user_low:
  .word 0x08000000
user_high:
  .word 0xFC000000       ; one past the last user byte
console:
  .word 0xFFF02000
gfx:
  .word 0xFFF09000
fb:
  .word 0xFFF04000
keyboard:
  .word 0xFFF06000
