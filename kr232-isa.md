# kr232 — Instruction Set Architecture

| | |
|---|---|
| Status | **Frozen 1.0** — the 32-bit baseline, frozen at M3's gate on 2026-11-23. Every deviation from kr216 1.0 is listed in §17. It is frozen as **ISA 1.0**; a change now costs a §15 decision, a §17 row and a 2.0 version |
| Date | 2026-11-22T19:10:04-08:00 |
| Basis | `..\kr216\kr216-isa.md` version 1.0 (frozen 2026-11-22), widened to 32-bit registers and a 4 GiB address space; the device region's shape follows `..\kr32\docs\display.md`, `devices.md`, `terminal.md` and `graphics.md` |
| Authority | `kr232-plan.md` 0.17 owns milestone scope, order and gates; this document owns the machine. §18 lists the two questions deferred to 2.0 |
| Approved against | `kr232-devices.md` 0.4 and `kr232-plan.md` 0.17, 2026-11-22 |
| Applies to | the planned `kr232`, `kr232-dis`, the krcompiler assembler/encoder and the kr232 image loader. `kr232-devices.md` owns the device register contracts; no implementation exists yet, so no conformance suite pins anything |
| Open items | §15 records the decisions this draft makes, §17 every change from kr216, §18 every question left to planning, Appendix C the map in one table |

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

**Relationship to kr216.** This document is kr216 1.0 with the data path and the address space
doubled: sixteen 32-bit general registers, a 4 GiB byte-addressable space, 32-bit stack slots,
32-bit return addresses and a 32-bit trap frame. The class skeleton, the opcode numbering, the
mnemonics, the flag and condition model, the frame instructions and the `SYS` interface are kept
wherever a 16-bit encoding generalises to 32 bits without ambiguity. Where a field could not
widen inside its class, a new escape prefix was added rather than stretching an existing class
into an opcode-dependent length. §17 is the authoritative diff; it marks each change **F**
(forced by the widening), **C** (chosen in this draft) or **O** (deliberately left open).

**Devices are not inherited from kr216.** kr216 has a 128-byte memory-mapped I/O block; kr232 needs
a display, so §2.3 and §14 give the top 64 MiB of the space to devices and follow kr32's shape: a
32 MiB VRAM aperture at `0xFC000000`, a 960 KiB window at `0xFFF00000` with one device per 4 KiB
page, and a text console rendered by the host. `kr232-devices.md` carries the register contracts
and the `fb`/`gfx`/`cons`/`input` guest packages that sit on them.

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-11-22 | First draft: kr216 1.0 widened to 32-bit registers and a 4 GiB address space; deviations catalogued in §17. |
| 0.2 | 2026-11-22 | Device region: 32 MiB framebuffer aperture at `0xFC000000`, a 960 KiB page-per-device window at `0xFFF00000`, and the console, framebuffer, keyboard, terminal, graphics and system pages. Reset SP moves to the top of RAM. Device contracts split into `kr232-devices.md`. |
| 0.3 | 2026-11-22 | Protection: `K` in R11, 256 MiB of RAM split 128 MiB kernel / 128 MiB user, a two-range protection fault (vector 8), `SYSCALL` at `0x79` (vector 9), and a 16-byte trap frame that switches to the kernel stack. Devices become kernel-only. |
| 0.4 | 2026-11-22 | Approved as the implementation basis for M0–M3, against `kr232-devices.md` 0.4 and `kr232-plan.md` 0.17; the Authority row names the plan. No encoding changed. |

## 1. Notation

- Addresses, registers and memory are 32 bits wide. A **word** is four bytes, a **halfword**
  two bytes, and a **byte** one byte.
- Multi-byte fields are little-endian: the low byte is at the lower address.
- Bit 31 is the most significant bit of a 32-bit value; bit 0 the least.
- `d8`, `d16`, `d32`, `imm8`, `imm16`, `imm32`, `disp9`, `disp16`, `disp32` are two's-complement
  values unless named `u8`, `u16` or `u32`.
- `Ra`, `Rb`, `Rd`, `Rs` are general-register numbers 0–15; `Fd`, `Fs` are 0–3.
- `mask16` is a 16-bit register mask; bit *n* selects R*n*. It is *not* widened: the machine
  still has sixteen general registers.
- `^`, `&`, `|` are XOR, AND, OR; `<<` and `>>` are shifts; `sext8` sign-extends a byte,
  `sext16` a halfword, to 32 bits.
- `PC` is the address of the instruction being executed unless stated otherwise; `[x]` is the
  memory operand at address `x`.

## 2. Machine model

### 2.1 Registers

| Register | Role |
|---|---|
| R0 | constant 0; reads as 0, writes are discarded |
| R1 | constant 1; reads as 1, writes are discarded |
| R2–R5 | general; ABI argument/return registers |
| R6, R7 | general; scratch under the ABI |
| R8, R9 | general; callee-saved under the ABI |
| R10 | general; C++ `this` under the ABI |
| R11 | flags |
| R12 | exception frame pointer (`EFP`) |
| R13 | frame pointer (`FP`) |
| R14 | program counter (`PC`) |
| R15 | stack pointer (`SP`), full-descending, 4-byte aligned under the ABI |

All sixteen registers are 32 bits wide. Writing R0 or R1 discards the value; reading either
yields the constant. R11–R15 are readable and writable as ordinary 32-bit registers, except
where an instruction says otherwise.

Writing R14 (PC) with any instruction other than a jump, a call, a return, a trap or `IRET`
is discarded: the instruction stream continues with the next instruction. That keeps PC usable
as a source for PC-relative materialisation — `MOV R2, PC` — without turning arithmetic into
control flow. R12, R13 and R15 take ordinary writes.

F0–F3 are 64-bit binary64 registers, unchanged by the widening. They are not addressable by any
address mode; only the `F`-prefixed instructions read and write them.

### 2.2 Flags (R11)

| Bit | Name | Meaning |
|---|---|---|
| 0 | Z | result was zero |
| 1 | N | result bit 31 |
| 2 | C | carry out, or no-borrow |
| 3 | V | signed overflow; `FCMP` sets it when the operands are unordered |
| 4 | I | interrupts enabled |
| 5 | T | single-step trace |
| 6 | K | kernel mode; 1 at reset, set by trap entry, cleared only by `IRET` |
| 7–31 | — | reserved, read as 0, writes ignored |

K is the protection bit. K = 1 runs in kernel mode and may reach the whole address space;
K = 0 runs in user mode and may reach only user RAM (§2.3). Hardware sets K when a trap or
`SYSCALL` enters the kernel and clears it when `IRET` restores a user frame (§11.3, §11.4). An
explicit write to R11 — `LD`, `MOV`, an ALU result, `POP #mask` with bit 11 — updates Z, N, C, V
and T only: K and I are never changed that way, so no sequence of ordinary instructions can enter
kernel mode or mask an interrupt the kernel left enabled.

A condition is tested as on ARM:

| # | Cond | Test | # | Cond | Test |
|---|---|---|---|---|---|
| 0 | EQ | Z=1 | 8 | HI | C=1 and Z=0 |
| 1 | NE | Z=0 | 9 | LS | C=0 or Z=1 |
| 2 | CS | C=1 | 10 | GE | N=V |
| 3 | CC | C=0 | 11 | LT | N≠V |
| 4 | MI | N=1 | 12 | GT | Z=0 and N=V |
| 5 | PL | N=0 | 13 | LE | Z=1 or N≠V |
| 6 | VS | V=1 | 14 | — | `CALL rel9` |
| 7 | VC | V=0 | 15 | — | `BRA rel9` |

T is the single-step flag: while it is set, the machine raises a breakpoint trap (vector 1)
after each instruction completes, with the saved PC pointing at the instruction that would run
next (section 11.3). The flag exists for guest code and for a trap-flag debugger.

### 2.3 Memory

4 GiB, byte-addressable, little-endian, with 32-bit addresses. All address arithmetic wraps
modulo 2^32; a multi-byte access that runs past `0xFFFFFFFF` continues at `0x00000000`. Byte,
halfword, word and 8-byte FP accesses are architectural; unaligned accesses execute correctly. The ISA
promises no atomicity or ordering beyond instruction boundaries; multi-hart ordering is out of
scope for 1.x (§15.44). Instructions may cross any byte boundary and there is no
instruction-alignment requirement.

RAM is 256 MiB, split in two halves: the bottom 128 MiB is kernel space and the top 128 MiB is
user space. Above RAM the space is unbacked until the device region, and the top 64 MiB belongs to
devices.

| Range | Size | Region | User mode (K = 0) |
|---|---|---|---|
| `0x00000000`–`0x000000FF` | 256 B | null page, inside kernel space | protection fault |
| `0x00000100`–`0x07FFFFFF` | 128 MiB − 256 B | kernel RAM | protection fault |
| `0x08000000`–`0x0FFFFFFF` | 128 MiB | user RAM | read, write, execute |
| `0x10000000`–`0xFBFFFFFF` | 3776 MiB | unbacked gap | address error |
| `0xFC000000`–`0xFDFFFFFF` | 32 MiB | framebuffer VRAM aperture (§14.2) | protection fault |
| `0xFE000000`–`0xFFEFFFFF` | 31 MiB | unbacked gap | address error |
| `0xFFF00000`–`0xFFFEFFFF` | 960 KiB | device window (§14.1) | protection fault |
| `0xFFFF0000`–`0xFFFFFFFF` | 64 KiB | reserved | protection fault |

The reset word and the sixteen trap vectors live at `0x00000100`–`0x0000013F`, inside kernel RAM.
The kernel image, its stacks and the kernel heap live in kernel RAM; user programs are linked at
`0x08000000` and run from user RAM.

**Protection is two range checks, not translation.** With K = 0, an instruction fetch, load or
store whose address is below `0x08000000` or at or above `0xFC000000` raises a protection fault
(vector 8) before any address decode happens. With K = 1 the whole space is reachable, subject to
the traps below. There is no page table and no virtual address: an address is the address.

Three limits follow, and they are deliberate:

- **User RAM is shared.** Every user program runs in `0x08000000`–`0x0FFFFFFF`, so one program can
  read another's memory. The kernel is protected from user code; user code is not protected from
  user code. Closing that needs paging (§16).
- **Devices are kernel-only.** The framebuffer, the terminal, the graphics command page and every
  other register are out of reach in user mode, so a user program reaches them only through
  `SYSCALL` and the kernel's ABI (`kr232-devices.md` §11).
- **The split is fixed.** A profile that backs more RAM gets more user space above `0x08000000`, not
  more kernel space; a profile MUST back kernel RAM at least up to the reset SP.

The machine also traps the null page (`0x00000000`–`0x000000FF`), both unbacked ranges, and
undefined device offsets. In user mode the protection check fires first for the null page and the
device region, so those traps are what a kernel-mode bad address produces. The emulator MAY
populate RAM lazily, so a mostly empty 256 MiB costs nothing.

### 2.4 Reset

