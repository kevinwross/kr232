# kr232 — Devices, display and the graphics package

| | |
|---|---|
| Status | **Draft 0.2** — the device and display contract for the kr232 plan. Devices are kernel-only and the packages have a kernel and a user path; both are open to change |
| Date | 2026-11-22T08:55:00-08:00 |
| Basis | `kr232-isa.md` 0.3 (§2.3 memory map and protection, §14 device window) and the kr32 device work: `..\kr32\docs\devices.md`, `display.md`, `terminal.md`, `graphics.md` |
| Applies to | the planned `kr232-emu` device model, the host display window, and the guest `fb`, `gfx`, `cons` and `input` packages |
| Normative | §2–§10 are device contracts the ISA points at. §11 is a *package* contract, not ISA: it describes a guest library the plan will build |
| Access | every device register and the framebuffer aperture are kernel-only (ISA §2.3); a user-mode access raises a protection fault, and user code reaches a device only through `SYSCALL` |
| Open items | §13 lists the differences from kr32, §14 what planning still has to settle |

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

**Why a display at all.** kr232 is a teaching machine, and a screen is the one peripheral that makes
a machine legible: a console for text, a framebuffer for pictures, and a small gfx library that
makes pictures without hand-written pixel loops. The CPU is emulated, so the guest must not
rasterise: kr32's measurements show a console scroll costing seconds of guest time, and the fix
there was to move the pixel work into the host. kr232 takes that model from the start — the console
is a host-rendered terminal, the drawing primitives are host-executed commands, and kernel-mode code
can still write pixels directly when it wants to. Devices are kernel-only (ISA §2.3), so a user
program reaches all of it through `SYSCALL`.

**Map in one line.** 32 MiB of VRAM at `0xFC000000`, and a 960 KiB device window at `0xFFF00000`
with one device per 4 KiB page. `kr232-isa.md` §14 is normative for the map and the access rules;
this document fills in the registers.

## 1. Device window

| Page | Base | Device | Section |
|---|---|---|---|
| 0 | `0xFFF00000` | timer | §3 |
| 1 | `0xFFF01000` | reserved: block device planned | §10 |
| 2 | `0xFFF02000` | console: polled byte stream | §4 |
| 3 | `0xFFF03000` | reserved: interrupt router planned | §10 |
| 4 | `0xFFF04000` | framebuffer control | §5 |
| 5 | `0xFFF05000` | reserved: serial tty planned | §10 |
| 6 | `0xFFF06000` | keyboard | §6 |
| 7 | `0xFFF07000` | reserved: pointer planned | §10 |
| 8 | `0xFFF08000` | terminal: host-rendered text console | §7 |
| 9 | `0xFFF09000` | graphics commands: host-rasterised drawing | §8 |
| 10 | `0xFFF0A000` | system control: halt, reset, cycle counter | §9 |
| 11–239 | `0xFFF0B000`–`0xFFFEFFFF` | reserved: any access traps | §10 |

Within a page, `base + 0x3C` is always the `ID` word. Nothing else about a page is shared between
devices.

## 2. Access rules

- A device access in user mode (K = 0) raises a protection fault (vector 8) before any decode, so
  no register and no VRAM byte is reachable from a user program. That check comes first; the rules
  below only apply in kernel mode.
- A device register is a 32-bit word at a 4-byte offset. Only an aligned 32-bit load or store to an
  offset the device defines is a defined access; anything else traps with vector 4 (address error).
- A *value* the device rejects is not a trap: the write leaves the register unchanged and the
  device reports the failure through its status bits. Mode-setting code is therefore safe to run
  without a trap handler.
- A read of a write-only register returns 0 unless the table says otherwise.
- A read of an offset the device does not define traps. There are no "harmless" holes in a device
  page, which is what makes a typo in a register offset visible.
- Bits a register does not define read 0 and ignore writes.
- Nothing in this draft raises an interrupt. Interrupt-enable bits are reserved and read 0.

## 3. Timer — page 0 (`0xFFF00000`)

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `CONTROL` | RW | 0 | bit 0 enable, bit 1 periodic, bit 2 interrupt enable (reserved) |
| `0x04` | `RELOAD` | RW | 0 | ticks; a write also loads `COUNT` |
| `0x08` | `COUNT` | RO | 0 | remaining ticks |
| `0x0C` | `STATUS` | RW1C | 0 | bit 0 `PENDING`; write 1 to clear |
| `0x10` | `IRQ` | RW | 0 | interrupt line, reserved until interrupts exist |
| `0x3C` | `ID` | RO | `0x544D5231` | `TMR1` |

