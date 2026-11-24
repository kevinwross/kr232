# Debugging instruments

Four command-line options that turn a run of a VM into a measurement. They were
written during a hunt for one defect — an immediate shift that selected the wrong
direction, so every scaled index in every compiled program came out zero — which
took twenty-five rounds because none of these existed. Each one answers a question
that reading a listing cannot.

The options below are implemented in `kr232-emu` (`kr232/src/main.cpp`) and, with
the same names and output, in `kr32`, `kr16` and `kr216`.

## `--watch <address>`

**Question it answers:** *which instruction writes this word?*

Prints a line for every step that changes the 32-bit word at `<address>`, with the
program counter of the write.

```sh
kr232-emu vm program.kr232 --watch 0x07ffff98
watch 0x07ffff98 at pc=0x00166b41: 0x07ffffe0 -> 0x07ffffe1
```

**Implementation.** The loop already steps one instruction at a time for
`--max-steps`. Read the word before the step, read it after, and print when the two
differ. Nothing is hooked; a memory bus that reported writes would be faster and
would change the machine, and this does not.

**The defect class.** A value that is wrong at a known place but whose writer is
unknown — aliasing, a stale slot, a stack frame overwritten by a callee. It found
the shift bug's symptom: a pointer "incrementing by one" between iterations.

## `--break <address>` and `--break-max <n>`

**Question it answers:** *what were the registers and the frame when this
instruction ran?*

Stops at the instruction whose address matches, prints the argument registers
(R2–R8), the frame and stack pointers, and the words below the frame pointer, then
exits. `--break-max` keeps going and repeats the dump for the next `n` hits, which
turns a call site into a sequence.

```sh
kr232-emu vm program.kr232 --break 0x1885fc --break-max 30
break at 0x001885fc: r2=0x07ffff70 r3=0x0000077c … fp=0x07fffff8 sp=0x07fffd54
  [fp- 4] = 0x07ffff70
  …
```

**Implementation.** Before each step, compare `machine.pc()` with the address. The
frame dump reads the eight words below `machine.reg(13)` through the ordinary
memory API; an address register dump (`[r8-8] … [r8+16]`) is equally useful and is
what a later revision printed.

**The defect class.** Wrong arguments at a call boundary: a caller passing garbage,
a callee reading the wrong slot, a register clobbered between two functions.
`--break-max` also distinguishes "always wrong" from "wrong from the tenth call",
which is the difference between a codegen bug and a buffer overrun.

## `--watch-page <address>`

**Question it answers:** *what wrote anything in this region, in order?*

Rounds the address down to a 4 KiB page and prints **every** changed word in that
page, with the PC of each write — a chronological log of all stores to the stack or
heap region.

```sh
kr232-emu vm program.kr232 --watch-page 0x07fff000
page 0x07ffffe0 at pc=0x00166a7c: 0x00000000 -> 0x00000770   ← array element 0
page 0x07ffff70 at pc=0x00166bb0: 0x00000001 -> 0x00000002   ← loop counter
page 0x07ffff98 at pc=0x00166b41: 0x07ffffe0 -> 0x07ffffe1   ← the bug
```

**Implementation.** Snapshot the 1024 words of the page before the first step, then
after every step compare word by word and print the differences, updating the
snapshot. It costs 1024 reads per instruction, which is affordable for runs of tens
of thousands of steps and is the reason it is not the default.

**The defect class.** An unknown writer in a known region. It answers "what is
touching this frame at all" when `--watch` needs an address you do not yet have,
and its ordering shows aliasing directly: two variables' values appearing at one
address is a slot collision, visible in one run.

## `--watch-fp`

**Question it answers:** *does the frame pointer ever move?*

Prints a line whenever `r13` changes, with the PC.

```sh
kr232-emu vm program.kr232 --watch-fp
fp 0x00000000 -> 0x07fffff8 at pc=0x00166a62
```

**Implementation.** Same shape as `--watch`: record `machine.reg(13)` before the
step, compare after.

**The defect class.** It is a *negative* instrument, and that is its value. During
the hunt I concluded from arithmetic that the frame pointer must be drifting; one
run of `--watch-fp` showed exactly one change — the function's `enter` — and killed
the hypothesis in a single line. A cheap test that can only confirm or refute one
claim is worth more than another afternoon of reading, because it ends the claim.

## How to use them together

The order matters, and it took twenty-five rounds to learn:

1. **`--watch-fp` first**, when any hypothesis involves the frame. It costs one run
   and eliminates a whole class of explanation.
2. **`--break` at the boundary** where a good value becomes a bad one — a call, a
   return, a loop head. This localises the defect to a few instructions rather than
   a function.
3. **`--watch` on the address** once you know which word is wrong, to name the
   writer.
4. **`--watch-page`** when you do not yet know the address, or when you suspect
   aliasing rather than a single bad write.

And one rule that saved no time at all until it was followed: **read the address
out of the dump, never compute it from a frame size.** Four hand-computed slot
addresses were wrong during the hunt, and each one produced a confident, false
conclusion.

## What these instruments found

The shift defect, in one line of output each:

```
--watch-fp        fp never moves (kills the frame-drift hypothesis)
--break … --max   insert receives the right pointer for nine calls, garbage on the tenth
--watch-page      a pointer slot receives 0x07ffffe0 then 0x07ffffe1
--watch           one instruction, pc=0x00166b41, writes both
```

The instruction was `st [fp-96], r6` after `shl r7, #2`, and `shl r7, #2` was the
bug: it shifted right. A five-line assembly test of the shift — `ld r7, #1; shl r7,
#2`, break after, expect `r7 = 4` — now exists in `kr232/tests/test_machine.cpp` and
would have caught it in the first hour.
## Porting notes

The four options exist in `kr232`, `kr32`, `kr16` and `kr216` under the same names.
Each port adapts three things, and each port's commit records which it verified.

| Machine | Word watched | Registers dumped by `--break` | Frame register |
|---|---|---|---|
| kr232 | 32-bit | r2–r8, FP, SP | r13 |
| kr32 | 32-bit | r2–r5, SP | r13 |
| kr16 | 16-bit (two bytes) | r0–r4 and PC | none: kr16 keeps PC and SP outside the register file |
| kr216 | 16-bit | r2–r9, then FP and SP | r13 |

`--watch-fp` reports nothing on a machine with no frame register, which is a
statement about the machine, not a failure of the instrument.

**What has been seen working**, as opposed to implemented:

- kr232: all four, including the run that found the shift defect.
- kr32: a break at the entry printing the registers (`bios.kr32`), and on the
  compiled `hello_c.kr32` a watch hit and a page log of the stack stores
  (`0x0800FFEC` taking `0x0002F538` at pc `0x0002F530`).
- kr16: a break at the image entry, and both a watch hit (`0x2000` going from
  `0x1234` to `0x5678`) and a page log on a five-instruction program written to
  exercise them.
- kr216: all four, on a three-line program compiled with the bundled toolchain:
  a break at `0x0200` printing the registers and `SP=0xFEF0`, a watch hit at
  `0xFEC2` (`0x035E` stored by pc `0x0237`), a page log of the stack stores, and a
  frame-register change (`0x0000` to `0xFEEE` at the function entry).

One cosmetic difference is worth knowing: kr216's emulator prints its budget or
halt message after a breakpoint stops the run, because the stop is taken inside the
step loop and the message is emitted afterwards. The break output is correct.