```
PC   = word at 0x00000100
SP   = 0x08000000
FP   = 0
EFP  = 0
R11  = 0x00000040        ; K = 1, kernel mode
R2-R10 and F0-F3: undefined
```

R0 and R1 read as their constants and need no initialisation. The reset SP is the top of kernel RAM
and therefore the top of the kernel stack: the first push lands at `0x07FFFFFC`. K = 1, so the
machine boots in kernel mode; the kernel drops to user mode by building a frame with K = 0 and
executing `IRET` (§11.4). Devices wake in their documented reset state (`kr232-devices.md` §12),
including the framebuffer's default mode and cleared VRAM.

## 3. Encoding framework

### 3.1 First-byte length map

The first byte selects the length class. The decoder resolves the length from the first byte
before fetching the rest, except inside the escape class where the prefix byte and, for `F0`
and `F4`, the bytes that follow declare the length.

| First byte | Class | Length | Section |
|---|---|---|---|
| `0x00`–`0x4F` | Short | 1 | 4.1 |
| `0x50`–`0x5F` | Register: `LD Rd,#imm32` | 5 | 5 |
| `0x60`–`0x6F` | Register: `LD Rd,[addr32]` | 5 | 5 |
| `0x70`–`0x7F` | Short (system) | 1 | 4.2 |
| `0x80`–`0x9F` | Compact, register form | 2 | 6.1 |
| `0xA0`–`0xBF` | Compact, branch form | 2 | 6.2 |
| `0xC0`–`0xDF` | Normal | 3 | 7 |
| `0xE0`–`0xEF` | Register: `ST [addr32],Rd` | 5 | 5 |
| `0xF0` | Escape: generic two-operand | 4–8 | 8.1 |
| `0xF1` | Escape: long integer | 6 | 8.2 |
| `0xF2` | Escape: reserved for 64-bit integers | — | 8.10 |
| `0xF3` | Escape: FP register | 2 | 8.3 |
| `0xF4` | Escape: FP load/store, 8-bit displacement | 3–4 | 8.4 |
| `0xF5` | Escape: FP conversion | 3 | 8.5 |
| `0xF6` | Escape: FP push/pop | 2 | 8.6 |
| `0xF7` | Escape: `ENTER`, 8-bit frame | 4 | 8.7 |
| `0xF8` | Escape: `ENTER`, 16-bit frame | 5 | 8.7 |
| `0xF9` | Escape: wide integer and absolute control | 6 | 8.8 |
| `0xFA` | Escape: FP load/store, 32-bit displacement | 7 | 8.9 |
| `0xFB` | Escape: `ENTER`, 32-bit frame | 7 | 8.7 |
| `0xFC`–`0xFF` | Escape: reserved | — | 8.10 |

### 3.2 Reserved fields and encodings

A field labelled **reserved** MUST be zero. A reserved first byte, a reserved opcode, or a
non-zero reserved field is an **illegal instruction** and raises trap vector 2. This makes every
unallocated encoding a trap, which is both the forward-compatibility rule and the decoder
fuzzing target.

Instruction length is a property of the first byte alone. No class has an opcode-dependent
length: that is why the widened address and immediate forms were given new escape prefixes
(`F9`, `FA`, `FB`) instead of lengthening `C0`–`DF`.

## 4. Short class

### 4.1 Register rows (`0 OOO RRRR`)

| Opcode | Instruction | Encoding |
|---|---|---|
| `0x00`–`0x0F` | `PUSH Rd` | short form of `PUSH #(1 << Rd)` |
| `0x10`–`0x1F` | `POP Rd` | short form of `POP #(1 << Rd)` |
| `0x20`–`0x2F` | `INC Rd` | `ADD Rd, #1` |
| `0x30`–`0x3F` | `DEC Rd` | `SUB Rd, #1` |
| `0x40`–`0x4F` | `CALL Rd` | indirect call; return address pushed |
| `0x50`–`0x6F` | — | Register class (section 5) |

`PUSH R14`, `PUSH R15`, `POP R14` and `POP R15` are illegal instructions: the short form is a
single-bit mask, and mask bits 14 and 15 are reserved. `PUSH R0` and `PUSH R1` push the
constants 0 and 1.

### 4.2 System encodings (`0 111 SSSS`)

| Byte | Instruction | Effect |
|---|---|---|
| `0x70` | `RET` | pop the return address into PC |
| `0x71` | `NOP` | nothing |
| `0x72` | `BRK` | breakpoint trap, vector 1 |
| `0x73` | `SYS` | host hypercall, section 13 |
| `0x74` | `EI` | set I |
| `0x75` | `DI` | clear I |
| `0x76` | `HALT` | stop the machine |
| `0x77` | `IRET` | return from a trap, section 11.4 |
| `0x78` | `RDCYCLE` | R2 = low 32 bits of the cycle counter |
| `0x79` | `SYSCALL` | trap to the kernel, vector 9; the saved PC is the next instruction |
| `0x7A`–`0x7F` | reserved | trap |

`SYS` is the kernel's hypercall into the emulator and raises a protection fault in user mode.
`SYSCALL` is the way user mode enters the kernel; the instruction itself does nothing except take
the trap, and the kernel decides what the call number in R7 means.

## 5. Register class

Five bytes: the opcode byte followed by a 32-bit payload.

| First byte | Instruction | Encoding |
|---|---|---|
| `0101 RRRR` (`0x50`–`0x5F`) | `LD Rd, #imm32` | `imm32` (LE) |
| `0110 RRRR` (`0x60`–`0x6F`) | `LD Rd, [addr32]` | `addr32` (LE) |
| `1110 RRRR` (`0xE0`–`0xEF`) | `ST [addr32], Rd` | `addr32` (LE) |

`LD Rd, #imm32` loads the 32-bit value into Rd; for `LD Rd, [addr32]` the last four bytes are the
address; for `ST [addr32], Rd` they are the address and Rd is the data. All sixteen registers
are legal in every row.

`LD Rd, #s8` and `LD Rd, #u8` in the Normal class are now the *short* forms of a constant load
(three bytes against five), so the assembler emits them whenever the constant fits in eight bits
and falls back to the Register form otherwise. The five-byte form is the only way to materialise
an arbitrary 32-bit constant in one instruction; the generic `F0` form reaches `imm16` in six
bytes, which is neither shorter nor wider, so the assembler never uses it for a constant load.

## 6. Compact class

### 6.1 Register form (`100 OOOOO Ra Rb`)

First byte `0x80` + op; second byte `Ra << 4 | Rb`.

| Op | Instruction | Op | Instruction | Op | Instruction | Op | Instruction |
|---|---|---|---|---|---|---|---|
| 0 | `MOV Ra, Rb` | 8 | `CMP Ra, Rb` | 16 | `LDBU Ra, [Rb]` | 24 | `LD Ra, -[Rb]` |
| 1 | `ADD Ra, Rb` | 9 | `MUL Ra, Rb` | 17 | `LDBS Ra, [Rb]` | 25 | `ST -[Rb], Ra` |
| 2 | `ADDC Ra, Rb` | 10 | `MULH Ra, Rb` | 18 | `STB [Rb], Ra` | 26 | `LDBU Ra, -[Rb]` |
| 3 | `SUB Ra, Rb` | 11 | `SHL Ra, Rb` | 19 | `LD Ra, [Rb]+` | 27 | `LDBS Ra, -[Rb]` |
| 4 | `SUBB Ra, Rb` | 12 | `SHR Ra, Rb` | 20 | `ST [Rb]+, Ra` | 28 | `STB -[Rb], Ra` |
| 5 | `AND Ra, Rb` | 13 | `SAR Ra, Rb` | 21 | `LDBU Ra, [Rb]+` | 29 | `NEG Ra` |
| 6 | `OR Ra, Rb` | 14 | `LD Ra, [Rb]` | 22 | `LDBS Ra, [Rb]+` | 30 | `NOT Ra` |
| 7 | `XOR Ra, Rb` | 15 | `ST [Rb], Ra` | 23 | `STB [Rb]+, Ra` | 31 | `JMP Ra` |

**Register roles.** `Ra` is the data register: the destination for a load or an ALU result, the
stored value for a store, the operand for a unary operation, and the target for `JMP`. `Rb` is
the second register: the source for `MOV` and the ALU operations, the base for the load/store
forms. Unary operations (`NEG`, `NOT`) and `JMP` require `Rb = R0`; any other value is an illegal
instruction.

**Auto-increment and auto-decrement.** `[Rb]+` loads from Rb and then adds the operand size to
Rb; `[Rb]+, Ra` in a store stores at Rb and then adds the operand size. `-[Rb]` subtracts the
operand size from Rb and then accesses. The step is 1 for byte forms and 4 for word forms. When
the data register and the base are the same register, the base is read before the data register
is written, so `LD R2, [R2]+` loads from the original R2 and leaves R2 + 4.

### 6.2 Branch form (`101 CCCC ddddddddd`)

First byte `0xA0 | (cond << 1) | ((disp9 >> 8) & 1)`; second byte `disp9 & 0xFF`. `disp9` is
signed and is relative to the address of the **next** instruction, range −256…+255. The range is
unchanged from kr216: a nine-bit displacement is as useful in a 4 GiB space as it was in 64 KiB,
because it is relative.

| Cond | Instruction |
|---|---|
| 0–13 | `Bcc` with that condition |
| 14 | `CALL rel9`: push PC+2, then branch |
| 15 | `BRA rel9`: unconditional branch |

## 7. Normal class (`110 OOOOO`)

Three bytes: opcode, then two payload bytes whose layout is per opcode. `RRRR` is the data
register, `BBBB` the base register, `CCCC` a condition. The class is uniformly three bytes; the
`DIV`/`DIVS` payload carries a reserved zero third byte for that reason.