A tick is one machine cycle — the same free-running counter the system page exposes. The ISA does
not fix cycles per instruction, so a period is an instruction budget, not a wall-clock promise.

Behaviour:

- While `CONTROL.bit0` is set, `COUNT` decrements once per tick.
- When `COUNT` reaches zero, `STATUS.PENDING` is set. If `CONTROL.bit1` (periodic) is set, `COUNT`
  is reloaded from `RELOAD`; otherwise `CONTROL.bit0` is cleared and `COUNT` stays at zero.
- Writing `RELOAD` stores the value and loads `COUNT`; a value of 0 disables the countdown.
- `STATUS.PENDING` is independent of `CONTROL`: clearing the enable does not clear it.
- `IRQ` is a plain register in this draft. It exists so that the register layout does not move when
  interrupts are added.

## 4. Console — page 2 (`0xFFF02000`)

The console is the polled byte stream: the host's standard output and standard input, which is the
same pair the `SYS` `write`/`read` calls use (ISA §13). It is the debug transport; the *screen*
console is the terminal in §7.

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `OUT` | WO | — | bits 0–7 are written to the host output stream |
| `0x04` | `IN` | RO | — | next input byte in bits 0–7; `0xFFFFFFFF` when empty |
| `0x08` | `STATUS` | RO | — | bit 0 `RX_READY`, bit 1 `TX_READY` (always 1), bit 2 `OVERRUN` (sticky) |
| `0x3C` | `ID` | RO | `0x434F4E31` | `CON1` |

- Reading `IN` consumes one byte; when the queue is empty it returns `0xFFFFFFFF` and changes
  nothing. A zero byte is a valid byte, which is why the empty sentinel is not 0.
- `OVERRUN` is set when input arrives while the queue is full and is cleared by writing 1 to
  `STATUS`.
- The console never echoes, never translates newlines, and never interprets control characters; a
  host that attaches a terminal does that on its side of the wire.
- Reading `OUT` returns 0.

## 5. Framebuffer — page 4 (`0xFFF04000`) and the aperture

### 5.1 Aperture

`0xFC000000`–`0xFDFFFFFF` is 32 MiB of VRAM (ISA §14.2). It is memory-like: any access width, any
alignment, any offset. A pixel is one little-endian 32-bit word `0x00RRGGBB` (XRGB8888); its bytes
in memory are `B, G, R, 0`. The top byte is ignored and the host presents the pixel opaque.

### 5.2 Control registers

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `CONTROL` | RW | 0 | bit 0 enable; other bits ignored |
| `0x04` | `WIDTH` | RW | 800 | 1..1920; a successful write sets `STRIDE = WIDTH*4` |
| `0x08` | `HEIGHT` | RW | 600 | 1..1080 |
| `0x0C` | `STRIDE` | RW | 3200 | `WIDTH*4..7680`, multiple of 4 |
| `0x10` | `FORMAT` | RW | 1 | 1 = XRGB8888; nothing else accepted |
| `0x14` | `FRONT` | RW | 0 | 4-byte-aligned byte offset in the aperture |
| `0x18` | `BACK` | RW | `0x001E0000` | 4-byte-aligned byte offset in the aperture |
| `0x1C` | `STATUS` | RO/W1C | 0 | bit 0 committed enable, bit 1 `MODE_OK`, bit 2 sticky `FLIP_ERROR` (write 1 to clear) |
| `0x20` | `FLIP` | WO | — | write bit 0 to commit; reads 0 |
| `0x24` | `FRAME` | RO | 0 | committed-frame counter |
| `0x3C` | `ID` | RO | `0x46425231` | `FBR1` |

The default `BACK` is `0x001E0000`, above the 1,920,000-byte (`0x1D4C00`) default surface, so two
800x600 surfaces fit in the first 4 MiB of the aperture and the rest is scratch.

### 5.3 `FLIP`

`FLIP` is the only transition. Writing bit 0 validates the bundle:

1. `WIDTH` is 1..1920 and `HEIGHT` is 1..1080.
2. `STRIDE` is a multiple of 4 and `WIDTH*4 ≤ STRIDE ≤ 7680`.
3. `FORMAT` is 1.
4. `FRONT` and `BACK` are multiples of 4.
5. `offset + HEIGHT*STRIDE ≤ 32 MiB` for both offsets, in 64-bit arithmetic.

