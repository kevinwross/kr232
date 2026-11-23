# kr232 — Implementation plan

| | |
|---|---|
| Status | **Approved 0.1** — approved by the owner on 2026-11-22 as the implementation plan for M0–M5. Implementation starts at M0; no box is ticked yet |
| Date | 2026-11-22T19:20:06-08:00 |
| Basis | `kr232-plan.md` 0.17 (approved), `kr232-isa.md` 0.4, `kr232-devices.md` 0.4 |
| Authoritative for | task order, checklists, build and test commands, hosts, the log and every gate record for M0–M5 |
| Not authoritative for | milestone scope, order and exit criteria (`kr232-plan.md` §4–§10), instruction encodings (`kr232-isa.md`), device registers (`kr232-devices.md`) |
| Repositories | `kr232` (this), `..\krcompiler`, `..\krdbg`; `..\kr216`, `..\kr32` and `..\kr16` are **read-only reference** |
| Coordinator | the agent running this plan, across all three working repositories |

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

## 0. How to use this document

- **One checkbox is one commit-sized unit and its check.** A box is ticked only when the named check has run and its evidence is in the log. A written-but-unchecked task is the normal state between sessions.
- **The plan owns scope; this file owns work.** Every gate below copies its exit criterion verbatim from `kr232-plan.md` §6–§10 and adds the commands that produce the evidence. When the two disagree, the plan wins and this file is corrected.
- **Nothing is reticked.** If a change invalidates a gate, the gate gets a new block with a new timestamp and the old block stays.
- **A failed check is recorded as failed**, with the output, not deleted and re-run until green.

## 1. Hosts and builds

| Host | Architecture | Role | Jobs |
|---|---|---|---|
| CROSBY (Windows 11, MSVC) | x64 | primary edit host; `windows-msvc*` rows; every document edit and the spec lint | `-j10` |
| CROSBY (WSL, Ubuntu) | x64 | GCC and Clang rows; the Linux-side behaviour a Windows host hides | `-j10` |
| BitWhiz (aarch64, GCC 14.2.0) | ARM64 | the ARM64 row, reached by `ssh bitwhiz` | `-j4` |

**Per-repository commands.** These are the shapes the repositories already use; the first task of each milestone confirms them and the log records the exact line run.

`kr232` (from M1, once it has a build):

```sh
cmake --preset windows-msvc          # or linux-wsl, linux-arm64
cmake --build --preset windows-msvc -j 10
ctest --preset windows-msvc
node tools/check-isa.mjs             # after every document edit, on every host
```

`krcompiler` (presets as they stand):

```sh
cmake --preset windows-msvc-debug    # linux-gcc-debug, linux-clang-debug, linux-sanitize
cmake --build --preset windows-msvc-debug -j 10
ctest --preset windows-msvc-debug
```

`krdbg` (presets as they stand):

```sh
cmake --preset windows-msvc          # linux-wsl, linux-arm64
cmake --build --preset windows-msvc -j 10
ctest --preset windows-msvc
```

**BitWhiz.** The checkout is a clone of each repository under `~/repos/<name>`. A run is `git pull --ff-only`, then configure if `CMakeLists.txt` or `CMakePresets.json` changed, then `cmake --build --preset <arm64 preset> -j 4` and `ctest --preset <arm64 preset>`. Golden-image tests are excluded there, as `kr232-plan.md` §13 requires; a green ARM64 row says nothing about a surface hash and the log says so when it is quoted.

**No network installs.** A missing compiler, SDK or package manager is reported and the row is recorded as not run; nothing is downloaded, vendored or placed in a repository.

**Components this project builds may be installed.** `krcompiler` is the one case, and the owner has authorised it. krdbg's `tests/test_host_build.cpp` reads `KRDBG_KRC_PREFIX`, so M4 cannot be a real check without a `krc` install:

```sh
cmake --install <krcompiler build dir> --prefix <prefix>
```

- The prefix sits outside every repository and outside `build/`, because a clean gate deletes `build/`: `C:\Users\kevin\repos\krc-prefix` on Windows, `~/krc-prefix` in WSL and on BitWhiz. Each host installs from its own tree.
- The installed revision is recorded in the log with `krc --version`, and the prefix is reinstalled whenever `krcompiler`'s toolchain changes. A stale prefix is a silent test of an older compiler, which is the failure mode krcompiler's own `AGENTS.local.md` records for cached paths.
- `kr232` and `krcompiler` tasks use the built binary from the build tree directly. Two consumers need the prefix instead: krdbg's tasks (the `KRDBG_KRC_PREFIX` checks) and, from M5, kr232's kernel target, which invokes `krc` from it. The log says which of the three a given command used.
- Nothing under a prefix is ever committed.