| Op | Instruction | Payload | Flags |
|---|---|---|---|
| 0 | `LD Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |
| 1 | `ST [Rb+d8], Rs` | `RRRR BBBB`, `d8` | none |
| 2 | `LDBU Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |
| 3 | `LDBS Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |
| 4 | `STB [Rb+d8], Rs` | `RRRR BBBB`, `d8` | none |
| 5 | `LD Rd, #s8` | `RRRR 0000`, signed `imm8` | none |
| 6 | `LD Rd, #u8` | `RRRR 0000`, unsigned `imm8` | none |
| 7 | `ADD Rd, #s8` | `RRRR 0000`, signed `imm8` | Z N C V |
| 8 | `SUB Rd, #s8` | `RRRR 0000`, signed `imm8` | Z N C V |
| 9 | `CMP Rd, #s8` | `RRRR 0000`, signed `imm8` | Z N C V |
| 10 | `CMP Rd, #u8` | `RRRR 0000`, unsigned `imm8` | Z N C V |
| 11 | `AND Rd, #u8` | `RRRR 0000`, `imm8` | Z N; C=0 V=0 |
| 12 | `OR Rd, #u8` | `RRRR 0000`, `imm8` | Z N; C=0 V=0 |
| 13 | `XOR Rd, #u8` | `RRRR 0000`, `imm8` | Z N; C=0 V=0 |
| 14 | `LEA Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |
| 15 | `JMP rel16` | `rel16` (LE) | none |
| 16 | `CALL rel16` | `rel16` (LE) | none |
| 17 | `LDBU Rd, [PC+d8]` | `RRRR 0000`, `d8` | none |
| 18 | `LDBS Rd, [PC+d8]` | `RRRR 0000`, `d8` | none |
| 19 | `SETcc Rd` | `RRRR CCCC`, `0x00` | none |
| 20 | `SHL Rd, #n` | `RRRR 0000`, `imm8` | Z N C; V=0 |
| 21 | `SHR Rd, #n` | `RRRR 0000`, `imm8` | Z N C; V=0 |
| 22 | `SAR Rd, #n` | `RRRR 0000`, `imm8` | Z N C; V=0 |
| 23 | `PUSH #mask16` | `mask16` (LE) | none |
| 24 | `POP #mask16` | `mask16` (LE) | none |
| 25 | `DIV Rd, Rb` | `RRRR BBBB`, `0x00` | Z N; C=0 V=0 |
| 26 | `DIVS Rd, Rb` | `RRRR BBBB`, `0x00` | Z N; C=0 V=0 |
| 27 | `SXTB Rd` | `RRRR 0000`, `0x00` | Z N; C=0 V=0 |
| 28 | `LEAVE #mask16` | `mask16` (LE) | none |
| 29 | `LDHU Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |
| 30 | `STH [Rb+d8], Rs` | `RRRR BBBB`, `d8` | none |
| 31 | `LDHS Rd, [Rb+d8]` | `RRRR BBBB`, `d8` | none |

- `d8` is signed; for the `[Rb+d8]` and `[PC+d8]` loads it is added to the base (or to the
  instruction address for PC) modulo 2^32.
- `JMP rel16` and `CALL rel16` are relative to the address of the **next** instruction, range
  −32768…+32767, and replace kr216's absolute `addr16` forms. Absolute targets use `F9`.
- `SETcc` accepts conditions 0–13. Conditions 14 and 15 select `CALL rel9` and `BRA rel9` in the
  Compact class and are illegal here.
- `SHL`/`SHR`/`SAR` immediate counts are 0–31. The assembler rejects an immediate of 32–255; the
  decoder treats such an encoding as count 0, so a hand-written byte stream still has defined
  behaviour.
- `DIV` is unsigned division, `DIVS` signed division truncated toward zero. The quotient is
  written to Rd; no remainder is produced (§15, item 11).
- Ops 29–31 are new in kr232: kr216 reserved them. They are the halfword forms the 16-bit
  `LD`/`ST` used to provide implicitly.

## 8. Escape class (`1111 SSSS`)

### 8.1 `F0` — generic two-operand form

```
F0 | op | spec1 | spec2 | [extension]       4-8 bytes
```

The operation byte selects one of the operations in the table below; the two specifier bytes
select the operands; at most one operand may need an extension.

**Operation byte.**

| Byte | Operation | Operand 1 (modes) | Operand 2 (modes) |
|---|---|---|---|
| `0x00` | `MOV` | register (0) | register or immediate (0, 1) |
| `0x01` | `ADD` | register (0) | register or immediate (0, 1) |
| `0x02` | `ADDC` | register (0) | register or immediate (0, 1) |
| `0x03` | `SUB` | register (0) | register or immediate (0, 1) |
| `0x04` | `SUBB` | register (0) | register or immediate (0, 1) |
| `0x05` | `AND` | register (0) | register or immediate (0, 1) |
| `0x06` | `OR` | register (0) | register or immediate (0, 1) |
| `0x07` | `XOR` | register (0) | register or immediate (0, 1) |
| `0x08` | `CMP` | register (0) | register or immediate (0, 1) |
| `0x09` | `MUL` | register (0) | register (0) |
| `0x0A` | `MULH` | register (0) | register (0) |
| `0x0B` | `SHL` | register (0) | register or immediate (0, 1) |
| `0x0C` | `SHR` | register (0) | register or immediate (0, 1) |
| `0x0D` | `SAR` | register (0) | register or immediate (0, 1) |
| `0x0E` | `LD` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x0F` | `ST` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x10` | `LDBU` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x11` | `LDBS` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x12` | `STB` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x13` | `LEA` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x14` | `NEG` | register (0) | unused (0) |
| `0x15` | `NOT` | register (0) | unused (0) |
| `0x16` | `SXTB` | register (0) | unused (0) |
| `0x17` | `DIV` | register (0) | register (0) |
| `0x18` | `DIVS` | register (0) | register (0) |
| `0x19` | `JMP` | target (0, 1, 2, 3, 4, 5, 6, 7) | unused (0) |
| `0x1A` | `CALL` | target (0, 1, 2, 3, 4, 5, 6, 7) | unused (0) |
| `0x1B` | `SXTH` | register (0) | unused (0) |
| `0x1C` | `LDHU` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x1D` | `LDHS` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x1E` | `STH` | register (0) | address (2, 3, 4, 5, 6, 7) |
| `0x1F`–`0xFF` | reserved | — | trap |

Operand order is the data-register order the other classes use: operand 1 is the data or
destination register, operand 2 is the source, base or target. A store names its address first in
assembly — `ST [Rb+d8], Rs` — but the encoder writes Rs into specifier 1 and the address into
specifier 2. `unused` operands are encoded as mode 0, register 0 and ignored.

**Specifier byte.** Each specifier is `0 MMM RRRR`: bit 7 reserved, bits 6–4 the mode, bits 3–0
the register. For mode 1 (immediate) the register field is reserved and must be zero.

**Modes.** Mode numbers are those of section 9: 0 register direct, 1 immediate, 2 `[Rb]`,
3 `[Rb]+`, 4 `-[Rb]`, 5 `[Rb+d8]`, 6 `[Rb+d16]`, 7 `[Rb+d32]`.

**Extensions.** They follow the two specifier bytes, in operand order: mode 1 takes an `imm16`
(two bytes, LE) sign-extended to 32 bits; mode 5 takes a `d8` (one byte, sign-extended); mode 6
takes a `d16` (two bytes, sign-extended); mode 7 takes a `d32` (four bytes). An encoding in which
both operands need an extension is illegal, which keeps the form within four to eight bytes.
Mode 1 is therefore the six-byte way to name a 16-bit constant; `F9` ops `0xA`–`0xF` are the
six-byte way to name a 32-bit one.

**Absolute addressing.** There is no separate absolute mode in the generic form: an absolute
address is `[R0 + d32]` (mode 7 with base 0), because R0 reads as zero. The dedicated five-byte
`LD Rd,[addr32]` and six-byte `F9` forms are shorter and are what the assembler emits.

### 8.2 `F1` — long integer

```
F1 | OOOO RRRR | addr32       6 bytes
```

| Op | Instruction |
|---|---|
| 0 | `LDBU Rd, [addr32]` |
| 1 | `LDBS Rd, [addr32]` |
| 2 | `STB [addr32], Rd` |
| 3 | `LEA Rd, [PC+d32]` |
| 4 | `LEA Rd, [SP+d32]` |
| 5 | `LDHU Rd, [addr32]` |
| 6 | `STH [addr32], Rd` |
| 7 | `LDHS Rd, [addr32]` |
| 8–15 | reserved; trap |

For ops 0–2 and 5–7, `RRRR` is the data register. For ops 3–4, the 32-bit field is a signed
displacement added to the instruction address (op 3) or to the current SP (op 4), modulo 2^32.

### 8.3 `F3` — FP register-register

```
F3 | OOOO Fd Fs               2 bytes
```

| Op | Instruction | Op | Instruction |
|---|---|---|---|
| 0 | `FADD Fd, Fs` | 5 | `FABS Fd` |
| 1 | `FSUB Fd, Fs` | 6 | `FSQRT Fd` |
| 2 | `FMUL Fd, Fs` | 7 | `FMOV Fd, Fs` |
| 3 | `FDIV Fd, Fs` | 8 | `FCMP Fd, Fs` |
| 4 | `FNEG Fd` | 9 | `FRND Fd` |
| | | 10–15 | reserved; trap |

`FADD`/`FSUB`/`FMUL`/`FDIV`/`FMOV` are binary: `Fd` is both destination and left operand, `Fs`
the right operand. `FNEG`, `FABS`, `FSQRT` and `FRND` are unary: `Fs` is reserved and must be
zero.

### 8.4 `F4` — FP load/store, 8-bit displacement

```
F4 | OOOO Fd MM | BBBB 0000 | [d8]       3-4 bytes
```

`OOOO`: 0 `FLD` (8-byte), 1 `FST` (8-byte), 2 `FLDS` (4-byte load and widen), 3 `FSTS` (4-byte
store and narrow); 4–15 reserved. `MM`: 00 `[Rb]`, 01 `[Rb+d8]`, 10 `[Rb]+`, 11 `-[Rb]`. The `d8`
byte is present only when `MM = 01`. `BBBB` is the base register; the low nibble of that byte is
reserved and must be zero.

`[Rb]+` adds 8 for `FLD`/`FST` and 4 for `FLDS`/`FSTS`; `-[Rb]` subtracts the same amount before
the access. The FP sizes are unchanged from kr216, so this class needed no widening at all.

### 8.5 `F5` — FP conversion

```
F5 | OOOO Fd 00 | RRRR 0000    3 bytes
```

| Op | Instruction | Meaning |
|---|---|---|
| 0 | `I2F Fd, Rb` | signed 32-bit to binary64 |
| 1 | `U2F Fd, Rb` | unsigned 32-bit to binary64 |
| 2 | `F2I Rb, Fd` | binary64 to signed 32-bit, truncating toward zero |
| 3 | `F2U Rb, Fd` | binary64 to unsigned 32-bit, truncating toward zero |
| 4–15 | reserved | trap |

A conversion of NaN or of a value outside the destination range raises an illegal-operand trap
(vector 3). `F2I` of −2^31 is in range; −2^31 − 0.5 truncates to −2^31. The low two bits of the
first payload byte are reserved and must be zero.

### 8.6 `F6` — FP push/pop

```
F6 | OOOOOO Fd                 2 bytes
```

| Op | Instruction | Effect |
|---|---|---|
| 0 | `FPUSH Fd` | SP -= 8; store 8 bytes of Fd at SP |
| 1 | `FPOP Fd` | load 8 bytes at SP into Fd; SP += 8 |
| 2–63 | reserved | trap |

### 8.7 `F7`/`F8`/`FB` — `ENTER`

```
F7 | frame8 | mask16           4 bytes
F8 | frame16 | mask16          5 bytes
FB | frame32 | mask16          7 bytes
```

The frame size is unsigned, MUST be a multiple of 4, and the assembler rejects any other value.
Mask bits 13, 14 and 15 must be zero. `FB` is new in kr232: with a 32-bit SP, frames larger than
64 KiB are reachable and the 16-bit frame of `F8` is not the architectural maximum. See section
10.7 for the operation.

### 8.8 `F9` — wide integer and absolute control

```
F9 | OOOO RRRR | payload32     6 bytes
```

New in kr232. It carries everything that needs a 32-bit field and a fixed length: the absolute
control transfers that kr216 encoded in the Normal class, the PC- and SP-relative forms wider
than `imm16`, and the long-immediate ALU operations.

| Op | Instruction | `RRRR` | payload32 |
|---|---|---|---|
| 0 | `JMP addr32` | reserved 0 | target address |
| 1 | `CALL addr32` | reserved 0 | target address |
| 2 | `JMP [addr32]` | reserved 0 | address of the target word |
| 3 | `CALL [addr32]` | reserved 0 | address of the target word |
| 4 | `LEA Rd, [PC+d32]` | Rd | signed displacement |
| 5 | `LEA Rd, [SP+d32]` | Rd | signed displacement |
| 6 | `LD Rd, [PC+d32]` | Rd | signed displacement |
| 7 | `LDBU Rd, [PC+d32]` | Rd | signed displacement |
| 8 | `LDBS Rd, [PC+d32]` | Rd | signed displacement |
| 9 | `LDHU Rd, [PC+d32]` | Rd | signed displacement |
| `0xA` | `ADD Rd, #imm32` | Rd | immediate |
| `0xB` | `SUB Rd, #imm32` | Rd | immediate |
| `0xC` | `CMP Rd, #imm32` | Rd | immediate |
| `0xD` | `AND Rd, #imm32` | Rd | immediate |
| `0xE` | `OR Rd, #imm32` | Rd | immediate |
| `0xF` | `XOR Rd, #imm32` | Rd | immediate |