On success the committed frame becomes `{CONTROL.bit0, WIDTH, HEIGHT, STRIDE, FORMAT, BACK}` — the
surface the guest just drew — and then:

- `FRONT` and `BACK` swap;
- `FRAME` increments;
- `FLIP_ERROR` is left alone.

On failure `FLIP_ERROR` is set and nothing else changes. A flip while disabled commits
`ENABLED = 0`, which is how a program hides the screen without changing the mode.

`STATUS.MODE_OK` is computed on read: 1 when the current registers would pass the validation above.
It lets a program find out why a flip failed without guessing.

### 5.4 Presentation

The host displays the committed frame: `FRONT`, `WIDTH`, `HEIGHT`, `STRIDE`, `FORMAT`, and the
enable bit, all captured at the last successful flip. Registers written after a flip do not affect
what is on screen until the next flip, so a program can build a frame while the old one is
displayed. Before the first successful flip the committed frame is the reset configuration:
disabled, 800x600, stride 3200, format 1, front 0.

Writes never fault. An out-of-range write leaves the register unchanged; the rejection is visible
through `MODE_OK` and `FLIP_ERROR`.

The terminal (§7) and the graphics device (§8) paint into the same back surface the guest draws on:
text, host drawing and guest pixels compose in one buffer, and the console is drawn last so it sits
on top.

## 6. Keyboard — page 6 (`0xFFF06000`)

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `DATA` | RO | — | pop one event word; `0xFFFFFFFF` when the FIFO is empty |
| `0x04` | `STATUS` | RO/W1C | 0 | bit 0 `AVAILABLE`, bit 1 `OVERRUN` (write 1 to clear), bits 8–15 count |
| `0x3C` | `ID` | RO | `0x4B424431` | `KBD1` |

An event word is packed little-endian:

| Bits | Field |
|---|---|
| 0–7 | ASCII code, 0 when the key has no character |
| 8–15 | modifiers: bit 0 shift, bit 1 control, bit 2 alt, bit 3 caps lock |
| 16–23 | scancode |
| 24–31 | type: 0 key down, 1 key up |

- The FIFO holds 64 events. The 65th is dropped and sets `OVERRUN`.
- Reading `DATA` pops the oldest event; reading it empty returns `0xFFFFFFFF` and sets nothing.
- The scancode byte is a device-defined code. `kr232-plan.md` will fix the table; until then a guest
  that wants text uses the ASCII byte and the modifiers, which are stable.
- No interrupt. A guest polls `STATUS.AVAILABLE` and then reads `DATA`.

## 7. Terminal — page 8 (`0xFFF08000`)

The terminal is the *screen* console. The host owns the character grid, the font, the palette, the
scrollback ring and the rendering; the guest writes characters. A console scroll therefore costs
the guest one store, not half a million pixel stores.

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `DATA` | WO | — | write one character; bits 0–7 |
| `0x04` | `STATUS` | RO | — | bit 0 `READY` (always 1), bits 8–15 columns, bits 16–23 rows |
| `0x08` | `CONTROL` | RW | 0 | bit 0 cursor visible, bit 1 console hidden |
| `0x0C` | `COMMAND` | WO | — | 1 clear, 2 full reset |
| `0x10` | `COLOR` | RW | `0x07` | bits 0–3 foreground index, bits 4–7 background |
| `0x3C` | `ID` | RO | `0x5445524D` | `TERM` |

### 7.1 Geometry

A cell is 10x20 pixels (the same atlas `DRAW_TEXT` uses). The grid is `columns = WIDTH / 10` and
`rows = HEIGHT / 20` of the *committed* framebuffer mode: the default 800x600 gives 80x30. A mode
change resets the grid and homes the cursor; a guest that changes the mode should expect to redraw.

### 7.2 Character handling

- `0x0A` newline: move to the next row; at the last row the grid scrolls up one line.
- `0x0D` carriage return: column 0.
- `0x08` backspace: one column left, no erase.
- `0x09` tab: advance to the next multiple of eight columns.
- `0x0C` form feed: clear the grid and home the cursor (same as `COMMAND` 1).
- Other bytes below `0x20` and `0x7F` are ignored. There are no escape sequences in v0.1.
- At the right margin the cursor wraps to the next row; a write at the bottom scrolls.