**Source layout.** Host code and target code never share a directory, and the directory's name says which compiler owns it. This is the pattern `krcompiler` already uses for KR32 and KR216, so the same path means the same thing in both repositories.

| Path | Compiled by | Holds |
|---|---|---|
| `src/`, `include/kr232/` | the host toolchain (MSVC, GCC, Clang) | the emulator, the disassembler, and their headers |
| `targets/kr232/` | `krc`, for the VM — via a custom target that invokes it, never a host compile | the M5 kernel, guest test programs, any C or assembly a guest runs |
| `sysroot/kr232/include/` (in `krcompiler`) | `krc`, as guest includes | `bits/*.h` and the `<kr232/*.h>` packages |
| `tests/fixtures/` | nothing — data | hand-encoded images, assembled `.s` sources, goldens |

- A host target's source list is explicit and **never globbed**, so nothing under `targets/` can be swept in. A layout check in the suite fails if any host target's sources contain a path under `targets/`.
- **Invoking `krc` is not a host compile.** The kernel's custom target runs `krc` on `targets/kr232/kernel/`; the layout check asserts that such a target's command line contains `krc`, and that no host target *compiles* a file under `targets/`. The distinction is the whole rule: one names the guest compiler, the other names the host one by accident.
- No header under `src/` or `include/` is included from `targets/`; guest code sees only `sysroot/kr232/include/`.
- A file under `targets/` that the host build compiles is a bug, not a convenience — the symptom is a confusing link error at best, and a host-built guest binary that only misbehaves under the VM at worst.

**One tree per host, and no host path in any artifact.** Windows edits `C:\Users\kevin\repos\<repo>`. WSL builds from a native ext4 mirror, refreshed from the Windows tree before a run:

```sh
scripts/wsl-mirror.sh                      # rsync -a --delete --exclude build/ /mnt/c/Users/kevin/repos/kr232/ ~/mirrors/kr232/
```

- The mirror is disposable, never edited, never committed; `.git` comes along so version stamping works. BitWhiz already builds from `~/repos/<name>` on ext4, so the Linux hosts differ in machine, not in shape.
- **A build directory is never shared between hosts.** Each preset has its own `binaryDir` and `build/` is excluded from the mirror: an MSVC cache is unreadable by GCC, and stale objects from the other platform are worse than a full rebuild. Build outputs never live on `/mnt/c` — drvfs is slow and its timestamps are coarse enough to mislead incremental builds.
- **No host path reaches a binary or a `.kdbg`.** GCC and Clang get `-ffile-prefix-map=<src>=/src -fdebug-prefix-map=<src>=/src`; MSVC gets `/pathmap:<src>=/src`. A debug record then says `/src/…` whichever host built it, so the last builder no longer decides what the debug data means, and the determinism comparison can include debug sections honestly. krdbg resolves `/src` through its source-root setting.
- The rule is checked, not trusted: a path-hygiene test greps the built artifacts for `C:\` and `/mnt/c` and fails on a hit.

## 2. Push policy

Push what has been verified, at the size of a reviewable unit, and never leave a default branch red.

| Situation | Push | What must have run first |
|---|---|---|
| Documents in `kr232` (ISA, devices, plan, this file) | immediately | `node tools/check-isa.mjs` |
| Code in `kr232`, task-sized | to `main`, freely | clean build `-j10`, `ctest` on the touched rows, lint |
| Code in `kr232`, milestone exit | to `main` | the milestone's gate, plus the ARM64 row recorded |
| Anything in `krcompiler` or `krdbg` | a `kr232-mN` branch, never their `main` | that target's local rows green and the existing targets' suites unchanged |
| `krcompiler` or `krdbg` at a milestone exit | branch, then a pull request | the full gate, both hosts |
| Fixing a red `main` | immediately | the row that was red, plus its neighbours |

1. **Branch by repository.** `kr232` is the coordinator's own repository and works on `main`. `krcompiler` and `krdbg` carry KR32, KR16, KR216 and their users, so kr232 work goes on `kr232-m0` … `kr232-m5` and reaches their default branch — `master` in `krcompiler`, `main` in `krdbg` — by pull request at that milestone's gate.
2. **ARM64 is a milestone cadence.** BitWhiz runs at each gate and additionally whenever a change is platform-sensitive: alignment, word size, endianness, or anything reading host byte order.
3. **Log before push.** The entry names host, command, result and revision. A push whose entry is missing has no evidence.
4. **No force-push. Tags only at approvals and freezes**: `spec-approval-1` exists; `isa-1.0` and `devices-1.0` are cut at M3's gate. Milestones get commits, not tags.

## 3. Logging and timestamps

**Session entries** live under `## Log`, appended, newest last, one per session or per significant step:

```
### <iso> — M1: decoder covers the Short class
Host: CROSBY (windows-msvc, -j10)
Changed: src/decoder.cpp, tests/test_decode.cpp
Ran: cmake --build --preset windows-msvc -j 10 → clean; ctest -R kr232_decode → 12/12
Failed: nothing
Next: Compact class, then the escape prefixes
```

The timestamp is the moment the entry is written: call `local-datetime` immediately before writing it and copy its `iso` string verbatim. It describes the record, not the run.

**Gate blocks** live under `## Gate verification` and are stamped differently: with the **start of the first build** the gate ran — the configure or compile line that began it, or the rebuilt binary's modification time when that is what identifies it. The block states which of the two it used. `local-datetime` is not used here, because it answers "what time is it now" about something that has already finished:

```
### <first build start> — M1 gate (Windows x64)
Build: cmake --preset windows-msvc; cmake --build --preset windows-msvc -j 10 (started <ts>)
Tests: ctest --preset windows-msvc → 214/214
ARM64: ssh bitwhiz … ctest --preset linux-arm64 → 108/108, golden tests excluded
Lint: node tools/check-isa.mjs → ok: 81/81 vectors; …
Revision: <sha>
```

Both forms are ISO-8601 with the host offset to the second, like the plan's version table. A wrong stamp is never corrected in place; a new entry supersedes it.

## 4. Coordination

- **One coordinator, one log.** This file is the single record of work across the three repositories, and every task carries its repository tag. A task in `krcompiler` is ticked here, not in a second log there.
- **Order is the plan's order.** M0 → M1 → M2 → M3, with M4 starting after M2 and completing before M3, and M5 after 1.x. A later milestone does not begin to avoid idling.
- **Subagents are for independent verification, not for contract-bearing code.** They are used to re-read a diff against the plan, to check a gate's evidence, and for parallel read-only research. The descriptor, the ABI, the container and the encoder are written by the coordinator, because a frozen contract is cheaper to keep consistent in one head than to negotiate between two.
- **The reference VMs are read.** `..\kr216`, `..\kr32` and `..\kr16` are read for precedent; nothing is built, imported or copied from them.

---

## 5. M0 — contracts and identifiers

**Goal.** Make kr232 nameable and its contracts written, before any code. This is the guide's step 1 (`..\krcompiler\new-target-guide.md` §9).

**Preconditions.** The plan is approved; `spec-approval-1` is pushed.

### Tasks

- [x] `[kr232]` Add `README.md`: what kr232 is, the three documents, and the one command that checks them. Check: the file names the lint command and the approved versions.
- [x] `[krcompiler]` Write the ADR — **0057**, the next free number, not the 0055 assumed at drafting: kr232 fits descriptor schema v2, `sret_register = 6`, `this_register = 10`, `aggregate_rules = MemoryOnly`, data model `Ilp32` with `long_long_bits` 0, `has_matrix` false, and the target id **5** for both KRO and KDBG. Check: committed on `kr232-m0` (972294b) and names every field it fixes.
- [ ] `[krcompiler]` Add the `plan.md` Appendix A decision row for kr232, next after D-050. Check: the row exists and ADR 0057 is cited.
- [ ] `[krcompiler]` Write `docs/abi-kr232.md`: data model (`Ilp32`; 32-bit pointers, `size_t`/`ptrdiff_t` 32, `intmax_t` 32, no 64-bit integer, `long double` 64, signed `char`), argument registers R2–R5, result R2, `this` R10, FP R13, SP R15, callee-saved R8–R9, frame layout (saved caller FP at `[FP]`, EFP chain in R12), 4-byte alignment with 8 recommended, narrow-type extension, varargs, aggregates and the R6 sret convention, the floating-point convention over F0–F3, vtable and typeinfo layout with the reserved `[vptr - 4]` slot, and FP register preservation. Check: every section `abi-kr216.md` carries has a kr232 counterpart or a stated "not applicable".
- [ ] `[krcompiler]` Extend the `target_id` line in `docs/symbols.md` to `5` kr232. Check: the line names all five. The `kROTargetId` case waits for the profile row in M2 — the function switches on `TargetId`, so the constant cannot precede the enumerator.
- [ ] `[krcompiler]` Add the failing test: `krc --target=kr232 -c` is expected to stop with "has no code generator yet". Check: the test **fails** at M0 with `unknown target` and the log quotes both messages.
- [ ] `[kr232]` Update `AGENTS.local.md` to point at the approved plan and this file. Check: done when the file exists.