Ops 0–3 branch to, or push and branch to, the payload word (ops 2–3 first load a word from it).
Ops 4 and 6–9 use the payload as a signed displacement relative to the instruction address; op 5
uses it relative to SP; ops `0xA`–`0xF` use it as the 32-bit operand. `CALL` here is the same
operation as `CALL rel16`: push the address of the next instruction and branch. Ops 0–3 MUST have
`RRRR = 0`.

### 8.9 `FA` — FP load/store, 32-bit displacement

```
FA | OOOO Fd MM | BBBB 0000 | d32     7 bytes
```

New in kr232; the wide companion of `F4`. `OOOO` is the same four operations as `F4`; `MM`: 00
`[Rb+d32]` with `BBBB` the base register, 01 `[PC+d32]` with `BBBB` reserved 0; 10 and 11
reserved. The displacement is signed and relative to the instruction address for the PC form.
`d32` is always present, which keeps the length fixed.

### 8.10 `F2` and `FC`–`FF`

`F2` is reserved for 64- and 128-bit integer operations. `FC`–`FF` are reserved. All of them are
illegal instructions and trap with vector 2.

## 9. Addressing modes

| Mode | Name | Syntax | Extension | Available in |
|---|---|---|---|---|
| 0 | Register direct | `Rd` | — | generic operand 1/2, ALU forms |
| 1 | Immediate | `#imm8`, `#imm16`, `#imm32` | `imm8`, `imm16` or `imm32` | generic operand 2, Register/Normal/`F9` forms |
| 2 | Register indirect | `[Rb]` | — | 2-byte loads/stores, generic, FP |
| 3 | Auto-increment | `[Rb]+` | — | 2-byte loads/stores, generic, FP |
| 4 | Auto-decrement | `-[Rb]` | — | 2-byte loads/stores, generic, FP |
| 5 | 8-bit displacement | `[Rb+d8]` | `d8` | Normal, generic, FP |
| 6 | 16-bit displacement | `[Rb+d16]` | `d16` | generic only |
| 7 | 32-bit displacement | `[Rb+d32]` | `d32` | generic only |

Every mode accepts all sixteen registers as base and as data; there is no mode that works only
for R0–R7. Auto-increment and auto-decrement step by the operand size: 1 for byte accesses, 2
for halfword, 4 for word, 8 for `FLD`/`FST`, 4 for `FLDS`/`FSTS`.

Mode 7 was reserved in kr216 and is `[Rb+d32]` here. That leaves the indexed form `[Rb+Ri]`
without a mode number; 1.x does not add one (§15.53).

Two PC-relative rules, deliberately different:

- A **branch** displacement is relative to the address of the next instruction.
- A **data** displacement (`LD Rd,[PC+d8]` from Normal op 17/18, `F1` op 3, `F9` ops 4 and 6–9,
  and the `FA` PC form) is relative to the address of the instruction that contains it. `LEA
  Rd,[SP+d32]` (`F1` op 4, `F9` op 5) is relative to the current SP instead.

## 10. Instruction semantics

Flags not named in a row are unchanged. Setting flags means writing Z, N, C and V as stated; an
instruction that "sets Z N" writes 1 or 0 to both. N is bit 31 of the result, C is the carry out
of bit 31 or the no-borrow bit, and V is signed overflow of the 32-bit result.

### 10.1 Data movement

| Instruction | Operation | Flags |
|---|---|---|
| `MOV Rd, Rb` | Rd = Rb | none |
| `MOV Rd, #imm` | Rd = imm32 | none |
| `CLR Rd` | alias: `MOV Rd, R0` | none |
| `LD Rd, [addr]` | Rd = word at address | none |
| `ST [addr], Rd` | word at address = Rd | none |
| `LDHU Rd, [addr]` | Rd = zero-extended halfword | none |
| `LDHS Rd, [addr]` | Rd = sign-extended halfword | none |
| `STH [addr], Rd` | halfword at address = low halfword of Rd | none |
| `LDBU Rd, [addr]` | Rd = zero-extended byte | none |
| `LDBS Rd, [addr]` | Rd = sign-extended byte | none |
| `STB [addr], Rd` | byte at address = low byte of Rd | none |
| `LEA Rd, [addr]` | Rd = effective address | none |
| `SXTB Rd` | Rd = sext8(low byte of Rd) | Z N; C=0 V=0 |
| `SXTH Rd` | Rd = sext16(low halfword of Rd) | Z N; C=0 V=0 |
| `PUSH #mask` / `POP #mask` | see 10.6 | none |

### 10.2 Integer arithmetic and logic

| Instruction | Operation | Flags |
|---|---|---|
| `ADD Rd, x` | Rd = Rd + x | Z N C V |
| `ADDC Rd, x` | Rd = Rd + x + C | Z N C V |
| `SUB Rd, x` | Rd = Rd − x | Z N C V |
| `SUBB Rd, x` | Rd = Rd − x − C | Z N C V |
| `CMP Rd, x` | flags from Rd − x; no write | Z N C V |
| `INC Rd` | `ADD Rd, #1` | Z N C V |
| `DEC Rd` | `SUB Rd, #1` | Z N C V |
| `NEG Rd` | Rd = 0 − Rd | Z N C V |
| `AND Rd, x` | Rd = Rd & x | Z N; C=0 V=0 |
| `OR Rd, x` | Rd = Rd \| x | Z N; C=0 V=0 |
| `XOR Rd, x` | Rd = Rd ^ x | Z N; C=0 V=0 |
| `NOT Rd` | Rd = ~Rd | Z N; C=0 V=0 |
| `MUL Rd, Rb` | Rd = low word of Rd × Rb | Z N from the low word; C=0 V=0 |
| `MULH Rd, Rb` | Rd = high word of the **signed** product | Z N from the high word; C=0 V=0 |
| `DIV Rd, Rb` | Rd = unsigned Rd ÷ Rb | Z N; C=0 V=0 |
| `DIVS Rd, Rb` | Rd = signed Rd ÷ Rb, truncating | Z N; C=0 V=0 |

Carry is the carry out for `ADD`/`ADDC`/`INC` and the no-borrow bit for `SUB`/`SUBB`/`DEC`/
`NEG`/`CMP`; V is signed overflow. `MUL` and `MULH` are the same multiplication, differing only
in which word they leave.

`MULH` is signed. An unsigned 32×32→64 product's high word is
`MULH(a, b) + (a < 0 ? b : 0) + (b < 0 ? a : 0)`, because a value with bit 31 set is negative
when read as signed: adding the other operand once restores its 2^32 contribution. The rule and
the correction sequence are unchanged from kr216, with 2^32 in place of 2^16.

`DIV`/`DIVS` by zero raises an illegal-operand trap (vector 3). `DIVS` of −2^31 by −1 wraps to
−2^31 and does not trap.

### 10.3 Shifts

| Instruction | Operation | Flags |
|---|---|---|
| `SHL Rd, n` | Rd = Rd << n | Z N; C = last bit shifted out; V=0 |
| `SHR Rd, n` | Rd = Rd >> n, zero fill | Z N; C = last bit shifted out; V=0 |
| `SAR Rd, n` | Rd = Rd >>> n, sign fill | Z N; C = last bit shifted out; V=0 |

`n` is 0–31, from an immediate (Normal class) or a register (Compact class); a register count has
its low five bits used, so a count of 32 behaves as 0. With `n = 0` the result is unchanged and C
is unchanged. "Last bit shifted out" is bit `32 − n` of the original value for `SHL` and bit
`n − 1` for `SHR`/`SAR`.

### 10.4 Compare and set

`SETcc Rd` writes 1 when the condition holds and 0 otherwise; it reads flags and writes none.

### 10.5 Branches and calls

| Instruction | Operation |
|---|---|
| `Bcc rel9` | if condition is true, PC += d (d relative to next instruction) |
| `BRA rel9` | PC += d |
| `CALL rel9` | push PC+2, then PC += d |
| `JMP rel16` / `CALL rel16` | PC += d, or push PC+3 then PC += d; d relative to next instruction |
| `JMP addr32` / `CALL addr32` | PC = target; `CALL` pushes the return address first |
| `JMP [addr32]` / `CALL [addr32]` | PC = word at target; `CALL` pushes the return address first |
| `JMP Rd` / `CALL Rd` | PC = Rd; `CALL` pushes the return address first |
| `RET` | PC = the word popped from the stack |

All branch displacements and the pushes are modulo 2^32. `CALL` decrements SP by 4 and stores the
return address at the new SP. `RET` loads the word at SP into PC and adds 4 to SP. Neither
touches flags.

An indirect `JMP` or `CALL` may name any register, including R0 and R1; the assembler emits the
one-byte `CALL Rd` form and the two-byte `JMP Rd` form for register targets, `F9` ops 0–3 for
absolute targets, and `rel16` when the target is in range.

### 10.6 Mask push and pop

`PUSH #mask` stores one word for each set bit, in ascending register order: R0 first, R15 last,
so the highest-numbered register ends at the lowest address. Each set bit decrements SP by 4
before storing. `POP #mask` reads in descending order, R15 first and R0 last, which exactly
reverses a `PUSH` of the same mask; each set bit loads then increments SP.

Bits 14 (PC) and 15 (SP) MUST be zero; a mask with either set is an illegal instruction. Bits 0
and 1 are legal and move the constants 0 and 1. Bit 11 moves the flags word, and a `POP` that
restores it updates Z, N, C, V and T only (§2.2): a user-mode `POP` can neither enter kernel mode
nor enable interrupts. The instruction is atomic with respect to interrupts: delivery happens only
between instructions.

### 10.7 Frames: `ENTER` and `LEAVE`