### 7.3 Palette

`COLOR` selects from a fixed 16-entry palette. A cell stores a character and the two indices that
were current when it was written; changing `COLOR` does not recolour existing cells.

| # | Colour | RGB | # | Colour | RGB |
|---|---|---|---|---|---|
| 0 | black | `0x000000` | 8 | dark grey | `0x555555` |
| 1 | blue | `0x0000AA` | 9 | light blue | `0x5555FF` |
| 2 | green | `0x00AA00` | 10 | light green | `0x55FF55` |
| 3 | cyan | `0x00AAAA` | 11 | light cyan | `0x55FFFF` |
| 4 | red | `0xAA0000` | 12 | light red | `0xFF5555` |
| 5 | magenta | `0xAA00AA` | 13 | light magenta | `0xFF55FF` |
| 6 | brown | `0xAA5500` | 14 | yellow | `0xFFFF55` |
| 7 | light grey | `0xAAAAAA` | 15 | white | `0xFFFFFF` |

### 7.4 Composition and visibility

- The terminal paints into the framebuffer back surface, after the graphics device, so text sits on
  top of drawings. A full-screen graphics program hides the console with `CONTROL.bit1`; clearing
  the bit redraws the whole grid.
- With `CONTROL.bit1` set the grid keeps updating and nothing is drawn.
- The terminal never flips. The guest decides when a frame is presented, exactly as for graphics —
  `con_present()` is one `FLIP`.
- With no framebuffer enabled the terminal still accepts writes and maintains its grid; nothing is
  rendered.

## 8. Graphics commands — page 9 (`0xFFF09000`)

Drawing primitives execute in the host. A guest submits a command by writing the registers and then
the doorbell; the host validates the arguments and rasterises.

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `COMMAND` | RW | 0 | operation number (§8.2) |
| `0x04`–`0x20` | `ARG0`–`ARG7` | RW | 0 | arguments, meaning per operation |
| `0x24` | `DOORBELL` | WO | — | write any value to execute `COMMAND`; reads 0 |
| `0x28` | `STATUS` | RO/W1C | 0 | bit 0 `ERROR` (write 1 to clear) |
| `0x3C` | `ID` | RO | `0x47504831` | `GPH1` |

### 8.1 Target surface

Commands draw into a target surface inside the aperture:

- By default the target is the framebuffer's `BACK` surface with the current `WIDTH`, `HEIGHT` and
  `STRIDE`.
- `SET_SURFACE` selects an explicit surface: an offset, a width, a height and a stride in bytes.
- The device keeps a clip rectangle, initialised to the whole target and reset by `SET_SURFACE`.
- Rectangles are half-open: `(x, y, w, h)` covers `x..x+w-1` and `y..y+h-1`.

### 8.2 Operations

| Op | Name | Arguments | Effect |
|---|---|---|---|
| 1 | `SET_CLIP` | x, y, w, h | intersect the clip with the target |
| 2 | — | — | reserved; kr32's `SET_COLOR` slot, unused because colours are explicit |
| 3 | `FILL_RECT` | x, y, w, h, colour | fill a rectangle |
| 4 | `COPY_RECT` | source offset, x, y, w, h | copy within the target |
| 5 | `DRAW_LINE` | x0, y0, x1, y1, colour | Bresenham, both endpoints included |
| 6 | `DRAW_TEXT` | x, y, pointer, length, fg, bg | 10x20 text from guest memory; `fg` and `bg` are XRGB colours |
| 7 | `FLIP` | — | the framebuffer `FLIP` of §5.3 |
| 8 | `SET_SURFACE` | offset, width, height, stride | select the target |
| 9 | `COPY_RECT_STRIDED` | source offset, source stride, x, y, w, h | copy from another surface |
| 10–`0xFFFFFFFF` | — | — | reserved; `ERROR` is set and nothing is drawn |

### 8.3 Validation

- Rectangles are clamped to the target and the clip, never rejected: a drawing call with a rectangle
  partly off-screen draws the part that fits and succeeds.
- `SET_SURFACE` is rejected (error, no change) when the width is 0 or above 1920, the height is 0 or
  above 1080, the stride is not a multiple of 4 or below `width*4` or above 7680, the offset is not
  a multiple of 4, or `offset + height*stride` leaves the 32 MiB aperture.
- `DRAW_TEXT` is rejected when the source range `[pointer, pointer+length)` leaves RAM (the device
  region is not readable as text). Length is clamped to 4096 bytes.