### Tests

- `node tools/check-isa.mjs` on every host — the only executable check in this repository.
- The new refusal test, which must fail for the documented reason.
- `ctest --preset windows-msvc-debug` on krcompiler, to prove the test registration did not disturb the existing targets.

### Gate

> **Plan §5.** Both ADRs are reviewed and merged; the ABI document is merged; the id `5` is recorded in ADR 0057 and in `docs/symbols.md`, with the `kROTargetId` case following at M2; the failing test exists and fails for the documented reason.

### Exit evidence

The gate block under `## Gate verification`, quoting the failing test's output, the ADR's number and the id lines, and the krcompiler suite count before and after.

---

## 6. M1 — `kr232-emu`: core and device model

**Goal.** Make the ISA executable: decoder, executor, protection, traps, eight device pages, a headless CLI, and a golden frame.

**Preconditions.** M0's gate is recorded; the ISA is 0.4.

### Tasks

- [ ] `[kr232]` Build skeleton: `CMakeLists.txt`, `CMakePresets.json` (`windows-msvc`, `linux-wsl`, `linux-arm64`, each with its own `binaryDir` and the prefix-map flags of §1), `include/kr232/`, `src/`, `targets/kr232/`, `tests/`, `scripts/wsl-mirror.sh`, and `scripts/gate-windows.ps1` / `gate-linux.sh` cloned from krdbg. Check: configure and build clean on Windows `-j10` and WSL `-j10`; the layout check fails when a `targets/` file is added to a host target; the path-hygiene test fails on a deliberately absolute `__FILE__` string and passes on the real binary.
- [ ] `[kr232]` `memory`: the 4 GiB space with the seven regions of ISA §2.3, load/store of 1/2/4 bytes, and the unbacked-gap behaviour. Check: unit tests per region boundary.
- [ ] `[kr232]` `decoder`: every class of ISA §3.1, including `F0`/`F4` lengths resolved from the bytes after the prefix. Check: `kr232-dis`-quality decode of all 81 Appendix A vectors, bytes in, text out.
- [ ] `[kr232]` `executor`: ALU, shifts, branches, loads/stores, `ENTER`/`LEAVE`, `CALL`/`RET`, `PUSH`/`POP`, halfword and byte forms, `SYS`, `IRET`, `BRK`, `RDCYCLE`. Check: one test per vector from bytes to architectural state.
- [ ] `[kr232]` Protection and traps: the 16-byte frame, vector 4 for the gaps and null page, vector 8 for the two user ranges and kernel-only instructions, vector 9 for `SYSCALL`, `IRET`'s restore of K, I and SP, and the K/I write rules. Check: the M1 gate's protection list, case by case.
- [ ] `[kr232]` Devices: timer, console, framebuffer, keyboard, terminal, graphics commands, system, blitter — registers, `ID` at `0x3C`, reset values, and the trap rules for undefined offsets and reserved pages. Check: per-page tests, and the `ID` word of all eight.
- [ ] `[kr232]` Blitter: `COPY`, `FILL`, `UPLOAD`, range validation before any byte moves. Check: a rejected range leaves memory unchanged.
- [ ] `[kr232]` Frame commit and `--frame-hash`: the committed surface's hash, with a checked-in golden. Check: the golden matches on x64 and the ARM64 row records "excluded".
- [ ] `[kr232]` Flat image loader and CLI: `vm <image> --max-steps N`, `--trace`, `--screenshot`, `--frame-hash`, with the program's `SYS exit` status as the process status. Check: a hand-encoded image runs and exits with its status.
- [ ] `[kr232]` A hand-encoded kernel-mode image that writes to the terminal, draws through the graphics page and flips. Check: its frame hash equals the golden.
- [ ] `[kr232]` Record the gate. Check: `## Gate verification` block with the plan's five items, each naming its test.

### Tests

The 81 vectors, the protection suite, the eight device suites, the blitter rejection cases, the frame golden, and the lint. `ctest --preset windows-msvc` and `--preset linux-wsl` at `-j10`, `--preset linux-arm64` at `-j4` on BitWhiz.

### Gate