```
ENTER #frame, #mask
    SP -= 4; [SP] = FP          ; save the caller's frame pointer
    FP = SP                     ; FP points at the saved FP slot
    PUSH #mask                  ; ascending order, as in 10.6
    SP -= frame                 ; locals and outgoing arguments

LEAVE #mask
    SP = FP - 4 * popcount(mask)
    POP #mask                   ; descending order
    FP = [SP]; SP += 4          ; SP now points at the return address
```

`LEAVE` does not pop the return address. A normal epilogue is `LEAVE #mask; RET`; a tail call is
`LEAVE #mask; JMP target`. Computing SP from FP means a frame extended at run time (`alloca`)
still unwinds. Mask bits 13, 14 and 15 MUST be zero, and the frame size MUST be a multiple of 4.
Neither instruction touches flags or exception state.

`ENTER` is atomic with respect to interrupts: no delivery occurs inside it.

### 10.8 System instructions

| Instruction | Effect | Mode |
|---|---|---|
| `NOP` | none | any |
| `EI` | I = 1 | kernel |
| `DI` | I = 0 | kernel |
| `HALT` | stop execution; the machine is halted | kernel |
| `BRK` | breakpoint trap, vector 1 | any |
| `SYSCALL` | trap to the kernel, vector 9 | any |
| `SYS` | host hypercall, section 13; not a trap | kernel |
| `RDCYCLE` | R2 = low 32 bits of the free-running cycle counter; flags unchanged | any |
| `IRET` | return from a trap, section 11.4 | kernel |

A kernel-only instruction executed with K = 0 raises a protection fault (vector 8) and does nothing.
Everything else — the integer, FP, memory, branch and frame instructions — is legal in both modes,
subject to the protection check on the addresses it touches.

### 10.9 Floating point

All FP arithmetic is binary64 with round-to-nearest-even, no traps and no status register:
overflow produces infinity, an invalid operation a quiet NaN, division by zero infinity. NaN
payloads and the sign of zero are not specified beyond IEEE-754.

| Instruction | Operation | Flags |
|---|---|---|
| `FADD Fd, Fs` | Fd = Fd + Fs | none |
| `FSUB Fd, Fs` | Fd = Fd − Fs | none |
| `FMUL Fd, Fs` | Fd = Fd × Fs | none |
| `FDIV Fd, Fs` | Fd = Fd ÷ Fs | none |
| `FNEG Fd` | sign bit inverted | none |
| `FABS Fd` | sign bit cleared | none |
| `FSQRT Fd` | correctly rounded square root | none |
| `FMOV Fd, Fs` | Fd = Fs | none |
| `FCMP Fd, Fs` | N = Fd < Fs; Z = Fd == Fs; V = unordered; C = 0 | Z N C V |
| `FRND Fd` | round to the nearest binary32 value, widened back | none |
| `FLD Fd, addr` | load 8 bytes | none |
| `FST Fd, addr` | store 8 bytes | none |
| `FLDS Fd, addr` | load 4 bytes, binary32 to binary64 | none |
| `FSTS Fd, addr` | store 4 bytes, binary64 to binary32 | none |
| `FPUSH Fd` / `FPOP Fd` | 8-byte stack move | none |
| `I2F`, `U2F`, `F2I`, `F2U` | 32-bit conversions; out of range traps | none |

`FRND` makes strict binary32 arithmetic possible on a binary64 datapath: an `f32` operation is
computed in binary64 and the result is rounded to binary32 precision before it is used. `FLDS`
widens a binary32 value exactly; `FSTS` narrows with round-to-nearest-even.

## 11. Traps

### 11.1 Causes

| Vector | Name | Cause |
|---|---|---|
| 0 | reset | not a trap: the reset vector, `PC = [0x00000100]` |
| 1 | breakpoint | `BRK`, or the single-step trap when T is set |
| 2 | illegal instruction | reserved or unallocated encoding, non-zero reserved field |
| 3 | illegal operand | divide by zero, invalid float↔integer conversion, unknown `SYS` number |
| 4 | address error | kernel-mode access in the null page, an access to an unbacked range, an undefined device register, or a mis-shaped device access |
| 5 | external interrupt | reserved; no source in this draft |
| 6 | timer interrupt | reserved; no source in this draft |
| 7 | console interrupt | reserved; no source in this draft |
| 8 | protection | user-mode fetch/load/store below `0x08000000` or at or above `0xFC000000`, or a kernel-only instruction executed in user mode |
| 9 | system call | `SYSCALL` |
| 10–15 | reserved | no source |

### 11.2 Priority

At most one trap is raised per instruction. The checks run in the order illegal instruction,
illegal operand, protection, address error, and the first one that fires is the trap taken; a fetch
of an illegal encoding is therefore reported before any operand or address check.

### 11.3 Entry

```
savedSP = SP
if K = 0: SP = 0x08000000        ; enter on the kernel stack, top of kernel RAM
SP -= 16
[SP+0]  = EFP                    ; previous exception frame pointer
[SP+4]  = faulting PC            ; address of the instruction that trapped
[SP+8]  = flags                  ; R11, with K and I as they were
[SP+12] = savedSP                ; SP of the interrupted context
EFP = SP
K = 1
I = 0
PC = word at 0x00000100 + 4 * vector
```

A trap taken with K = 0 lands on the kernel stack, never on the user's stack: the processor ignores
the user SP for the frame and records it at `[SP+12]` so `IRET` can restore it. A trap taken with
K = 1 continues on the current kernel stack, which is what makes nested traps work.

Because entry from user mode resets SP to `0x08000000`, the kernel MUST have unwound its own frames
before `IRET`ing to a user program: a frame it leaves on the stack sits exactly where the next user
trap pushes its own.

The saved PC is the address of the faulting instruction, not the next one, so a handler that wants
to resume past a `BRK` must add 1 to `[EFP+4]` before `IRET`. Two entries are the exception, because
both are complete instructions rather than faults: the trace trap and `SYSCALL` save the address of
the next instruction, so `IRET` resumes after them.

If the frame push itself faults — a kernel-mode trap with an invalid SP — the machine halts: there
is no double-fault path.

A trap handler that uses F0–F3 must save them itself; the hardware frame does not.

### 11.4 `IRET`

```
f    = [EFP+8]
pc   = [EFP+4]
efp  = [EFP+0]
sp   = [EFP+12]
PC = pc; EFP = efp; SP = sp; flags = f
```

`IRET` is kernel mode only; executed with K = 0 it raises a protection fault. It restores every
architecturally defined flag from the saved word — including K, I and T — so it is the only
instruction that can leave kernel mode. The kernel enters a user program by building a frame with
K = 0 and that program's SP and PC in it; the next `IRET` lands in user mode.

## 12. Boot

The machine starts at the reset state of section 2.4 and executes from the address in the word at
`0x00000100`. Vector *n* lives at `0x00000100 + 4n`, so the sixteen vectors occupy
`0x00000100`–`0x0000013F` and vector 0 *is* the reset word. The image writer places the reset word
there; the linker's default layout puts the kernel's `crt0` at `0x00000200`, so the reset word is
normally `0x00000200`, exactly as in kr216. The kernel stack top is `0x08000000`, the top of kernel
RAM.

User programs are a separate image: linked at `0x08000000`, with a stack top the kernel chooses
(normally `0x10000000`, the top of user RAM), and entered by the kernel with `IRET` and K = 0. The
machine has no opinion about how the kernel loads one; §14 and `kr232-devices.md` only define the
addresses it may use.

## 13. `SYS` host calls

`SYS` (0x73) enters the emulator directly. It is a virtual-machine hypercall: it does not use the
vector table, does not push an exception frame, and does not change flags. It is kernel mode only;
with K = 0 it raises a protection fault. This is the kernel's door to the host — `exit`, `write`,
`read`, `time` — not the user program's; user code calls `SYSCALL` and the kernel decides.

| Register | Use |
|---|---|
| R7 | call number, and not preserved |
| R2 | argument 0 / result |
| R3 | argument 1 |
| R4 | argument 2 |
| R5 | argument 3 |

| Call | Number | Arguments | Result |
|---|---|---|---|
| `exit` | 0 | R2 = status, the **whole 32-bit word** | never returns; the machine halts with that status, and an emulator that reports it as a process status passes it through unmodified |
| `write` | 1 | R2 = fd, R3 = address, R4 = length | R2 = bytes written, or a negative error |
| `read` | 2 | R2 = fd, R3 = address, R4 = length | R2 = bytes read, or a negative error; 0 is end of input |
| `time` | 3 | — | R2:R3 = 64-bit seconds since the epoch (low 32 bits, high 32 bits) |
| `debug_write` | 4 | R2 = address, R3 = length | R2 = 0 |

Descriptors name streams: **1** is the console output stream and **0** its input; any other descriptor answers -1. `write` copies guest bytes to the stream and reports the count, `read` fills guest memory from the input queue and reports 0 at end of input, `time` answers a 64-bit count of seconds split across R2:R3, and `debug_write` sends guest bytes to the host's debug stream without touching memory. A range that is not mapped, or a descriptor that is not named, is refused with -1 in R2 rather than trapping.

A status is not truncated by the machine: a program that exits with 300 exits with 300, and only the host operating system may narrow it when it reports a process status. Call numbers 5 and above are reserved and raise an illegal-operand trap (vector 3). A failed call
returns a negative value in R2; the individual codes are the emulator's host-service contract.
Addresses and lengths are 32-bit; a length is unsigned and the host MAY reject a range that would
wrap past `0xFFFFFFFF`.

## 14. Devices

Devices are memory-mapped in the 960 KiB window at `0xFFF00000`, one device per 4 KiB page. The
register contracts are in `kr232-devices.md`; this section fixes only what the ISA needs: where
the pages are, how an access behaves, and what the framebuffer aperture is.

The whole device region — window and aperture — is kernel-only. With K = 0 an access raises a
protection fault (vector 8) before any device decode, so user-mode code reaches a device only by
asking the kernel, and the kernel is the only thing that writes a register.

### 14.1 Device window

| Page | Base | Device |
|---|---|---|
| 0 | `0xFFF00000` | timer |
| 1 | `0xFFF01000` | reserved; a block device is planned |
| 2 | `0xFFF02000` | console: polled byte stream |
| 3 | `0xFFF03000` | reserved; an interrupt router is planned |
| 4 | `0xFFF04000` | framebuffer control |
| 5 | `0xFFF05000` | reserved; a serial tty is planned |
| 6 | `0xFFF06000` | keyboard |
| 7 | `0xFFF07000` | reserved; a pointer device is planned |
| 8 | `0xFFF08000` | terminal: host-rendered text console |
| 9 | `0xFFF09000` | graphics commands: host-rasterised drawing |
| 10 | `0xFFF0A000` | system control: halt, reset, cycle counter |
| 11 | `0xFFF0B000` | blitter: host-side copy and fill |
| 12–239 | `0xFFF0C000`–`0xFFFEFFFF` | reserved; any access traps |

Access rules:

- Only a 32-bit aligned load or store to an offset the device defines is a defined access. A byte
  or halfword access, an unaligned access, an offset the device does not define, or any access to
  a reserved page raises an address-error trap (vector 4).
- A value the device rejects — a mode that is too wide, a flip whose surface leaves the aperture —
  is *not* a trap. The write leaves the register unchanged and the device reports the failure
  through its own status bits.
- Every device defines an `ID` word at offset `0x3C`, so a probe is always one read at
  `base + 0x3C`: `FBR1` for the framebuffer, `TERM` for the terminal, `GPH1` for graphics
  commands, and so on in `kr232-devices.md`.
- Bits a register does not define read as 0 and ignore writes.

### 14.2 Framebuffer aperture

`0xFC000000`–`0xFDFFFFFF` is 32 MiB of VRAM behind the device bus. It behaves like memory: 8-,
16- and 32-bit accesses, aligned or not, and every offset in the window is valid storage.
Off-screen regions are scratch memory, not an error — pixels written outside the committed
surface are simply not displayed. The aperture is kernel-only (§2.3): a user-mode access raises a
protection fault, so pixels reach the screen through the kernel.

A pixel is one little-endian 32-bit word `0x00RRGGBB` (XRGB8888), so its bytes in memory are
`B, G, R, 0`. The top byte is ignored and the host presents opaque.

The control page (`kr232-devices.md` §5) selects the visible surface: `FRONT` and `BACK` are byte
offsets into the aperture and `STRIDE` is in bytes. Two 1920x1080 surfaces need 16.6 MiB, so double
buffering, off-screen composition and scratch surfaces all fit in the one 32 MiB window.

Polled I/O: no device raises an interrupt in 1.x (§15.37; §11.1, vectors 5–7).

## 15. Decisions recorded here

Each is normative from this revision; changing one is an ISA change. Items marked *(C)* are
choices this draft makes that the plan may reverse; the rest follow from §17.

1. **Vector numbering.** 1 breakpoint, 2 illegal instruction, 3 illegal operand, 4 address error,
   5–7 interrupt sources reserved, 8–15 unused. *(C: unchanged from kr216.)*
2. **Trap entry order and the saved PC.** The frame is `{EFP, PC, flags}`, one 32-bit word each,
   at ascending addresses; EFP points at it; `I` is cleared; the saved PC is the faulting
   instruction's address.
3. **`F0` opcode allocation** 0x00–0x1E and the specifier byte layout `0 MMM RRRR`.
4. **`F0` operand rules:** specifier 1 is the data/destination register and specifier 2 the
   source, base or target, matching `Ra`/`Rb` in the Compact class; at most one operand may carry
   an extension, so the form stays 4–8 bytes; absolute addressing is `[R0+d32]`; mode 6 stays
   `[Rb+d16]` sign-extended and mode 7 is the new `[Rb+d32]`.
5. **`F1` op order:** 3 `LEA Rd,[PC+d32]`, 4 `LEA Rd,[SP+d32]`, 5–7 the halfword forms. *(C.)*
6. **`F6` op numbers:** 0 `FPUSH`, 1 `FPOP`.
7. **Reserved fields must be zero** and trap otherwise, including `Fs` in unary `F3` ops, the `Rb`
   field of a unary Compact operation or `JMP`, the register field of an immediate specifier, the
   low nibble of the `F4`/`FA` base byte, and `RRRR` in `F9` ops 0–3.
8. **`MULH` is signed.** The unsigned high word is the correction sequence in 10.2.
9. **Flags for instructions the plan does not classify:** `INC`/`DEC` are `ADD`/`SUB` with 1;
   `NEG` is `0 − Rd`; `MUL`/`MULH`/`DIV`/`DIVS`/`SXTB`/`SXTH` set Z and N, clear C and V;
   `SETcc`, `LEA`, all branches, all frame/system instructions and all halfword loads/stores
   leave flags unchanged.
10. **Shift counts are 0–31**; the assembler rejects an immediate of 32–255, and a register count
    uses its low five bits; `n = 0` leaves C unchanged.