- An unknown opcode sets `ERROR` and draws nothing.
- `ERROR` is sticky until written as 1; a successful command leaves it alone.

`DRAW_TEXT` is the only command that reads guest memory, and the host is the trust boundary: it
validates the range before reading. There is no DMA in this draft.

## 9. System control — page 10 (`0xFFF0A000`)

| Offset | Name | Access | Reset | Meaning |
|---|---|---|---|---|
| `0x00` | `HALT` | WO | — | write any value to halt with status 0 |
| `0x04` | `RESET` | WO | — | write any value to reset the machine |
| `0x08` | `CYCLE_LO` | RO | 0 | free-running cycle counter, low 32 bits |
| `0x0C` | `CYCLE_HI` | RO | 0 | free-running cycle counter, high 32 bits |
| `0x3C` | `ID` | RO | `0x53595331` | `SYS1` |

`CYCLE_LO` returns the same low word `RDCYCLE` writes to R2. `HALT` is the same stop as the
instruction of the same name; `SYS exit` (number 0) is the way to stop with a status.

## 10. Reserved pages

Pages 1 (block), 3 (interrupt router), 5 (serial tty), 7 (pointer) and 11–239 have no device: any
access raises an address-error trap. They are reserved so that the map does not move when those
devices land, and the `ID` read at `0x3C` traps too, which distinguishes "not implemented" from
"implemented and broken".

## 11. Guest packages (provisional)

The display is only usable through a thin library. These are the packages the plan will ship; they
are a *package* contract, not ISA, and the names are the kr32 ones with a `kr232/` prefix.

### 11.1 Two modes, one API

Devices are kernel-only (ISA §2.3): with K = 0 every register and every VRAM byte is out of reach.
That splits the implementation of each package, not its API:

| | Kernel mode (K = 1) | User mode (K = 0) |
|---|---|---|
| Device access | writes the registers directly | `SYSCALL` to the kernel |
| `fb_back()` / `fb_front()` | real VRAM pointers | not available |
| VRAM allocation | the package's bump allocator | the kernel's allocator, offset returned |
| Drawing | one command to page 9 | one command block through `SYSCALL` |

Nothing above the package sees the difference: `gfx_fill_rect` is one call in both modes, and the
arguments are the same.

### 11.2 `<kr232/fb.h>` — the device (kernel only)

```c
typedef struct {
    unsigned width;
    unsigned height;
    unsigned stride_bytes;
    unsigned format;
    unsigned frame;
} fb_info;

int fb_probe(void);                 /* 0 when "FBR1" is at page 4 */
int fb_mode(int width, int height); /* set mode and stride; 0 or -EINVAL */
int fb_info_get(fb_info *out);
unsigned *fb_back(void);            /* pixels of the drawable surface */
unsigned *fb_front(void);           /* pixels of the displayed surface */
int fb_present(void);               /* FLIP; 0 or -EIO */
```

`fb.h` exists for the kernel and for kernel-mode test programs. A user program cannot dereference
`fb_back()`: the pointer names VRAM, which its mode cannot read, and the kernel will not hand one
out. User programs use `gfx.h` and `cons.h`, both of which become `SYSCALL` wrappers (§11.6).

### 11.3 `<kr232/gfx.h>` — drawing

```c
typedef struct {
    unsigned offset;    /* VRAM offset of row 0; opaque in user mode */
    int width;
    int height;
    int stride;         /* pixels per row */
    int clip_x, clip_y, clip_w, clip_h;
} gfx_surface;

gfx_surface *gfx_create(int width, int height);
void gfx_destroy(gfx_surface *surface);
int  gfx_present(gfx_surface *surface, int x, int y, int w, int h);

void gfx_clear(gfx_surface *s, unsigned color);
void gfx_pixel(gfx_surface *s, int x, int y, unsigned color);
void gfx_hline(gfx_surface *s, int x, int y, int w, unsigned color);
void gfx_vline(gfx_surface *s, int x, int y, int h, unsigned color);
void gfx_fill_rect(gfx_surface *s, int x, int y, int w, int h, unsigned color);
void gfx_frame_rect(gfx_surface *s, int x, int y, int w, int h, unsigned color);
void gfx_line(gfx_surface *s, int x0, int y0, int x1, int y1, unsigned color);
void gfx_blit(gfx_surface *dst, int dx, int dy, gfx_surface *src,
              int sx, int sy, int w, int h);
void gfx_set_clip(gfx_surface *s, int x, int y, int w, int h);
void gfx_reset_clip(gfx_surface *s);

int  gfx_font_width(void);          /* 10 */
int  gfx_font_height(void);         /* 20 */
int  gfx_text(gfx_surface *s, int x, int y, const char *text,
              unsigned fg, unsigned bg);
```