> **Plan §6.** (1) Each of the 81 Appendix A vectors executes from its bytes and produces the documented architectural effect. (2) A user-mode access below `0x08000000` or at or above `0xFC000000` raises vector 8 with a 16-byte frame; an access to the unbacked `0x10000000`–`0xFBFFFFFF` gap raises vector 4 in either mode, while the `0xFE000000`–`0xFFEFFFFF` gap raises vector 4 in kernel mode and vector 8 in user mode; `IRET` restores K, I and SP; `SYSCALL` resumes after the instruction; `EI`, `DI`, `HALT`, `SYS` and `IRET` trap in user mode. (3) Every populated device page answers `ID` at `0x3C`; an undefined offset and a reserved page trap with vector 4; a rejected FLIP sets `FLIP_ERROR` and commits nothing; blitter `COPY`/`FILL`/`UPLOAD` validate ranges before moving bytes. (4) A hand-encoded kernel-mode image writes to the terminal, draws through the graphics page and flips; `--frame-hash` over the committed surface matches a checked-in golden. (5) `node tools/check-isa.mjs` still passes.

---

## 7. M2 — profile, assembler, object writer, disassembler, container

**Goal.** The target becomes assemblable and linkable, but not compilable.

**Preconditions.** M1's gate is recorded, so an image can be executed.

### Tasks

- [ ] `[krcompiler]` `TargetId::Kr232`, the `TargetProfile` row with all nine capability flags (`has_fpu` true, `has_matrix`, `has_atomics`, `has_threads`, `has_tls`, `has_eh`, `has_rtti` false, `has_cpp` true, `backend_available` false), `to_string`, `parse_target`. Check: `test_target` pins every field; `krc --list-targets` shows `kr232 backend=no`.
- [ ] `[krcompiler]` `Kr232Descriptor` with the register roles of M0. Check: a unit test reads each role back.
- [ ] `[krcompiler]` `src/asm/kr232_assembler.cpp` and its encoder, plus `make_object` with target id 5. Check: round-trip tests, assemble → bytes → disassemble, for every Appendix A vector.
- [ ] `[krcompiler]` `.kdbg` emission from assembly: line and function records. Check: `krc --dump-symbols` on a linked image names the fixture's lines.
- [ ] `[krcompiler]` `src/linker/kr232_image.cpp` — the `.k232` writer the M0 ADR specifies: header, section records, the `.reset` section at `0x0100`, CRC, `image_base = 0`. Check: a unit test reads back every header field, and the writer rejects an entry outside executable text, a section outside the map and overlapping sections.
- [ ] `[krcompiler]` `toolchain::link_impl` selects the writer for target 5, with 4-byte heap alignment and the kr232 stack bounds. Check: a linked image's header and entry agree with the ADR.
- [ ] `[krcompiler]` `krc --dump-symbols` reads a `.k232`. Check: the gate's decode assertion passes on a fixture.
- [ ] `[krcompiler]` `cmake/RunKr232Image.cmake` and the `KRC_KR232_VM` cache variable in `CMakeLists.txt`, with the sibling fallback. Check: a deliberate wrong exit status fails with `expected exit N, got M`.
- [ ] `[kr232]` `kr232-dis`: the canonical decoder and text form, shared with the emulator. Check: a disassembly test over all 81 vectors and over the assembled fixture.
- [ ] `[kr232]` The `.k232` loader replaces the flat loader and dispatches on the container magic. Check: a bad checksum, a section outside the map and a base mismatch each fail at load.
- [ ] `[kr232]` A `.s` fixture assembles to an object, links to a `.k232`, and runs under M1. Check: exit status and screen match.
- [ ] `[kr232]` Log the gate. Check: `## Gate verification` block with every M2 item.

### Tests

Round-trip for all 81 vectors; a hand-written `.s` linked and run; the `.kdbg` decode; two builds byte-identical; the loader's rejection cases; `krc --list-targets`; the krcompiler suite on Windows and WSL.

### Gate

> **Plan §7.** `krc --list-targets` shows `kr232` with `backend=no`; a hand-written `.s` file assembles to an object, links to a `.k232`, and runs under M1 with the expected exit status and screen; the image's `.kdbg` decodes through `krc --dump-symbols` with the fixture's lines and functions and `image_base = 0`; every vector round-trips byte-exactly; two builds of one image are byte-identical.

---

## 8. M3 — code generator, runtime, sysroot, packages

**Goal.** C and C++ programs build, run and are debuggable — and the ISA and devices freeze behind them.

**Preconditions.** M2's gate is recorded; M4's adapter is available for the final check.

### Tasks