11. **`MUL`'s overflow is not trapped**, and `DIVS` of −2^31 by −1 wraps.
12. **`DIV`/`DIVS` produce the quotient only.** The backend synthesises `a − (a ÷ b) × b` for `%`.
13. **`SYS` numbering** 0–4 and the reserved-5+ trap; `time` returns a 64-bit value in R2:R3.
14. **Device window layout.** The 960 KiB window at `0xFFF00000` is 240 pages of 4 KiB with one
    device per page (§14.1); the framebuffer owns the 32 MiB aperture at `0xFC000000`; the top
    64 MiB of the address space is the device region (§2.3). *(C; the shape is kr32's.)*
15. **Undefined I/O addresses trap** rather than reading zero.
16. **`BRK`'s resumability rule:** the handler advances the saved PC by hand.
17. **Word size is four bytes.** Word loads, stores, stack slots, return addresses and trap frames
    are 4 bytes; halfword and byte accesses are distinct sizes with their own load/store forms.
18. **Absolute control transfer moves to `F9`**, and Normal ops 15/16 are `JMP rel16`/`CALL
    rel16`. A 16-bit absolute target is not representable in a 4 GiB space, and keeping the
    opcode numbers would have produced a jump that silently wrapped into the low 64 KiB. *(C.)*
19. **Halfword access is architectural:** `LDHU`, `LDHS`, `STH` at Normal 29–31 and `F0` 0x1C–0x1E,
    `SXTH` at `F0` 0x1B, and the `F1` absolute forms 5–7. *(C.)*
20. **`F9` ops 0xA–0xF are long-immediate ALU operations**, so a 32-bit constant can be combined
    with a register in one six-byte instruction. *(C.)*
21. **Every Normal-class instruction is three bytes**, which gives `DIV`/`DIVS` a reserved zero
    third byte. *(C; fixes the kr216 ambiguity noted in §17 item 15.)*
22. **The reset SP is `0x08000000`**, the top of kernel RAM, and reset leaves K = 1, so the first
    push lands at `0x07FFFFFC` and the machine boots in kernel mode.
23. **Vectors are four bytes apart** and the table occupies `0x00000100`–`0x0000013F`, sharing its
    first word with the reset vector.
24. **`ENTER` frame sizes are multiples of 4**, and `F7`/`F8`/`FB` select 8-, 16- and 32-bit frame
    fields. *(C.)*
25. **Every device page carries its `ID` at offset `0x3C`.** A probe is one read, and the four
    bytes are the device name. *(C; kr32 placed the ID at a different offset per device.)*
26. **Console, terminal and graphics are three devices.** The console is the polled byte stream,
    the terminal is the host-rendered text console, and the graphics-command page is the
    host-rasterised drawing fast path the gfx package uses. A program can use text, pixels, or
    both. *(C.)*
27. **The framebuffer is 32 MiB of VRAM with a front/back offset model**: XRGB8888, commit on
    `FLIP`, default 800x600 (80x30 terminal cells). Direct pixel writes and host-rendered drawing
    compose in the same back surface. *(C; kr32 defaults to 1920x1080.)*
28. **Device value errors are reported, not trapped.** Shape errors (byte access, undefined
    offset, reserved page) trap with vector 4; an out-of-range *value* leaves the register
    unchanged and sets a status bit, so mode-setting code needs no trap handler to be correct.
    *(C.)*
29. **`K` is bit 6 of R11** and resets to 1. Only trap entry sets it and only `IRET` clears it;
    an explicit write to R11 never changes K or I. *(C.)*
30. **RAM is 256 MiB, split 128/128.** Kernel space is `0x00000000`–`0x07FFFFFF` and user space is
    `0x08000000`–`0x0FFFFFFF`; the boundary is fixed even when a profile grows RAM. *(C.)*
31. **Protection is two range checks, not translation.** With K = 0 an instruction fetch, load or
    store below `0x08000000` or at or above `0xFC000000` raises a protection fault (vector 8)
    before any address decode. *(C.)*
32. **`SYSCALL` (`0x79`) is the user-mode gate**, vector 9, with the saved PC after the
    instruction. `SYS` stays the kernel's hypercall and traps in user mode. *(C.)*
33. **Trap entry switches to the top of kernel RAM.** Entering from user mode sets
    SP = `0x08000000`, the frame carries the interrupted SP, and the frame is therefore 16 bytes.
    The kernel stack is not relocatable in this draft. *(C.)*
34. **Kernel-only instructions** are `EI`, `DI`, `HALT`, `SYS` and `IRET`; each raises vector 8 in
    user mode. `SYSCALL`, `BRK`, `RDCYCLE` and the frame instructions are legal in both modes.
    *(C.)*
35. **No paging in 1.x, and the encoding for it stays reserved.** Protection is the two range checks
    of §2.3. Vector 10 and `F0` opcodes 0x1F–0x20 remain unallocated, so a later MMU can add a
    translation fault and a `RDCR`/`WRCR` control-register pair without renumbering anything; the
    rest of `F0` stays free. *(C.)*
36. **`SYSCALL` uses the `SYS` register layout.** R7 carries the call number, R2–R5 the arguments,
    R2 the result, and a failed call returns a negative value. The numbers and the error values are
    the kernel's ABI and belong to the plan. *(C.)*
37. **I/O is polled for all of 1.x.** No device asserts an interrupt; vectors 5–7 stay reserved and
    every per-device interrupt-enable register stays inert. *(C.)*
38. **Stack alignment is 4 bytes, and 8 is recommended.** 8-byte FP accesses are unaligned-legal
    (§2.3), so `FPUSH` on a 4-byte-aligned stack is defined; the ABI asks callers to keep SP 8-byte
    aligned for speed, not for correctness. *(C.)*
39. **The register file stays at sixteen registers.** No prefix byte, escape slot or wider field is
    reserved for more; an encoding that needs a fifth register bit is out of scope for 1.x. *(C.)*
40. **Every device in the map is mandatory.** A build that omits a populated page is non-conformant,
    and a headless emulator must still answer that page's `ID` and status reads rather than trap.
    *(C.)*
41. **The timer counts machine cycles** — the same free-running counter `RDCYCLE` and the system page
    expose. A period is a cycle budget, and the ISA still does not fix cycles per instruction. *(C.)*
42. **The default framebuffer mode is 800x600**, stride 3200, which gives the terminal 80x30 cells;
    a mode change resets the grid (§7.1 of `kr232-devices.md`). *(C.)*
43. **User mode has no pixel readback in 1.x.** A user program draws through `SYSCALL` command blocks
    and cannot read VRAM back; reading remains a kernel capability. *(C.)*
44. **kr232 is a uniprocessor.** No `LL`/`SC`, no fences and no memory-ordering model; a second hart
    is out of scope for 1.x. *(C.)*
45. **No kr216 compatibility.** There is no kr216 ISA mode and no image translator; kr216 programs are
    reassembled for kr232. *(C.)*
46. **The heap follows the image.** The kernel heap starts after the kernel image and the user heap
    after the user image, each described by its own linker script; the stack tops stay fixed at
    `0x08000000` and `0x10000000`. *(C.)*
47. **One whole-frame `FLIP` per present.** There is no dirty-rectangle upload channel and no command
    FIFO in 1.x; the rectangle argument of `gfx_present` stays a hint. *(C.)*
48. **The halfword forms stay**: `LDHU`/`LDHS`/`STH` at Normal 29–31, `F0` 0x1C–0x1E, `SXTH` at `F0`
    0x1B, and the `F1` absolute forms 5–7. *(C.)*
49. **`SYSCALL` numbers reuse the `SYS` meanings** for 0 `exit`, 1 `write` and 2 `read`; kernel-only
    calls start at 16. `time` (3) and `debug_write` (4) stay host calls of the kernel, and a user
    `SYSCALL` to them fails. *(C.)*
50. **The kernel copies a user command block before validating it.** It copies the block into a
    kernel buffer, checks every field, and only then writes the device registers; the graphics device
    never reads user memory. *(C.)*
51. **The calling convention is C-style.** R2–R5 carry the first four arguments, further arguments
    are pushed on the stack, a struct return uses a hidden pointer in R2, and R2 carries the result.
    *(C.)*
52. **The kernel owns the screen.** A user program draws only through syscalls; the kernel can take
    the screen back, kill the program and restore the console. *(C.)*
53. **1.x gains no ISA extensions.** The drafted instruction set is what 1.x implements: no bit
    operations, no 64-bit integer instructions, no `rel32` branch form and no indexed addressing;
    `F2` and `F0` 0x21 and above stay unallocated. *(C.)*
54. **Bulk movement is host work.** Page 11 is a blitter that copies or fills RAM or VRAM on the
    host, and page 9 gains `UPLOAD` for a strided copy from RAM into a surface. Guest code does not
    loop over bytes to move a buffer; small copies stay inline in the library. *(C.)*
55. **The terminal mirrors each accepted character to the console page.** Every byte the terminal
    takes is also written to page 2's `OUT`, so a host transcript of a run contains what the screen
    shows, in kernel and user mode alike; the device mirrors, so a caller cannot opt out. It is a
    development aid: a later revision separates the terminal (screen and framebuffer) from a
    COM-port tty, and the mirror retires with the split. *(C.)*
56. **The kernel owns VRAM allocation.** A program asks the kernel for a surface through `SYSCALL`;
    the kernel's bump allocator returns an offset, validates it on every later call, and can reclaim
    it. The graphics page keeps no allocator. *(C.)*
57. **The terminal has no escape sequences in 1.x.** It switches on control characters only and
    `COLOR` sets the pen; SGR is a later revision. *(C.)*
58. **The blitter is synchronous: one command at a time.** The `CONTROL` write completes the move;
    there is no queue, no busy state and no ordering rule. *(C.)*
59. **There is no pointer device in 1.x.** Page 7 stays reserved and traps; the keyboard is the only
    input device. A pointer arrives with the kernel that would use it. *(C.)*

## 16. Deferred

- **64- and 128-bit integer operations** — the `F2` escape. Until it exists, 64-bit arithmetic
  is an `ADDC`/`SUBB` pair chain on the 32-bit datapath.
- **Atomics** — 1.x is a uniprocessor with no `LL`/`SC`, no `AMO`, no fences and no ordering model
  (§15.44); a multi-hart machine is out of scope.
- **Paging, per-process isolation and virtual memory** — the protection model is two range checks:
  no page table, no translation, no separate address space per process. The kernel is protected
  from user code but user programs are not protected from each other. This is the largest gap for
  running more than one program at a time, and §15.35 keeps the encoding it will need.
- **More privilege structure and a relocatable trap stack** — `K` is one bit and the kernel stack
  top is the top of kernel RAM; per-hart stacks and a kernel-configurable boundary are later work.
- **Bit operations** (`BTST`/`BSET`/`BCLR`/`CLZ`/`CTZ`/`POPCNT`).
- **Blitter queues** — one command completes before the next starts; chaining and completion interrupts
  are later work.
- **Indexed addressing** `[Rb+Ri]` — mode 7 is now `[Rb+d32]`.
- **An unsigned high-word multiply (`MULHU`)** — the correction sequence in 10.2 is the portable
  way to get it.
- **Remainder instruction** — see §15 item 12.
- **Interrupt sources** — vectors 5–7 exist but nothing raises them, and no device can interrupt
  the CPU: the timer, keyboard and graphics pages are polled, and their interrupt-enable bits are
  reserved. 1.x is polled by decision (§15.37); delivery is a later milestone.
- **Far conditional branches** — `Bcc` reaches ±256 bytes and `rel16` covers ±32 KiB; anything
  farther needs a `JMP addr32` trampoline.
- **Device DMA** — no device reads or writes memory on its own. The block device on page 1 would
  need it, and its page is reserved until then.
- **Pointer, serial and block devices** — pages 7, 5 and 1 are reserved and trap; the populated
  pages are the timer, console, framebuffer, keyboard, terminal, graphics, system and blitter
  (§15.54, §15.59).
- **Partial presents** — the host sees the whole committed frame after a `FLIP`; there is no
  dirty-rectangle upload channel, and `gfx_present`'s rectangle is a hint the device ignores.
- **A guest-side text renderer** — the terminal and `DRAW_TEXT` render text in the host from its
  own 10x20 atlas, so the guest carries no font. A program that wants a different glyph set would
  have to rasterise its own text into VRAM.
- **Sound, multiple windows, alpha blending** — nothing in the device model reserves space for
  them; a second aperture could be added at the cost of the unbacked gap.

## 17. Changes from kr216

Type: **F** forced by the 32-bit widening, **C** chosen in this draft, **O** left open.

| # | Area | kr216 1.0 | kr232 0.2 | Type |
|---|---|---|---|---|
| 1 | Register width | 16 bits | 32 bits | F |
| 2 | Address space | 64 KiB, wrap mod 65536 | 4 GiB, wrap mod 2^32 | F |
| 3 | Sizes | word = 2 bytes | word = 4, halfword = 2, byte = 1 | F |
| 4 | Flags | N = bit 15; bits 6–15 reserved | N = bit 31; bits 6–31 reserved | F |
| 5 | Word loads/stores, stack slots, return addresses | 2 bytes | 4 bytes | F |
| 6 | `LD Rd,#imm16` | 3 bytes, class `0x50`–`0x5F` | 5 bytes, `#imm32` | F |
| 7 | `LD Rd,[addr16]`, `ST [addr16],Rd` | 3 bytes, `0x60`/`0xE0` | 5 bytes, `addr32` | F |
| 8 | `F1` | 4 bytes, `addr16`/`d16` | 6 bytes, `addr32`/`d32`; ops 5–7 added | F/C |
| 9 | `F0` | 4–6 bytes; mode 7 reserved | 4–8 bytes; mode 7 = `[Rb+d32]`; mode 6 stays `d16` | F/C |
| 10 | `JMP`/`CALL` absolute | Normal 15/16, `addr16` | `F9` ops 0–3, `addr32` | F |
| 11 | Normal 15/16 | `JMP addr16` / `CALL addr16` | `JMP rel16` / `CALL rel16` | C |
| 12 | New `F9` prefix | — | absolute and indirect control, PC/SP-relative `LEA`, PC-relative loads, long-immediate ALU | C |
| 13 | New `FA` prefix | — | FP load/store with `d32`, `[PC+d32]` form | C |
| 14 | New `FB` prefix | — | `ENTER` with a 32-bit frame | C |
| 15 | Normal 25/26 | `DIV`/`DIVS` shown as 2 bytes in Appendix A, contradicting §3.1's 3-byte class | uniformly 3 bytes, reserved zero third byte | C |
| 16 | Normal 29–31 | reserved | `LDHU`, `STH`, `LDHS` | C |
| 17 | Halfword ops | none | `LDHU`/`LDHS`/`STH`/`SXTH`; `F1` ops 5–7 | C |
| 18 | Constant-load preference | assembler emits the 3-byte Register form for `#u8`/`#s8` | assembler emits the 3-byte Normal form; Register form only beyond 8 bits | C |
| 19 | Compact word step | 2 | 4 (byte forms stay 1) | F |
| 20 | Compact branch | `rel9` | `rel9` kept; a relative displacement survives widening | C |
| 21 | Trap frame | 6 bytes `{EFP,PC,flags}` | 16 bytes `{EFP,PC,flags,SP}`, pushed on the kernel stack | F/C |
| 22 | Vector stride | 2 bytes | 4 bytes; table `0x0100`–`0x013F` | F |
| 23 | Reset SP | `0xFEF0` | `0x08000000`, top of kernel RAM, with K = 1 | C |
| 24 | Null page | `0x0000`–`0x00FF` | `0x00000000`–`0x000000FF` | F |
| 25 | Reserved and unbacked ranges | `0xFF80`–`0xFFFF` | 31 MiB unbacked gap below the device window, plus the top 64 KiB | C |
| 26 | Device window | `0xFF00`–`0xFF7F`, 16-bit registers at 2-byte offsets | 960 KiB at `0xFFF00000`, one device per 4 KiB page, 32-bit registers at 4-byte offsets | F/C |
| 27 | `SYS time` | 32-bit seconds in R2:R3 | 64-bit seconds in R2:R3 (each half 32-bit) | F |
| 28 | `ENTER` frame alignment | even | multiple of 4 | C |
| 29 | Shift counts | 0–15, low 4 bits of a register count | 0–31, low 5 bits | F |
| 30 | `MUL`/`MULH` | 16×16→32 | 32×32→64 | F |
| 31 | `DIVS` edge | −32768 ÷ −1 | −2^31 ÷ −1 | F |
| 32 | FP conversions | 16-bit integer domains | 32-bit integer domains | F |
| 33 | `RDCYCLE` | low word of the counter | low 32 bits of the counter | F |
| 34 | Appendix A | 60 vectors | re-derived: every 16-bit-field vector changed | F |
| 35 | Locked decisions | §15 of kr216 frozen at 1.0 | §15 here is a draft; nothing is frozen | O |
| 36 | Memory map | 64 KiB, RAM everywhere but the null page | 256 MiB of RAM split 128/128, devices in the top 64 MiB | C |
| 37 | Framebuffer | none | 32 MiB VRAM aperture at `0xFC000000` plus a control page: `FLIP`, `FRONT`/`BACK`, XRGB8888 | C |
| 38 | Terminal | the byte console is the only text path | host-rendered text console page 8: grid, palette, scrollback, cursor | C |
| 39 | Keyboard | none | event-FIFO page 6 (ASCII, modifiers, scancode, type) | C |
| 40 | Graphics commands | none | host-rasterised drawing page 9 with 9 operations | C |
| 41 | Console registers | `0xFF00`/`0xFF02`/`0xFF04`, 2-byte stride | page 2 at 4-byte stride; `IN` returns `0xFFFFFFFF` when empty | C |
| 42 | System registers | `0xFF20`–`0xFF26` | page 10: halt, reset, cycle lo/hi | C |
| 43 | Timer | 16-bit counter, compare, control | 32-bit reload/count/status with a periodic mode | C |
| 44 | Device probing | none | an `ID` word at `0x3C` on every device page | C |
| 45 | Guest package | none | `fb`, `gfx`, `cons` and `input` headers (`kr232-devices.md` §11) | C |
| 46 | Privilege | none | `K` is bit 6 of R11; resets to 1; trap entry sets it, `IRET` clears it | C |
| 47 | User-mode protection | none | fetch, load or store below `0x08000000` or at or above `0xFC000000` faults with vector 8 | C |
| 48 | Kernel/user split | none | 128 MiB kernel space below, 128 MiB user space above | C |
| 49 | User-to-kernel entry | none | `SYSCALL` at `0x79`, vector 9, saved PC after the instruction | C |
| 50 | Kernel-only instructions | none | `EI`, `DI`, `HALT`, `SYS` and `IRET` fault in user mode | C |
| 51 | Block move and fill | post-increment loops | blitter page 11 (`COPY`/`FILL`) and a graphics `UPLOAD` op | C |

The vectors that did *not* change are the ones whose fields are all 8-bit or smaller: the short
class, both Compact forms, the `d8`/`imm8` Normal forms, and `F3`–`F7`. That is why the port is
reviewable: a diff of Appendix A shows exactly which classes widened. Rows 36–45 change no
instruction encoding at all; they are address-map and device work. Rows 46–50 add one byte of
encoding — `SYSCALL` at `0x79` — and otherwise change the flag set, the trap frame and the map,
which is where the protection work shows up in the ISA.

## 18. Settled at M5

Roughly ordered by how much the rest depends on them. Answered questions move to §15; this list
holds what is still open, and none of it blocks M1.

**None.** The one question -- `SYSCALL` error values -- is settled here, at M5:

- `R2 = 0` for a call that succeeded.
- `R2 = 0xFFFFFFFF` (-1) for a refused argument, including a pointer outside user RAM.
- `R2 = 0xFFFFFFFE` (-2) for a call number the kernel does not implement.

A refused call never traps: the kernel returns the value and the program continues. That is the
whole contract -- the numbering itself is the kernel's (ISA §15.49), and the display numbers live in
`kr232-devices.md` §14.
## Appendix A — Encoding vectors

`asm -> bytes`, all little-endian. Branch vectors are given as explicit displacements because a
relative encoding depends on the address.

### Short class

| Assembly | Bytes |
|---|---|
| `push fp` | `0D` |
| `pop fp` | `1D` |
| `ret` | `70` |
| `nop` | `71` |
| `brk` | `72` |
| `sys` | `73` |
| `ei` | `74` |
| `di` | `75` |
| `halt` | `76` |
| `iret` | `77` |
| `rdcycle` | `78` |
| `syscall` | `79` |
| `inc r2` | `22` |
| `dec r2` | `32` |
| `call r3` | `43` |

### Compact class

| Assembly | Bytes |
|---|---|
| `mov r3, r0` | `80 30` |
| `add r3, r4` | `81 34` |
| `cmp r4, r0` | `88 40` |
| `ldbu r4, [r2]` | `90 42` |
| `ld r4, [r2]+` | `93 42` |
| `jmp r3` | `9F 30` |
| `mov fp, sp` | `80 DF` |
| `neg r2` | `9D 20` |
| `not r2` | `9E 20` |
| `beq +6` | `A0 06` |
| `bra -9` | `BF F7` |

### Normal class

| Assembly | Bytes |
|---|---|
| `ld r4, [r5+3]` | `C0 45 03` |
| `st [r5+3], r4` | `C1 45 03` |
| `ldhu r4, [r5+2]` | `DD 45 02` |
| `sth [r5+2], r4` | `DE 45 02` |
| `ldhs r4, [r5+2]` | `DF 45 02` |
| `ld r4, #0x34` | `C6 40 34` |
| `ld r4, #-3` | `C5 40 FD` |
| `setlt r2` | `D3 2B 00` |
| `shl r2, #3` | `D4 20 03` |
| `div r2, r3` | `D9 23 00` |
| `sxtb r4` | `DB 40 00` |
| `jmp +6` | `CF 06 00` |
| `call +6` | `D0 06 00` |
| `jmp -8` | `CF F8 FF` |
| `push #0x0300` | `D7 00 03` |
| `pop #0x0300` | `D8 00 03` |
| `leave #0x0300` | `DC 00 03` |

### Register class

| Assembly | Bytes |
|---|---|
| `ld r2, #0x1234` | `52 34 12 00 00` |
| `ld r6, [0x4000]` | `66 00 40 00 00` |
| `st [0x4000], r6` | `E6 00 40 00 00` |

### Escape class

| Assembly | Bytes |
|---|---|
| `ldbu r2, [0x4000]` | `F1 02 00 40 00 00` |
| `ldhu r2, [0x4000]` | `F1 52 00 40 00 00` |
| `lea r2, [sp+6]` | `F1 42 06 00 00 00` |
| `fadd f0, f1` | `F3 01` |
| `fadd f2, f3` | `F3 0B` |
| `fcmp f0, f1` | `F3 81` |
| `frnd f1` | `F3 94` |
| `fld f0, [r2+8]` | `F4 01 20 08` |
| `fst f1, [r2]` | `F4 14 20` |
| `fsts f1, [r2+4]` | `F4 35 20 04` |
| `i2f f0, r2` | `F5 00 20` |
| `f2i r2, f0` | `F5 20 20` |
| `fpush f1` | `F6 01` |
| `fpop f1` | `F6 05` |
| `enter #8, #0x0300` | `F7 08 00 03` |
| `enter #300, #0` | `F8 2C 01 00 00` |
| `enter #0x1000, #0x0100` | `FB 00 10 00 00 00 01` |
| `mov r2, r3` (generic) | `F0 00 02 03` |
| `ld r2, [r3+4]` (generic) | `F0 0E 02 53 04` |
| `ld r2, [r3+6]` (generic) | `F0 0E 02 63 06 00` |
| `ld r2, [r3+0x1234]` (generic) | `F0 0E 02 73 34 12 00 00` |
| `st [r3+2], r2` (generic) | `F0 0F 02 53 02` |
| `mov r2, #0x4000` (generic) | `F0 00 02 10 00 40` |
| `sxth r2` (generic) | `F0 1B 02 00` |
| `ldhu r2, [r3+2]` (generic) | `F0 1C 02 53 02` |
| `jmp 0x0200` | `F9 00 00 02 00 00` |
| `call 0x0200` | `F9 10 00 02 00 00` |
| `jmp [0x4000]` | `F9 20 00 40 00 00` |
| `lea r2, [pc+0x100]` | `F9 42 00 01 00 00` |
| `ld r2, [pc+0x100]` | `F9 62 00 01 00 00` |
| `add r2, #0x123456` | `F9 A2 56 34 12 00` |
| `cmp r2, #0x10000` | `F9 C2 00 00 01 00` |
| `and r2, #0xFF00` | `F9 D2 00 FF 00 00` |
| `fld f2, [r3+0x100]` (wide) | `FA 08 30 00 01 00 00` |
| `fst f1, [pc+0x40]` (wide) | `FA 15 00 40 00 00 00` |

Notes on the vectors:

- `ld r4, #0x34` assembles to the three-byte Normal form `C6 40 34`, not the five-byte Register
  form `54 34 00 00 00`. This is the reverse of kr216, where both were three bytes and the Register
  form won.
- The generic `mov r2, #0x4000` vector pins the `F0` immediate encoding; the assembler never emits
  it, because the five-byte `LD Rd,#imm32` is shorter.
- `setlt r2` is Normal op 19, payload `RRRR CCCC` = `0x2B` (R2, cond 11), then `0x00`.
- `neg r2` and `not r2` are Compact ops 29 and 30 with `Rb = R0`.
- `div r2, r3` carries a reserved zero third byte, so every Normal-class instruction is three
  bytes.
- `jmp +6` / `call +6` / `jmp -8` are `rel16` from the next instruction; the absolute targets
  `jmp 0x0200` / `call 0x0200` are `F9` ops 0 and 1 with `RRRR = 0`.
- Generic `st` keeps the data register in specifier 1 (mode 0, R2 = `0x02`) and the address in
  specifier 2 (mode 5, R3 = `0x53`), with the single `d8` extension last.
- `ld r2, [r3+0x1234]` is the new mode 7: specifier 2 is `0x73` (mode 7, R3) followed by a four-byte
  `d32`, for eight bytes total.
- `fst f1, [pc+0x40]` uses `FA` with `MM = 01`; `BBBB` is reserved zero in the PC form.

## Appendix B — First-byte map

```
0x00-0x0F  PUSH Rd              1
0x10-0x1F  POP Rd               1
0x20-0x2F  INC Rd               1
0x30-0x3F  DEC Rd               1
0x40-0x4F  CALL Rd              1
0x50-0x5F  LD Rd, #imm32        5
0x60-0x6F  LD Rd, [addr32]      5
0x70-0x77  system               1
0x78       RDCYCLE              1
0x79-0x7F  reserved             trap
0x80-0x9F  Compact register     2
0xA0-0xBF  Compact branch       2
0xC0-0xDF  Normal               3
0xE0-0xEF  ST [addr32], Rd      5
0xF0       generic              4-8
0xF1       long integer         6
0xF2       reserved (64-bit)    trap
0xF3       FP register          2
0xF4       FP load/store        3-4
0xF5       FP convert           3
0xF6       FP push/pop          2
0xF7       ENTER short          4
0xF8       ENTER long           5
0xF9       wide integer         6
0xFA       FP load/store wide   7
0xFB       ENTER frame32        7
0xFC-0xFF  reserved             trap
```

## Appendix C — Memory map

The map is normative in §2.3 and §14. This appendix repeats it in one table so a decoder, a linker
script and a device model can all be checked against the same list.

| Range | Size | Region | Access |
|---|---|---|---|
| `0x00000000`–`0x000000FF` | 256 B | null page, kernel space | traps (vector 4) |
| `0x00000100`–`0x07FFFFFF` | 128 MiB − 256 B | kernel RAM | read, write, execute; user mode faults |
| `0x08000000`–`0x0FFFFFFF` | 128 MiB | user RAM | read, write, execute in either mode |
| `0x10000000`–`0xFBFFFFFF` | 3776 MiB | unbacked gap | traps (vector 4) |
| `0xFC000000`–`0xFDFFFFFF` | 32 MiB | framebuffer VRAM aperture | device bus; user mode faults |
| `0xFE000000`–`0xFFEFFFFF` | 31 MiB | unbacked gap | traps (vector 4) |
| `0xFFF00000`–`0xFFFEFFFF` | 960 KiB | device window | device bus; user mode faults |
| `0xFFFF0000`–`0xFFFFFFFF` | 64 KiB | reserved | traps (vector 4) |

Inside the device window, page *n* is `0xFFF00000 + n × 0x1000` for *n* = 0–239. Section 14.1
lists the populated pages and `kr232-devices.md` defines their registers. The reset word and the
trap vectors are the first 64 bytes of the RAM range, at `0x00000100`–`0x0000013F`.