- Every drawing call submits one command to page 9 and the package keeps the device's target
  surface in step with the `gfx_surface` it was handed: a call on a surface the device is not
  currently targeting is preceded by `SET_SURFACE`. The guest never loops over pixels.
- `gfx_create` allocates inside the 32 MiB aperture with a bump allocator and never reuses in v0.1;
  `gfx_destroy` is a no-op, documented rather than hidden. A surface is a handle plus an offset;
  the pixels stay in VRAM. In user mode the allocator is the kernel's and the offset comes back
  from a `SYSCALL`.
- `gfx_present` copies an off-screen surface to the screen with one `COPY_RECT_STRIDED` and then
  issues a `FLIP`; presenting the framebuffer surface itself is a flip alone. The `x, y, w, h`
  rectangle is a hint this device ignores.
- `gfx_text` draws with the host's atlas, so console text and graphics text match. In user mode the
  string is copied to the kernel first, because the kernel is the only thing that can read the
  device's `DRAW_TEXT` source.

### 11.4 `<kr232/cons.h>` — the console

```c
void con_putc(char c);                       /* terminal DATA */
void con_puts(const char *text);
void con_write(const char *text, int count);
void con_clear(void);                        /* COMMAND 1 */
void con_set_colors(int fg_index, int bg_index); /* COLOR */
void con_set_cursor(int visible);            /* CONTROL bit 0 */
void con_hide(int hidden);                   /* CONTROL bit 1 */
void con_present(void);                      /* FLIP */
```

`con_putc` handles `\n`, `\r`, `\b`, `\t` and `\f` in the host (§7.2); the guest does not translate
anything. `con_present` is a flip only. In user mode each call is one `SYSCALL`; a `con_write` of
many characters is one call, not one per byte.

### 11.5 `<kr232/input.h>` — the keyboard

```c
typedef struct {
    unsigned char ascii;
    unsigned char modifiers;
    unsigned char scancode;
    unsigned char type;
} kr232_event;

int input_poll(kr232_event *event);   /* 1 when an event was popped, 0 when empty */
int input_pending(void);              /* 1 when at least one event waits */
```

The struct is the packed event word byte for byte, so a guest can copy a word and cast.

### 11.6 User-mode protocol (provisional)

A user program cannot touch page 9, the terminal page, the framebuffer control page or VRAM, so a
display call becomes a `SYSCALL`. The shape:

- The call number goes in R7 and the arguments in R2–R5, matching `SYS` (ISA §13).
- A drawing call passes a pointer to a command block in user RAM: `{op, arg0..arg7}`, the same
  fields the device takes in §8.2. The kernel copies the block, checks every argument, and writes
  page 9 itself.
- The kernel validates user pointers against user RAM (`0x08000000`–`0x0FFFFFFF`) before reading
  them, and validates rectangles and surface offsets exactly as §8.3 says. A bad pointer is an
  error return, not a trap in the kernel.
- `gfx_create` becomes an allocation call: the kernel owns the VRAM bump allocator and returns an
  offset the program cannot dereference, only pass back.
- There is no `fb_back()` in user mode. A program that wants pixels back asks for a
  kernel-mediated read, which v0.1 does not provide.

The exact numbers, the error returns and whether the command block is copied or read in place are
`kr232-plan.md`'s to fix; this section only says that the boundary is a syscall and that the kernel
validates everything.

## 12. Reset state

| Device | Reset |
|---|---|
| Timer | stopped, reload 0, count 0, no pending status |
| Console | queues empty, no overrun |
| Framebuffer | disabled, 800x600, stride 3200, format 1, front 0, back `0x001E0000`, frame counter 0, no error, VRAM cleared |
| Keyboard | FIFO empty, no overrun |
| Terminal | grid cleared, cursor at home and visible, 80x30, colour 7 |
| Graphics | command 0, arguments 0, clip full target, no error |
| System | nothing to reset; the cycle counter starts at 0 |

"VRAM cleared" is 32 MiB of zero. An emulator MAY back the aperture sparsely and return zero for
untouched pages; the architectural result is the same.