- [ ] `[krcompiler]` Backend skeleton in the guide's shape: `Kr232Backend(module, profile, level)`, `emit`, `debug_info`, `frame_bytes`, registered at both dispatch sites in `src/toolchain/toolchain.cpp` and the six branches in `src/tools/krc/main.cpp`. Check: `krc --target=kr232 -c` reaches the backend and emits a prologue.
- [ ] `[krcompiler]` Prologue/epilogue with `ENTER`/`LEAVE`, and the frame layout the ABI fixes. Check: a frame-size unit test; `-S` byte-identical against the previous step at each subsequent task.
- [ ] `[krcompiler]` Loads and stores, including `LDBU`/`LDHS`, the signed/unsigned narrow extensions and the FP forms. Check: by-value `char`/`short` parameters round-trip.
- [ ] `[krcompiler]` ALU, comparisons, branches, calls, returns. Check: `int main(void){return 42;}` returns 42 under the emulator.
- [ ] `[krcompiler]` Aggregate paths: memory-class parameters copied size-exactly, the R6 return destination loaded after argument evaluation, struct return by copy-out. Check: the three-`char` record test, and a nested call in an argument.
- [ ] `[krcompiler]` Varargs and FP argument/return conventions. Check: a `printf`-style call with mixed integer and `double` arguments.
- [ ] `[krcompiler]` C++ surfaces: class, `this` in R10, virtual dispatch with the reserved typeinfo slot, constructors. Check: a virtual call dispatches to the override; the vtable's `[vptr - 4]` word is present and zero.
- [ ] `[krcompiler]` `targets/kr232/runtime/crt0.s`: frame, call `main`, pass the status to `SYS exit`; no stack setup, since reset SP is `0x08000000`. Check: a linked image exits with `main`'s status.
- [ ] `[krcompiler]` `targets/kr232/runtime/libc.c`: console output, the `SYS` write/read/exit stubs, blitter-backed `memcpy`/`memset`. Check: output appears on the terminal transcript.
- [ ] `[krcompiler]` `sysroot/kr232/include/bits/` generated from the profile: `bits/types.h`, `bits/stdint.h`. Check: `stdint.h` widths round-trip in a unit test.
- [ ] `[krcompiler]` The four guest packages: `<kr232/fb.h>`, `<kr232/gfx.h>`, `<kr232/cons.h>`, `<kr232/input.h>`, with kernel-mode implementations. Check: the drawing program fills, draws and flips through `gfx`.
- [ ] `[krcompiler]` Per-target link/run loops at `-O0`, a determinism test, a KDBG-invariants test at `-O0`, and the clean gate. Check: the executed rows exist because `KRC_KR232_VM` resolved.
- [ ] `[krcompiler]` The `-O1` measurement ADR 0049 requires, recorded whether it lands or not. Check: the measurement is in the log; the `_o1` twins appear only if it does.
- [ ] `[krcompiler]` The compiled program loads in krdbg at source level and stops on a line breakpoint. Check: the session stops at the expected line with locals visible.
- [ ] `[kr232]` Log the gate and cut the freezes. Check: `## Gate verification` block; tags `isa-1.0` and `devices-1.0` on `kr232`, with the ISA and devices status lines moved to 1.0 and §18/§14 retitled "Deferred to 2.0".

### Tests

The per-target loops, determinism, KDBG invariants, the clean gate, `-S` byte-identity at every step, the three programs (C, C++, drawing), the krdbg stop, and the ARM64 row.

### Gate

> **Plan §8.** `int main(void){return 42;}`, a C++ program with a class, a virtual call, a by-value struct parameter, a struct return and an array, and a program that draws through `<kr232/gfx.h>` and flips, all build with `-O0`, run under `kr232-emu`, and match their status, terminal output and frame; the by-value and returned records include a three-`char` one, whose copy must be size-exact; `-S` output is byte-identical at every conversion step; the executed rows exist because `KRC_KR232_VM` is found; the compiled program loads in krdbg at source level and stops on a line breakpoint; determinism, KDBG invariants and the clean gate pass. **ISA 1.0 and devices 1.0 freeze at this gate.**

---

## 9. M4 — krdbg adapter

**Goal.** kr232 is debuggable at assembly level, then source level.

**Preconditions.** M2's gate is recorded, so a `.k232` with a `.kdbg` exists; and the `krc` prefix is installed from the same `krcompiler` revision the work is on (§1), because the host-build check cannot run without it.

### Tasks