## 13. Changes from kr32

| Area | kr32 | kr232 | Why |
|---|---|---|---|
| Default mode | 1920x1080, stride 7680 | 800x600, stride 3200 | 80x30 terminal cells, and two surfaces in 4 MiB |
| `ID` location | per device (framebuffer `0x28`, terminal `0x14`, graphics `0x2C`) | `0x3C` on every page | one probe rule for every device |
| Console page | host debug console, data/status only | `OUT`/`IN`/`STATUS` with an empty sentinel of `0xFFFFFFFF` | one unambiguous polled transport |
| `SET_COLOR` command | op 2 sets the text pen | op 2 reserved; `DRAW_TEXT` takes XRGB colours | the gfx API passes a colour per call |
| Empty FIFO reads | 0, distinguished by status | `0xFFFFFFFF` sentinel | 0 is a valid byte and a valid event |
| Interrupts | per-device IRQ line registers, router | reserved registers, nothing raises | this draft has no interrupt delivery |
| Serial, block, pointer | implemented on pages 5, 1, 7 | reserved, trapping | not needed yet; pages kept so the map does not move |
| User/kernel split | drawing reaches the device through `SYS` calls | kernel-mode package writes registers; user mode uses `SYSCALL` | devices are kernel-only (ISA §2.3) |
| Paging | devices decode only when paging is on | always decoded | no paging in this draft |
| System control | halt/shutdown syscalls | page 10 registers plus `SYS exit` | kr232 already had MMIO halt/reset |
| Palette | defined in the kernel palette table | device contract, 16 VGA colours | screenshots are comparable across hosts |
| Text drawing | `DRAW_TEXT` reads a VRAM staging buffer | `DRAW_TEXT` reads guest RAM, range-checked; user text is copied by the kernel | the kernel, not the device, owns the user-memory read |
| Privilege | every program builds page tables and sets `MCTL` | one `K` bit; devices are kernel-only | two range checks instead of paging |

The parts kept identical on purpose: pixel format and byte order, the `FLIP` bundle and its
validation order, `FRONT`/`BACK` swapping, the `STATUS` bit meanings, the 10x20 cell, the terminal
register set, and the command numbering in §8.2 with only `SET_COLOR` retired. Drawing text takes
explicit XRGB colours rather than the terminal's palette indices, because a gfx program is not
bound by the console's palette.

## 14. Open questions

1. **Escape sequences.** v0.1 has none. Is ANSI SGR the next step, and does `COLOR` then become a
   per-cell pen with a colour stack?
2. **Command transport.** Immediate registers plus a doorbell are one command in flight. Is a FIFO
   in VRAM worth it before a program draws thousands of primitives per frame?
3. **Font.** 10x20 gives 80x30 and readable text; 8x16 gives 100x37 and more lines. One atlas, a
   mode bit, or a guest-supplied font?
4. **Dirty rectangles.** Presenting the whole frame is simple; a partial upload needs the device to
   own rectangle bookkeeping. Worth it for animation?
5. **Pointer device.** Page 7 is reserved. Mouse or tablet, and does it share the event FIFO with
   the keyboard?
6. **VRAM allocation.** The bump allocator is package-level and leaks by design. Does allocation
   become a device service (a `GFX_ALLOC` command) or stay a library concern?
7. **Multiple surfaces on screen.** One committed full-screen surface per flip; a compositor is out
   of scope. Is that acceptable for the whole 1.x line?
8. **Console and terminal together.** The console page is the debug stream and the terminal is the
   screen. Should `con_*` also mirror to the console page the way kr32 mirrors to serial?
9. **Timer tick.** One tick per cycle is simple but makes periods microarchitectural. Should the
   timer instead count a fixed instruction count or a host-time-derived rate?
10. **Framebuffer without a host.** A headless emulator still has to answer `ID` and `MODE_OK`. Is
    the framebuffer optional, or must a headless build fake it?
11. **Syscall ABI for the display.** The kernel now owns VRAM allocation and command submission. The
    numbers, the command-block layout, the error convention, and whether the block is copied or read
    in place are all still open, and so is how a program reads pixels back.
12. **Direct user access to VRAM.** Paging would allow a surface to be mapped into a user address
    space, which makes `fb_back()` meaningful in user mode and removes a syscall per primitive. Is
    that worth the MMU, or does the command model stay for good?