- [ ] `[krdbg]` `adapters/kr232/` skeleton and CMake option `KRDBG_KR232_ADAPTER`, linked like KR216's direct-source pattern. Check: configure, build, and the adapter test target compiles.
- [ ] `[krdbg]` `layout()`: one core context, the register groups, flags Z…K with **K and I non-writable**, 32 address and pointer bits, `variable_instruction_length` true with `instruction_alignment` 1, `step_unit` "instructions". Check: the layout test asserts each field, and `write_flags` cannot clear K.
- [ ] `[krdbg]` Memory regions: kernel RAM, user RAM, aperture, device window, and both unbacked gaps marked neither readable nor writable. Check: a read in each region and in each gap.
- [ ] `[krdbg]` Execution and state: `step_instruction`, `run`, `pc`/`set_pc`, `read_value`/`write_value` by group index, `read_flags`/`write_flags`, `reset`. Check: stepping a fixture retires the documented instructions and reports `Halted`/`Fault` truthfully.
- [ ] `[krdbg]` `disassemble` through `kr232-dis`, advancing by decoded length. Check: a one-byte `RET` is followed by the next instruction, not by a four-byte gap.
- [ ] `[krdbg]` `unwind()` through the EFP chain, `FunctionInfo::frame` 0, and `Location::FrameOffset` negated. Check: two frames and a local's value at each.
- [ ] `[krdbg]` `IComPort` over page 2 and `IVideoSource` over the committed frame, with `has_com_port` and `has_framebuffer` agreeing. Check: a byte through the port reaches the guest, and a frame hash matches the emulator's.
- [ ] `[krdbg]` Source level: load `.k232`, locate `.kdbg`, `kdbg::read_payload`, resolve paths, `replace_target(std::move(target), info)` on reload. Check: line breakpoints resolve after a reload.
- [ ] `[krdbg]` Host wiring: `--kr232`, the include under `KRDBG_HAVE_KR232`, `load_any` on the `.k232` extension and `--target kr232`, `tests/test_host_build.cpp` with `KRDBG_KRC_PREFIX`. Check: the prefix is installed at the krcompiler revision recorded in the log, `test_host_build` **runs** rather than skipping, and the demo loads the fixture both ways.
- [ ] `[krdbg]` Fixtures and golden: `tests/fixtures/kr232/` assembled by M2's toolchain with a `regenerate.ps1`, and `goldens/kr232-loaded.hash`. Check: the golden matches on x64; the ARM64 run excludes it.
- [ ] `[krdbg]` `tests/test_kr232_adapter.cpp`: load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM, framebuffer. Check: ten cases green.
- [ ] `[krdbg]` Boundary check and host gate. Check: `krdbg_platform_boundary` clean; the host gate passes on the changed rows; the adapter build is reported separately because the repository gate builds adapter-OFF.
- [ ] `[kr232]` Log the gate. Check: `## Gate verification` block.

### Tests

The adapter suite, the golden, the boundary check, the host gate, and the demo's two load paths.

### Gate

> **Plan §9.** The adapter compiles with its option ON; the fixture tests cover load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM and framebuffer; the disassembly view advances by decoded length, so a one-byte `RET` is followed by the next instruction; `write_flags` cannot clear `K`; the demo loads the fixture by `.k232` extension and by `--kr232`; the golden matches; `krdbg_platform_boundary` reports clean; the host gate passes on the changed rows. The adapter build is reported separately, because the repository gate builds adapter-OFF.

---

## 10. M5 — kernel and user mode (after 1.x)

**Goal.** A user program runs under protection and is debuggable through a kernel.

**Preconditions.** 1.x is frozen; the two deferred questions are the ones this milestone answers.

### Tasks

- [ ] `[kr232]` Kernel in `targets/kr232/kernel/`, built by a custom target that invokes `krc` from the prefix and links with `krld`, guarded by `KR232_BUILD_KERNEL` (default OFF so M1–M3 need no compiler): vector table at `0x0100`, a trap handler that resumes `SYSCALL`, and `IRET` into user mode with a constructed frame. Check: a trap reaches the handler and returns; the layout check confirms the host build never compiles it, and the kernel target's command line names `krc`.
- [ ] `[kr232]` Syscall table: `write`, `read`, `exit`, blitter copy/fill, graphics commands, with kernel-side validation of every user pointer. Check: an invalid pointer is rejected without trapping the kernel.
- [ ] `[kr232]` `--user <program.k232>` on the CLI: preload at `0x08000000`, no entry word, the kernel builds the frame. Check: the kernel enters the program at its link address.
- [ ] `[krcompiler]` A user-mode program built by krc that prints, draws and exits through the table. Check: it runs and its frame matches a golden.
- [ ] `[kr232]` Protection from user mode: the M1 trap tests rerun with K = 0. Check: each raises vector 8 or 4 as documented.
- [ ] `[krdbg]` `host.kernel_supported` and `load_kernel`, replacing the session target. Check: the debugger loads kernel then program and steps the program through a syscall.
- [ ] `[kr232]` Answer the two deferred questions and revise the drafts: `SYSCALL` error values in ISA §18, display call numbers in devices §14; both sections lose "deferred" and gain the decisions in §15. Check: the lint passes and the revision is recorded.
- [ ] `[kr232]` Log the gate. Check: `## Gate verification` block.

### Tests

The kernel's entry and return, the syscall table's validation, protection from user mode, the user program's output and frame, and the debugger's kernel-plus-program session.

### Gate

> **Plan §10.** A kernel loads a user image with `--user`, enters it through a constructed trap frame with `IRET` and K = 0, and the program writes to the console, draws and exits with a status the kernel returns through `SYS exit`; a user-mode access below `0x08000000` and a kernel-only instruction both reach the kernel's handler; the same program is stepped in krdbg at source level with its registers and stack visible; the two §12 questions are answered in the ISA and devices drafts.

---

## 11. Cross-milestone risks that change the work

- **The VM gates the compiler tests.** `KRC_KR232_VM` must resolve from M2's gate onward, or krcompiler's executed rows silently vanish. The M2 task that adds the cache variable includes the deliberate-failure check for exactly this reason.
- **Size-exact copies.** A record whose size is not a multiple of four must be copied byte-exactly; the three-`char` test is the only thing that proves it.
- **R6 is loaded last.** Any argument that itself returns an aggregate overwrites the destination register.
- **The typeinfo word is reserved.** Emitting vtables without `[vptr - 4]` moves every virtual slot when `dynamic_cast` arrives.
- **One id counter.** `kROTargetId` and the KDBG `target_id` are the same number; 4 is KR216.
- **The container crosses two repositories.** The writer is krcompiler's, the loader is kr232's; the M0 ADR is the only thing keeping them equal.
- **`has_rtti` is documentation.** A `dynamic_cast` compiles and fails at link; the M3 fixture must not use one.
- **Goldens are x64-only.** The ARM64 row records "excluded", never "passed".
- **The two compilers never share a directory.** Host code is `src/` and `include/`; guest code is `targets/kr232/` and the sysroot headers. A host target that globs `targets/` compiles guest code with the wrong compiler, and the failure is either a link error that names nothing useful or a host-built binary that misbehaves only under the VM.
- **Two hosts must not share a tree.** A build directory written by MSVC is unreadable by GCC, and a source path baked into debug data makes the last builder decide what the record means. WSL builds from the ext4 mirror; every artifact is prefix-mapped to `/src`; the path-hygiene test is the guard.

## 12. Status

| Milestone | State | Gate recorded |
|---|---|---|
| M0 | in progress — 2 of 7 tasks | — |
| M1 | not started | — |
| M2 | not started | — |
| M3 | not started | — |
| M4 | not started | — |
| M5 | not started | — |

## 13. Log

### 2026-11-22T19:49:28-08:00 — M0: README added, sibling remotes checked

Host: CROSBY (Windows; documentation only, no build)
Changed: `README.md` added; §2's branch rule corrected — krcompiler's default branch is `master`, not `main`; M0 task 1 ticked.
Ran: `node tools/check-isa.mjs` → `ok: 81/81 vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; 13 device page rows tile the window; tables well-formed`. `git -C ..\krcompiler remote -v` and `git -C ..\krdbg remote -v` → both have push remotes; both trees clean on `master` and `main` respectively.
Failed: nothing.
Next: ADR and `docs/abi-kr232.md` in krcompiler, on branch `kr232-m0`.

### 2026-11-22T19:50:32-08:00 — M0: ADR 0057 written; the id split found and fixed

Host: CROSBY (Windows; documentation only, no build)
Changed: `..\krcompiler` branch `kr232-m0` created; ADR `0057-kr232-target-profile.md` added and indexed (`972294b`). The plan moved to 0.18 and this file to match: M0 records id 5 in the ADR and `symbols.md`, and the `kROTargetId` case moves to M2.
Found: the M0 check "`kROTargetId` gains a case" cannot be done before `TargetId::Kr232` exists, and adding the enumerator in M0 would force `to_string`, `to_target` and every switch over the enum to handle a target with no profile — the `-Wswitch` failure KR216 hit (`implementation.md`, 2026-11-22T00:41). The plan's gate text was wrong; corrected rather than worked around.
Ran: `node tools/check-isa.mjs` → ok, unchanged (no draft edited).
Failed: nothing.
Next: the ADR's Appendix A row, `docs/abi-kr232.md`, then the failing test.

## 14. Gate verification

*(One block per gate, stamped with the first build's start, never rewritten.)*
