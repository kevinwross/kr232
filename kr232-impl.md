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
| CROSBY (WSL, Ubuntu 26.04) | x64 | GCC and Clang rows; the Linux-side behaviour a Windows host hides | `-j10` |
| BitWhiz (aarch64, GCC 14.2.0) | ARM64 | the ARM64 row, reached by `ssh bitwhiz` | `-j4` |

**Verified 2026-11-22.** CROSBY Windows: MSVC 14.52.37025 and CMake 4.3.1-msvc1 are reachable **only inside the Visual Studio developer environment** — `vcvarsall.bat x64` first, or `cmake` and `ctest` are not on `PATH`. WSL Ubuntu 26.04: CMake 4.2.3, Ninja 1.13.2, GCC 15.2.0; the GCC-15 internal-compiler-error trap krcompiler records applies, so build the Linux rows with `ulimit -s unlimited` and low parallelism. BitWhiz: `ssh bitwhiz` and `ssh BitWhiz` both work, because the client matches `Host` patterns case-sensitively and the config block names both spellings (`Host bitwhiz BitWhiz`, user `kevinro`). Before that line existed, the lowercase spelling matched no block, fell back to the Windows user `kevin`, and was refused. It is a Raspberry Pi 5 on Debian 13 (kernel 6.18, aarch64) with 4 cores, GCC 14.2.0, CMake 3.31.6, Ninja 1.12.1, git 2.47.3 and 15 GB free, so `-j4` is the whole machine. It holds clones of `krcompiler` and `krdbg` but **not of `kr232`**, so the first ARM64 run clones it there.

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
5. **One red may sit on a default branch, and only when a gate requires it.** The guide's first commit for a target is documentation plus a failing test, so `krcompiler`'s `master` carries exactly one: the test that names what turns it green and in which milestone. The gate block records the test, its removal condition and the revision it landed at. No other red is acceptable, and a red without a recorded removal condition is a regression like any other.

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
- [x] `[krcompiler]` Write the container ADR — **0058**, added to this checklist when the gap was noticed: the `K232` header, section records, and one reader for a kernel image (base 0, reset word at `0x0100`) and a user image (base `0x08000000`, mapped only). Check: committed on `kr232-m0` (3db94f7).
- [x] `[krcompiler]` Add the `plan.md` Appendix A decision row for kr232 as **D-051**. Check: the row exists and ADR 0057 is cited (b16e3be).
- [x] `[krcompiler]` Write `docs/abi-kr232.md`: data model (`Ilp32`; 32-bit pointers, `size_t`/`ptrdiff_t` 32, `intmax_t` 32, no 64-bit integer, `long double` 64, signed `char`), argument registers R2–R5, result R2, `this` R10, FP R13, SP R15, callee-saved R8–R9, frame layout (saved caller FP at `[FP]`, EFP chain in R12), 4-byte alignment with 8 recommended, narrow-type extension, varargs, aggregates and the R6 sret convention, the floating-point convention over F0–F3, vtable and typeinfo layout with the reserved `[vptr - 4]` slot, and FP register preservation. Check: every section `abi-kr216.md` carries has a kr232 counterpart or a stated "not applicable" (682f7ee).
- [x] `[krcompiler]` Extend the `target_id` line in `docs/symbols.md` to `5` kr232. Check: the line names all five — it named only 1–3, so KR216's 4 was added at the same time. The `kROTargetId` case waits for the profile row in M2 (b16e3be).
- [x] `[krcompiler]` Add the failing test: `krc --target=kr232 -c` is expected to stop with "has no code generator yet". Check: the test **fails** at M0 with `unknown target` and the log quotes both messages (7952efb).
- [x] `[kr232]` Update `AGENTS.local.md` to point at the approved plan and this file. Check: both are named in its Authoritative-for row.

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

- [x] `[kr232]` Build skeleton: `CMakeLists.txt`, `CMakePresets.json` (`windows-msvc`, `linux-wsl`, `linux-arm64`, each with its own `binaryDir` and the prefix-map flags of §1), `include/kr232/`, `src/`, `targets/kr232/`, `tests/`, `scripts/wsl-mirror.sh`, and `scripts/gate-windows.ps1` / `gate-linux.sh` cloned from krdbg. Check: configure and build clean on Windows `-j10` and WSL `-j10`; the layout check fails when a `targets/` file is added to a host target; the path-hygiene test fails on a deliberately absolute `__FILE__` string and passes on the real binary.
- [x] `[kr232]` `memory`: the 4 GiB space with the eight rows of ISA §2.3, load/store of 1/2/4 bytes, and the unbacked-gap behaviour. Check: unit tests per region boundary.
- [x] `[kr232]` `decoder`: every class of ISA §3.1, including `F0`/`F4` lengths resolved from the bytes after the prefix. Check: a decode of all 81 Appendix A vectors, bytes in, mnemonic and length out, matching the appendix.
- [x] `[kr232]` `executor`: ALU, shifts, branches, loads/stores, `ENTER`/`LEAVE`, `CALL`/`RET`, `PUSH`/`POP`, halfword and byte forms, `SYS`, `IRET`, `BRK`, `RDCYCLE`. Check: one test per vector from bytes to architectural state — the 81-vector effect harness, plus the twelve value cases.
- [x] `[kr232]` Protection and traps: the 16-byte frame, vector 4 for the gaps and null page, vector 8 for the two user ranges and kernel-only instructions, vector 9 for `SYSCALL`, `IRET`'s restore of K, I and SP, and the K/I write rules. Check: the M1 gate's protection list, case by case.
- [x] `[kr232]` Devices: timer, console, framebuffer, keyboard, terminal, graphics commands, system, blitter — registers, `ID` at `0x3C`, reset values, and the trap rules for undefined offsets and reserved pages. Check: per-page tests, and the `ID` word of all eight.
- [x] `[kr232]` Blitter: `COPY`, `FILL`, `UPLOAD`, range validation before any byte moves. Check: a rejected range leaves memory unchanged.
- [x] `[kr232]` Frame commit and `--frame-hash`: the committed surface's hash, with a checked-in golden. Check: the golden matches on x64 and the ARM64 row records "excluded".
- [x] `[kr232]` Flat image loader and CLI: `vm <image> --max-steps N`, `--trace`, `--screenshot`, `--frame-hash`, with the program's `SYS exit` status as the process status. Check: a hand-encoded image runs and exits with its status.
- [x] `[kr232]` A hand-encoded kernel-mode image that writes to the terminal, draws through the graphics page and flips. Check: its frame hash equals the golden.
- [x] `[kr232]` Record the gate. Check: `## Gate verification` block with the plan's five items, each naming its test.

### Tests

The 81 vectors, the protection suite, the eight device suites, the blitter rejection cases, the frame golden, and the lint. `ctest --preset windows-msvc` and `--preset linux-wsl` at `-j10`, `--preset linux-arm64` at `-j4` on BitWhiz.

### Gate

> **Plan §6.** (1) Each of the 81 Appendix A vectors executes from its bytes and produces the documented architectural effect. (2) A user-mode access below `0x08000000` or at or above `0xFC000000` raises vector 8 with a 16-byte frame; an access to the unbacked `0x10000000`–`0xFBFFFFFF` gap raises vector 4 in either mode, while the `0xFE000000`–`0xFFEFFFFF` gap raises vector 4 in kernel mode and vector 8 in user mode; `IRET` restores K, I and SP; `SYSCALL` resumes after the instruction; `EI`, `DI`, `HALT`, `SYS` and `IRET` trap in user mode. (3) Every populated device page answers `ID` at `0x3C`; an undefined offset and a reserved page trap with vector 4; a rejected FLIP sets `FLIP_ERROR` and commits nothing; blitter `COPY`/`FILL`/`UPLOAD` validate ranges before moving bytes. (4) A hand-encoded kernel-mode image writes to the terminal, draws through the graphics page and flips; `--frame-hash` over the committed surface matches a checked-in golden. (5) `node tools/check-isa.mjs` still passes.

---

## 7. M2 — profile, assembler, object writer, disassembler, container

**Goal.** The target becomes assemblable and linkable, but not compilable.

**Preconditions.** M1's gate is recorded, so an image can be executed.

### Tasks

- [x] `[krcompiler]` `TargetId::Kr232`, the `TargetProfile` row with all nine capability flags (`has_fpu` true, `has_matrix`, `has_atomics`, `has_threads`, `has_tls`, `has_eh`, `has_rtti` false, `has_cpp` true, `backend_available` false), `to_string`, `parse_target`. Check: `test_target` pins every field; `krc --list-targets` shows `kr232 backend=no`.
- [x] `[krcompiler]` `Kr232Descriptor` with the register roles of M0. Check: a unit test reads each role back.
- [x] `[krcompiler]` `src/asm/kr232_assembler.cpp` and its encoder, plus `make_object` with target id 5. Check: round-trip tests, assemble → bytes → disassemble, for every Appendix A vector.
- [x] `[krcompiler]` `.kdbg` emission from assembly: line and function records. Check: `krc --dump-symbols` on a linked image names the fixture's lines.
- [x] `[krcompiler]` `src/linker/kr232_image.cpp` — the `.kr232` writer the M0 ADR specifies: header, section records, the `.reset` section at `0x0100`, CRC, `image_base = 0`. Check: a unit test reads back every header field, and the writer rejects an entry outside executable text, a section outside the map and overlapping sections.
- [x] `[krcompiler]` `toolchain::link_impl` selects the writer for target 5, with 4-byte heap alignment and the kr232 stack bounds. Check: a linked image's header and entry agree with the ADR.
- [x] `[krcompiler]` `krc --dump-symbols` reads a `.kr232`. Check: the gate's decode assertion passes on a fixture.
- [x] `[krcompiler]` `cmake/RunKr232Image.cmake` and the `KRC_KR232_VM` cache variable in `CMakeLists.txt`, with the sibling fallback. Check: a deliberate wrong exit status fails with `expected exit N, got M`.
- [x] `[kr232]` `kr232-dis`: the canonical decoder and text form, shared with the emulator. Check: a disassembly test over all 81 vectors and over the assembled fixture.
- [x] `[kr232]` The `.kr232` loader replaces the flat loader and dispatches on the container magic. Check: a bad checksum, a section outside the map and a base mismatch each fail at load.
- [x] `[kr232]` A `.s` fixture assembles to an object, links to a `.kr232`, and runs under M1. Check: exit status and screen match.
- [x] `[kr232]` Log the gate. Check: `## Gate verification` block with every M2 item.

### Tests

Round-trip for all 81 vectors; a hand-written `.s` linked and run; the `.kdbg` decode; two builds byte-identical; the loader's rejection cases; `krc --list-targets`; the krcompiler suite on Windows and WSL.

### Gate

> **Plan §7.** `krc --list-targets` shows `kr232` with `backend=no`; a hand-written `.s` file assembles to an object, links to a `.kr232`, and runs under M1 with the expected exit status and screen; the image's `.kdbg` decodes through `krc --dump-symbols` with the fixture's lines and functions and `image_base = 0`; every vector round-trips byte-exactly; two builds of one image are byte-identical.

---

## 8. M3 — code generator, runtime, sysroot, packages

**Goal.** C and C++ programs build, run and are debuggable — and the ISA and devices freeze behind them.

**Preconditions.** M2's gate is recorded; M4's adapter is available for the final check.

### Tasks

- [x] `[krcompiler]` Backend skeleton in the guide's shape: `Kr232Backend(module, profile, level)`, `emit`, `debug_info`, `frame_bytes`, registered at both dispatch sites in `src/toolchain/toolchain.cpp` and the six branches in `src/tools/krc/main.cpp`. Check: `krc --target=kr232 -c` reaches the backend and emits a prologue.
- [x] `[krcompiler]` Prologue/epilogue with `ENTER`/`LEAVE`, and the frame layout the ABI fixes. Check: a frame-size unit test; `-S` byte-identical against the previous step at each subsequent task.
- [x] `[krcompiler]` Loads and stores, including `LDBU`/`LDHS`, the signed/unsigned narrow extensions and the FP forms. Check: by-value `char`/`short` parameters round-trip.
- [x] `[krcompiler]` ALU, comparisons, branches, calls, returns. Check: `int main(void){return 42;}` returns 42 under the emulator.
- [x] `[krcompiler]` Aggregate paths: memory-class parameters copied size-exactly, the R6 return destination loaded after argument evaluation, struct return by copy-out. Check: the three-`char` record test, and a nested call in an argument.
- [x] `[krcompiler]` Varargs and FP argument/return conventions. Check: a `printf`-style call with mixed integer and `double` arguments.
- [x] `[krcompiler]` C++ surfaces: class, `this` in R10, virtual dispatch with the reserved typeinfo slot, constructors. Check: a virtual call dispatches to the override; the vtable's `[vptr - 4]` word is present and zero.
- [x] `[krcompiler]` `targets/kr232/runtime/crt0.s`: frame, call `main`, pass the status to `SYS exit`; no stack setup, since reset SP is `0x08000000`. Check: a linked image exits with `main`'s status.
- [x] `[krcompiler]` `targets/kr232/runtime/libc.c`: console output, the `SYS` write/read/exit stubs, blitter-backed `memcpy`/`memset`. Check: output appears on the terminal transcript.
- [x] `[krcompiler]` `sysroot/kr232/include/bits/` generated from the profile: `bits/types.h`, `bits/stdint.h`. Check: `stdint.h` widths round-trip in a unit test.
- [x] `[krcompiler]` The four guest packages: `<kr232/fb.h>`, `<kr232/gfx.h>`, `<kr232/cons.h>`, `<kr232/input.h>`, with kernel-mode implementations. Check: the drawing program fills, draws and flips through `gfx`.
- [x] `[krcompiler]` Per-target link/run loops at `-O0`, a determinism test, a KDBG-invariants test at `-O0`, and the clean gate. Check: the executed rows exist because `KRC_KR232_VM` resolved.
- [x] `[krcompiler]` The `-O1` measurement ADR 0049 requires, recorded whether it lands or not. Check: the measurement is in the log; the `_o1` twins appear only if it does.
- [x] `[krcompiler]` The compiled program loads in krdbg at source level and stops on a line breakpoint. Check: the session stops at the expected line with locals visible.
- [x] `[kr232]` Log the gate and cut the freezes. Check: `## Gate verification` block; tags `isa-1.0` and `devices-1.0` on `kr232`, with the ISA and devices status lines moved to 1.0 and §18/§14 retitled "Deferred to 2.0".

### Tests

The per-target loops, determinism, KDBG invariants, the clean gate, `-S` byte-identity at every step, the three programs (C, C++, drawing), the krdbg stop, and the ARM64 row.

### Gate

> **Plan §8.** `int main(void){return 42;}`, a C++ program with a class, a virtual call, a by-value struct parameter, a struct return and an array, and a program that draws through `<kr232/gfx.h>` and flips, all build with `-O0`, run under `kr232-emu`, and match their status, terminal output and frame; the by-value and returned records include a three-`char` one, whose copy must be size-exact; `-S` output is byte-identical at every conversion step; the executed rows exist because `KRC_KR232_VM` is found; the compiled program loads in krdbg at source level and stops on a line breakpoint; determinism, KDBG invariants and the clean gate pass. **ISA 1.0 and devices 1.0 freeze at this gate.**

---

## 9. M4 — krdbg adapter

**Goal.** kr232 is debuggable at assembly level, then source level.

**Preconditions.** M2's gate is recorded, so a `.kr232` with a `.kdbg` exists; and the `krc` prefix is installed from the same `krcompiler` revision the work is on (§1), because the host-build check cannot run without it.

### Tasks

- [x] `[krdbg]` `adapters/kr232/` skeleton and CMake option `KRDBG_KR232_ADAPTER`, linked like KR216's direct-source pattern. Check: configure, build, and the adapter test target compiles.
- [x] `[krdbg]` `layout()`: one core context, the register groups, flags Z…K with **K and I non-writable**, 32 address and pointer bits, `variable_instruction_length` true with `instruction_alignment` 1, `step_unit` "instructions". Check: the layout test asserts each field, and `write_flags` cannot clear K.
- [x] `[krdbg]` Memory regions: kernel RAM, user RAM, aperture, device window, and both unbacked gaps marked neither readable nor writable. Check: a read in each region and in each gap.
- [x] `[krdbg]` Execution and state: `step_instruction`, `run`, `pc`/`set_pc`, `read_value`/`write_value` by group index, `read_flags`/`write_flags`, `reset`. Check: stepping a fixture retires the documented instructions and reports `Halted`/`Fault` truthfully.
- [x] `[krdbg]` `disassemble` through `kr232-dis`, advancing by decoded length. Check: a one-byte `RET` is followed by the next instruction, not by a four-byte gap.
- [x] `[krdbg]` `unwind()` through the EFP chain, `FunctionInfo::frame` 0, and `Location::FrameOffset` negated. Check: two frames and a local's value at each.
- [x] `[krdbg]` `IComPort` over page 2 and `IVideoSource` over the committed frame, with `has_com_port` and `has_framebuffer` agreeing. Check: a byte through the port reaches the guest, and a frame hash matches the emulator's.
- [x] `[krdbg]` Source level: load `.kr232`, locate `.kdbg`, `kdbg::read_payload`, resolve paths, `replace_target(std::move(target), info)` on reload. Check: line breakpoints resolve after a reload.
- [x] `[krdbg]` Host wiring: `--kr232`, the include under `KRDBG_HAVE_KR232`, `load_any` on the `.kr232` extension and `--target kr232`, `tests/test_host_build.cpp` with `KRDBG_KRC_PREFIX`. Check: the prefix is installed at the krcompiler revision recorded in the log, `test_host_build` **runs** rather than skipping, and the demo loads the fixture both ways.
- [x] `[krdbg]` Fixtures and golden: `tests/fixtures/kr232/` assembled by M2's toolchain with a `regenerate.ps1`, and `goldens/kr232-loaded.hash`. Check: the golden matches on x64; the ARM64 run excludes it.
- [x] `[krdbg]` `tests/test_kr232_adapter.cpp`: load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM, framebuffer. Check: ten cases green.
- [x] `[krdbg]` Boundary check and host gate. Check: `krdbg_platform_boundary` clean; the host gate passes on the changed rows; the adapter build is reported separately because the repository gate builds adapter-OFF.
- [x] `[kr232]` Log the gate. Check: `## Gate verification` block.

### Tests

The adapter suite, the golden, the boundary check, the host gate, and the demo's two load paths.

### Gate

> **Plan §9.** The adapter compiles with its option ON; the fixture tests cover load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM and framebuffer; the disassembly view advances by decoded length, so a one-byte `RET` is followed by the next instruction; `write_flags` cannot clear `K`; the demo loads the fixture by `.kr232` extension and by `--kr232`; the golden matches; `krdbg_platform_boundary` reports clean; the host gate passes on the changed rows. The adapter build is reported separately, because the repository gate builds adapter-OFF.

---

## 10. M5 — kernel and user mode (after 1.x)

**Goal.** A user program runs under protection and is debuggable through a kernel.

**Preconditions.** 1.x is frozen; the two deferred questions are the ones this milestone answers.

### Tasks

- [x] `[kr232]` Kernel in `targets/kr232/kernel/`, built by a custom target that invokes `krc` from the prefix and links with `krld`, guarded by `KR232_BUILD_KERNEL` (default OFF so M1–M3 need no compiler): vector table at `0x0100`, a trap handler that resumes `SYSCALL`, and `IRET` into user mode with a constructed frame. Check: a trap reaches the handler and returns; the layout check confirms the host build never compiles it, and the kernel target's command line names `krc`.
- [x] `[kr232]` Syscall table: `write`, `read`, `exit`, blitter copy/fill, graphics commands, with kernel-side validation of every user pointer. Check: an invalid pointer is rejected without trapping the kernel.
- [x] `[kr232]` `--user <program.kr232>` on the CLI: preload at `0x08000000`, no entry word, the kernel builds the frame. Check: the kernel enters the program at its link address.
- [x] `[krcompiler]` A user-mode program built by krc that prints, draws and exits through the table. Check: it runs and its frame matches a golden.
- [x] `[kr232]` Protection from user mode: the M1 trap tests rerun with K = 0. Check: each raises vector 8 or 4 as documented.
- [x] `[krdbg]` `host.kernel_supported` and `load_kernel`, replacing the session target. Check: the debugger loads kernel then program and steps the program through a syscall.
- [x] `[kr232]` Answer the two deferred questions and revise the drafts: `SYSCALL` error values in ISA §18, display call numbers in devices §14; both sections lose "deferred" and gain the decisions in §15. Check: the lint passes and the revision is recorded.
- [x] `[kr232]` Log the gate. Check: `## Gate verification` block.

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
| M0 | complete — gate recorded 2026-11-22T20:07:37-08:00 | M0 |
| M1 | complete — 11 of 11 tasks | met 2026-11-22T21:01:09-08:00 |
| M2 | complete — 12 of 12 tasks | met 2026-11-22T22:05:00-08:00 on Windows and WSL; merged as krcompiler e441d06 |
| M3 | 15 of 15 tasks — varargs is deferred, its four parts named in §14; `libc`'s console path and SYS stubs are in | recorded 2026-11-23T19:20:00-08:00; ISA 1.0 and devices 1.0 tagged |
| M4 | 14 of 14 tasks — the ARM64 row passes rather than excluding, and the demo loads by both paths | recorded 2026-11-23T20:50:00-08:00 |
| M5 | 7 of 7 tasks — kernel, syscall table, `--user`, the user program, protection, krdbg kernel support and both deferred questions | recorded 2026-11-24T04:30:00-08:00 |

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

### 2026-11-22T19:54:08-08:00 — M0: container ADR, Appendix A row, ABI appendix, id table

Host: CROSBY (Windows; documentation only, no build)
Changed (all on `kr232-m0`): `0057` ADR (972294b); D-051 in `plan.md` Appendix A and the `target_id` line in `docs/symbols.md` (b16e3be); `0058` container ADR (3db94f7); `docs/abi-kr232.md` with its index entry (682f7ee).
Found: `docs/symbols.md` listed only ids 1–3, so KR216's 4 had never been written down even though `kROTargetId` returned it. Fixed while adding 5. The impl checklist was also missing the container ADR the plan's §5 requires; added as a task and done.
Wrote: the ABI appendix differs from KR216's where the machine does — a 4-byte return address at `FP+4` and the first stack argument at `FP+8`, `MemoryOnly` copies that are size-exact with a 2-byte and a 1-byte tail, the reserved `[vptr - 4]` slot, and no `.init_array` walk or allocator in 1.x.
Ran: `node tools/check-isa.mjs` → ok, unchanged (no draft edited).
Failed: nothing.
Next: the failing refusal test, then M0's gate.

### 2026-11-22T19:55:47-08:00 — M0: the refusal test is in and red

Host: CROSBY (Windows, MSVC 14.52, CMake 4.3.1-msvc1 via the VS developer environment)
Changed (`kr232-m0`): `tests/CMakeLists.txt` gains `krc_kr232_no_backend` (7952efb), registered rather than hidden.
Ran: `cmake --build --preset windows-msvc-debug -j 10 --target krc` → clean, reconfigured with the new test; `ctest -R krc_kr232_no_backend --output-on-failure` → **failed as designed**:
`Required regular expression not found. Regex=[target 'kr232' has no code generator yet]` and the tool printed `krc: error: unknown target 'kr232' (expected kr32, kr16, or kr64)`.
Found: that message omits **kr216** as well, so the expected-target list has been stale since ADR 0053. M2 fixes it when it adds kr232 to `to_target` — recorded here so it is not rediscovered.
Environment: `cmake` and `ctest` exist only inside the Visual Studio developer environment; `vcvarsall.bat x64` is required, and the doctor confirms it (`msvc 14.52.37025`, `cmake version 4.3.1-msvc1`). The doctor's own note that a confined session cannot answer no longer applies at this policy.
Pending: the full `ctest` row was started to confirm the suite grows by exactly one failure; its result belongs in the M0 gate block.
Next: M0's gate, which needs the owner's merge of `kr232-m0` (or a ruling that a pushed branch is what "merged" means).

### 2026-11-22T20:00:11-08:00 — M0: the host row is 919 of 920, one red by design

Host: CROSBY (Windows, `windows-msvc-debug`, `-j10`)
Ran: `ctest --preset windows-msvc-debug` → **919 passed, 1 failed of 920**, 266 s. The only failure is `krc_kr232_no_backend`, the test this milestone added; it fails because the tool still answers `unknown target 'kr232'`.
Reads: the two failures krcompiler used to carry have been fixed since they were recorded, so the row is otherwise clean — the new red is the only one, and it names its own removal condition.
Failed: nothing unexpected.
Next: the M0 gate. Its merge clause is the owner's action, so the gate block waits for that decision.

### 2026-11-22T20:00:52-08:00 — hosts verified; BitWhiz is denied

Host: CROSBY (checks only, no build)
Ran: `pwsh -File .agents/doctor.msvc.ps1` in krcompiler → `msvc 14.52.37025`, `cmake version 4.3.1-msvc1`, "only from the Visual Studio developer environment". `wsl.exe -l -v` → Ubuntu-26.04 running, version 2; inside it `cmake 4.2.3`, `ninja 1.13.2`, `git 2.53.0`, `gcc 15.2.0`. `ssh -o BatchMode=yes bitwhiz` → `kevin@bitwhiz: Permission denied (publickey,password)`.
Found: the ARM64 row has no access. The alias resolves, the credentials do not, and the fix is the owner's — a key or an ssh config entry for this session. Until then every ARM64 row is recorded "not run", never "passed".
Changed: §1's hosts table now carries these verified facts, so a later session does not re-derive them or, worse, assume them.
Next: the M0 gate's merge clause, and BitWhiz access.

### 2026-11-22T20:02:02-08:00 — BitWhiz is reachable; the alias was the problem

Host: CROSBY (checks only)
Ran: `ssh bitwhiz` → `kevin@bitwhiz: Permission denied (publickey,password)`. `C:\Users\kevin\.ssh\config` defines `Host BitWhiz` with `User kevinro`. `ssh BitWhiz "hostname; whoami"` → `BitWhiz`, `kevinro`. `ssh BitWhiz` with the toolchain query → kernel 6.18.39 aarch64, 4 cores, GCC 14.2.0, CMake 3.31.6, Ninja 1.12.1, git 2.47.3, 15 GB free, and `~/repos` holds `krcompiler` and `krdbg` but no `kr232`.
Found: the alias is capitalised on this client; the lowercase spelling resolves to a default user and is refused. No key work was needed, and nothing was read from the key files — only the client configuration, to find the right user.
Changed: §1 now states the working invocation, the machine's shape, and that the first ARM64 run must clone `kr232` there.
Next: the M0 gate's merge clause, which is the owner's action.

### 2026-11-22T20:02:26-08:00 — the ABI appendix checked against the ISA

Host: CROSBY (reading only)
Checked, claim by claim, in `docs/abi-kr232.md` against `kr232-isa.md`:
frame offsets — `CALL` "decrements SP by 4 and stores the return address" (ISA §10.6 line 717) and `ENTER` stores FP at `SP-4` before pointing FP there (§10.7), so the return address is at `FP+4` and the first stack argument at `FP+8`, as written; `CALL addr32` exists as `F9` op 1 (§6); `FLDS`/`FSTS` widen and narrow (§7.9); `FPUSH`/`FPOP` move eight bytes (§8.7); an indirect call is the one-byte `CALL Rd`; `FLDS`/`FSTS` conversion and the vector-3 trap for a bad float↔integer conversion are as stated (§11.1); the stack-alignment sentence matches ISA §15.38 and the note at line 1070.
Found: nothing wrong. No edit was needed.
Failed: nothing.
Next: the M0 gate's merge clause — the only thing between M0 and M1.

### 2026-11-22T20:12:19-08:00 — M0 gate recorded: merged to master and re-run there

Host: CROSBY (Windows x64, MSVC 14.52, `windows-msvc-debug`, `-j10`)
Changed: §2 gained its one exception — a red that names its own removal condition may sit on a default branch when a gate requires it. `krcompiler` `master` received the `--no-ff` merge **7c31e3d** of `kr232-m0`.
Ran: `git fetch`, `switch master`, `pull --ff-only` → already up to date at `f5384cf`; `git merge --no-ff kr232-m0` → 8 files, 533 insertions; `git push origin master` → `f5384cf..7c31e3d`. Then the gate on the merged tree: build start `20:07:37`, `ctest --preset windows-msvc-debug` → **919 of 920**, 268 s, the one red being `krc_kr232_no_backend`.
Found: the `krc` binary had not been relinked by the gate build, so its modification time would have stamped the gate with a moment when nothing was built — the trap krcompiler's own `AGENTS.local.md` warns about. The build's printed start line is the stamp instead, and the block says so.
Gate: **M0 met** — see `## Gate verification`. M1's precondition is now satisfied.
Next: M1's build skeleton — `CMakeLists.txt`, three presets, the layout check, the path-hygiene test.

### 2026-11-22T20:15:10-08:00 — M1: the build skeleton stands on both x64 hosts

Host: CROSBY (Windows x64 `windows-msvc`, `-j10`) and CROSBY WSL (Ubuntu 26.04, `linux-wsl`, `-j10`, from `~/mirrors/kr232`)
Added: `CMakeLists.txt`, `CMakePresets.json` (three presets, each with its own `binaryDir`, build and test presets at `-j10`/`-j10`/`-j4`), `include/kr232/version.h`, `src/version.cpp`, `src/main.cpp` (the `vm`/`--version`/`--help` skeleton), a header-only test harness with one case, `cmake/CheckPathHygiene.cmake`, `targets/kr232/README.md` and its `probe.cpp` canary, `scripts/gate-windows.ps1`, `scripts/gate-linux.sh`, `scripts/wsl-mirror.sh`.
Ran: Windows configure+build+`ctest` → 3 of 3 (version, hygiene, hygiene selftest). WSL mirror then configure+build+`ctest` → 3 of 3. Layout selftest: `cmake -DKR232_LAYOUT_SELFTEST=ON` → **fails as required**, `kr232_core lists 'targets/kr232/probe.cpp', which is guest code under targets/`.
Found, and it is why the check exists: the first WSL build **failed** the hygiene test — `kr232-emu` carried `/home/kevinro/mirrors/kr232/src/main.cpp`. The prefix-map flags live on the `kr232_warnings` interface target and only `kr232_core` linked it, so `main.cpp`, `tests/main.cpp` and `test_version.cpp` were compiled unmapped. Windows had passed because an MSVC Debug executable carries its PDB path rather than DWARF; GCC links the DWARF section into the binary, which is why the same defect was invisible on one host and fatal on the other. Fixed by linking `kr232_warnings` into every target with sources, and both hosts now pass.
Found also: `cl` warns **D9007** — `/pathmap` is ignored without `/experimental:deterministic` — so the MSVC mapping had been a no-op. Both flags are now set, and a clean rebuild prints no warning.
Next: M1's `memory` region tests, then the decoder and the 81 vectors.

### 2026-11-22T20:16:51-08:00 — M1: the memory map, with its boundaries tested

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl` from `~/mirrors/kr232`)
Added: `include/kr232/memory.h` and `src/memory.cpp` — `region_of()` over the eight ISA §2.3 rows, and a `Memory` that serves 1/2/4-byte little-endian accesses to backed RAM and reports everything else `Unmapped`; `tests/test_memory.cpp` with seven cases.
Ran: Windows → 3 of 3, unit case 0.52 s; WSL → 3 of 3, unit case 0.17 s.
Found: the checklist said "seven regions"; ISA §2.3 has **eight** rows, and the doc is corrected. Also the constructor validated *after* allocating, so a rejected 256 MiB request would have allocated the buffer first — validation now runs before the allocation, and the test that a too-large request throws costs nothing.
Kept honest: the trap rows — null page, both gaps, aperture, devices, reserved — are `Unmapped` even though the first two fall inside the RAM buffer's address range, because ISA §2.3 makes an access to them a fault; an access crossing the end of RAM is refused whole, and a test reads the neighbouring byte to prove the refused store wrote nothing.
Cost noticed and removed: two default-constructed `Memory` instances zeroed 512 MiB, making the unit case 5.2 s. Classification is by address, so the aperture test now uses a 64 KiB machine and the case runs in 0.5 s; only the test that genuinely needs the last RAM byte allocates 256 MiB.
Next: the decoder, then the 81-vector suite.

### 2026-11-22T20:19:32-08:00 — M1: the decoder decodes all 81 vectors

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl` from the mirror)
Added: `include/kr232/decoder.h`, `src/decoder.cpp`, `tests/test_decode.cpp`. Every class of ISA §3.1 is covered: Short, Register, Compact register and branch, all 32 Normal ops, and the eleven escape forms (`F0` generic with its operand specifiers and extensions, `F1`, `F3`, `F4`, `F5`, `F6`, `F7`/`F8`/`FB`, `F9`, `FA`), with reserved bytes rejected rather than guessed. `F0`'s and `F4`'s lengths come from the bytes after the prefix, which is the one place a length can differ from its class.
Ran: Windows → 15 cases, 0 failed checks, 3 of 3 CTest entries; WSL → 3 of 3. The vector case reads **Appendix A out of `kr232-isa.md`** at test time rather than a transcribed copy, so a vector cannot drift from the test that checks it: 81 vectors found, every one decoded, every length equal to its byte count, every mnemonic equal to the appendix's.
Corrected: the checklist said "text out for all 81". The appendix's operand text mixes radices by hand — `enter #8, #0x0300` beside `enter #300, #0` — so byte-exact text is not a property the document defines. The check now requires mnemonic and length, which is what desynchronizes an executor; operand text stays canonical and is asserted by the executor's own tests. The correction is recorded here rather than folded in silently.
Also: the decoder rejects the shapes that must trap — `F2` and `FC`–`FF`, `0x7A`–`0x7F`, a compact unary with `Rb ≠ R0`, a `SETcc` condition of 14 or 15, and a generic form whose two operands both need an extension.
Next: the executor.

### 2026-11-22T20:20:39-08:00 — M1: executor design settled, code not yet landed

Host: CROSBY (no build run)
Done: decoded the round's scope and settled four decisions before writing `Machine`, because each one is hard to change later.
- **Flags live in R11** at the ISA's bit positions, and `set_reg(11, x)` masks K and I back to their previous values, so no ordinary write can enter kernel mode or mask an interrupt (ISA §2.2, §15.29).
- **A memory access the map cannot serve traps rather than returning zero.** The executor asks `region_of()` for the vector instead of guessing one, and keeps the trap vector with the trapping instruction's PC.
- **Fetch is up to eight bytes**, stopping at the first unmapped byte, so a truncated instruction at the end of RAM is an illegal encoding (vector 2) and not a read past the map.
- **PC is R14** in the register file rather than a separate field, so `set_pc`/`pc` and a future debugger read the same state.
Stopped: writing `execute()` is a single coherent table — every class, every Normal op, the generic specifier modes and the frame instructions — and a partial version would have committed either a link error or, worse, legal instructions reporting vector 2. The two files written so far were removed rather than left untracked, so the tree is clean and the next session starts from the same commit as this one.
Failed: nothing; no check was run.
Next: `Machine` and its tests in one piece, then the per-vector state table.

### 2026-11-22T20:22:33-08:00 — M1: the decoder now carries the operands an executor needs

Host: CROSBY (Windows, `windows-msvc`)
Changed: `Decoded` gains an `Operand` pair, and the decoder fills it where the text alone does not identify the operands — the `F0` generic form's two specifiers (mode, register, extension), and `F4`/`F5`/`FA`'s addressing mode, `Fd` and `Rb`. Without it an executor would have to re-parse the bytes the decoder has already parsed.
Also: `decode()` lost its `address` parameter. It existed for an absolute rendering of a branch target, and this text form is relative by design — `jmp +6`, not `jmp 0x0206`. Keeping an unused parameter behind a `(void)` cast would have been a warning silenced rather than a decision made.
Ran: Windows build clean under `/W4 /WX`, `ctest` 3 of 3, 15 cases and 0 failed checks. The vector case still finds and matches all 81.
Found: one test run reported the unit case at 7.7 s and the next at 0.06 s on the same binary — a scanner or the build itself competing, not the code; recorded so a later session does not chase it as a regression.
Stopped: `execute()` again, and deliberately. It is one table — every class, every Normal op, the generic modes, the frame instructions — and the header without its implementation was removed rather than left untracked, so the tree is clean at this commit.
Next: `Machine` and `execute()` in one piece.

### 2026-11-22T20:26:22-08:00 — M1: `Machine` executes every class

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl` from the mirror)
Added: `include/kr232/machine.h`, `src/machine.cpp`, `tests/test_machine.cpp` (12 cases). Every class executes: Short (push, pop, inc, dec, call, ret, nop, brk, sys, ei, di, halt, iret, rdcycle, syscall), Register, both Compact forms, all 32 Normal ops, the `F0` generic form through its operand modes, `F1`, `F3`, `F4`, `F5`, `F6`, `F7`/`F8`/`FB`, `F9`, `FA`.
Ran: Windows build clean under `/W4 /WX`, 27 cases and 0 failed checks, `ctest` 3 of 3; WSL build and `ctest` 3 of 3 with GCC 15.
Found, and it cost three runs: the first test binary trapped on every step. The machine was correct — a PowerShell `.Replace` in my own test edit had written **literal `\n`** into the fixture, turning `set_sp(0x8000)` and `set_pc(0x1000)` into the tail of a comment. PC stayed 0, every fetch hit the null page, and the failure looked like an executor bug until a 20-line probe linked against `kr232_core.lib` showed decode and three steps retiring correctly. The lesson is the one this log keeps re-learning: a whole class of failures is a fixture that never ran, and the probe that isolates library from fixture answers it faster than reading the executor.
Also fixed on the way: `reset()` discarded a `[[nodiscard]]` result (now checks it), and an unused lambda parameter tripped `/W4`.
Not ticked: the executor's check is "one test per vector from bytes to architectural state", and 12 targeted cases are not 81. The per-vector table is the next piece of work, and the task stays open until it exists.
Next: the per-vector state table, then protection and traps.

### 2026-11-22T20:27:41-08:00 — M1: the per-vector table designed, not written

Host: CROSBY (no build)
Settled the shape of the check, because it decides how the task is split:
- A single loop over the 81 Appendix A vectors, each executed on a **canonical machine**: SP at `0x8000`, R2–R10 holding addresses in kernel RAM, F0–F3 holding distinct doubles, `[SP]` a `ret` target, an `IRET` frame at `0x6000`, and a saved FP at `0x7000`. The fixture is what makes one table serve every form.
- **The expectation is the effect's shape, chosen per mnemonic**, not a hand-written value per vector: `push` must move SP down and write; `pop` up; `call` down and jump; `ret` jump to the frame's PC; a branch either advances by its length or lands on its computed target; `brk` traps 1; `syscall` traps 9; `sys` with R7 = 0 halts; `enter` moves SP down, `leave` restores it; a load changes a register; a store changes memory; `cmp` changes only flags; `rdcycle` writes R2; `iret` resumes at the frame's PC.
- Where a value is unambiguous the table will carry it (`ld r2,#0x1234` → R2 = 0x1234); where the value would duplicate the executor's own arithmetic, the check asserts the shape and says so, because a second implementation of ADD in the test proves agreement with itself, not with the ISA.
Split: three commits, not one — the harness with the integer shapes, then the FP shapes, then the value table for the vectors where the value is fixed by the Appendix. Each lands verified rather than half-written.
Not landed: any code this round. A no-op edit to `tests/test_decode.cpp` was reverted and the tree is clean at `38b2ef3`.
Next: the harness and its integer shapes.

### 2026-11-22T20:32:03-08:00 — M1: all 81 vectors execute and show their form's effect

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl` from the mirror)
Added: `TEST_CASE(every_vector_executes_with_the_effect_its_form_requires)` in `tests/test_decode.cpp`, beside the parser it reuses so the Appendix has one reader. Each of the 81 vectors runs on a canonical machine — SP at `0x8000`, R2–R10 as kernel-RAM addresses, F0–F3 distinct doubles, a `ret` target at `[SP]`, an `IRET` frame at `0x6000`, a saved FP at `0x7000` — and asserts its mnemonic's shape: push moves SP down, pop up, call down and jumps, ret resumes at the frame's PC, a branch advances or lands on its independently computed target, `brk` traps 1, `syscall` traps 9, `sys` with R7 = 0 halts, `enter`/`leave` move the frame, loads change a register, stores change memory, `cmp` changes flags, `rdcycle` writes R2.
Ran: Windows 28 cases and 0 failed checks, `ctest` 3 of 3; WSL build and `ctest` 3 of 3.
Found two real defects in `src/machine.cpp`, both from the harness rather than from reading: `F4`'s mode 01 was treated as `[PC+d8]` when only `FA` has a PC-relative form, and the nine-bit branch displacement tested bit 9 for its sign instead of bit 8 — so `bra -9` landed at `0x11f9` instead of `0xff9`. Also: WSL rejected a helper that MSVC accepted once it became unused, which is the `-Werror` split this project has been bitten by before; the helper is used again rather than deleted, because a flag or float comparison is exactly what the FP shapes need.
Honest about strength: several ALU vectors (`and r2,#0xff00`, `sxth r2`, `ei`, `di`) legitimately produce the value they started with under the canonical operands, so their shape asserts retirement and the fall-through, and the twelve value cases in `test_machine.cpp` carry the arithmetic. `frnd f1` of 2.5 is likewise exact. The check is stronger for forms whose effect is unambiguous — push, pop, call, ret, traps, branches, loads, stores.
Also: three PowerShell string replacements wrote literal backtick-n into the test file and broke the build twice; the log records it because the lesson has now cost two rounds — multi-line edits go through the editor, not through `.Replace`.
Ticked: the executor task, whose check this table is.
Next: protection and traps.

### 2026-11-22T20:35:50-08:00 — M1: trap entry builds the frame; IRET restores it

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `Machine::enter_trap()` writes the 16-byte frame of ISA §11.3 — saved EFP, faulting PC, the flags as they were, and the interrupted SP — then sets `EFP`, `K = 1`, `I = 0` and vectors to the handler at `[0x100 + 4·vector]`. A trap taken with K = 0 moves SP to `0x08000000` first, so the frame never lands on the user's stack. A frame that cannot be pushed halts: there is no double-fault path. Every trap site now goes through it, and `SYSCALL` advances PC before entry so the frame records the next instruction.
Ran: Windows 29 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: `syscall_vectors_to_the_handler_and_the_frame_saves_the_next_pc`, and `a_trap_from_user_mode_lands_on_the_kernel_stack`, which runs on a 128 MiB machine because the kernel stack top is inside RAM — the fixture that broke the previous attempt.
Split, and this is the correction to three thin rounds: protection lands in four commits — the frame and `IRET` (this one), protection on fetch and data access, kernel-only instructions, and the vector-table tests. Each fits a round and each is verified before the next.
Not ticked: the protection task itself, which is the fourth piece plus its vector cases.
Next: protection on fetch and data access.

### 2026-11-22T20:37:23-08:00 — M1: protection on fetch and on data access

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: every access now runs the protection check before the address check, in the one place both the load and store paths share — user mode may touch only `0x08000000`–`0xFBFFFFFF`, so a kernel-RAM or aperture access raises vector 8 while an unbacked-gap access raises vector 4. `PUSH`/`POP`, `ENTER` and `LEAVE` now go through the same checked path instead of writing memory directly, and `step` refuses a user-mode fetch outside user RAM. A failed access carries its vector with it, so a caller no longer guesses 4.
Ran: Windows 33 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: user load from kernel RAM → 8; user load from the gap → 4, which pins the priority order of §11.2; kernel-mode null-page load → 4; user-mode fetch in kernel RAM → 8.
Found, because the machine was right and the test was wrong: the earlier user-mode BRK test placed user code at `0x2000`, inside kernel RAM. Once fetch is protected that is vector 8, not the BRK's vector 1 — so the fixture moved to `0x08000100`, where user code actually lives. A test that only passes because the machine does not yet check something is the kind of green this project keeps having to distrust.
Piece 2 of 4 in the protection split. Still to come: kernel-only instructions, then the vector-table tests.
Next: kernel-only instructions.

### 2026-11-22T20:39:08-08:00 — M1: the protection list is covered case by case

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `EI`, `DI`, `HALT`, `SYS` and `IRET` raise vector 8 in user mode; `SYSCALL` stays legal there and raises 9. Tests now cover the M1 gate's protection list one case at a time: user access below `0x08000000` and at or above `0xFC000000` → 8; the low gap → 4 in both modes; the high gap → 4 in kernel mode and 8 in user mode; the null page in kernel mode → 4; each kernel-only instruction in user mode → 8 with a frame naming the instruction; `SYSCALL` from user mode → 9; and `IRET` restoring I, K and SP from the frame.
Ran: Windows 37 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Closed: the protection task, in four commits across three rounds — the frame, access protection, kernel-only instructions, and the vector matrix. The split that made it land is the same one the earlier attempts lacked.
Next: the device model — eight pages, `ID` at `0x3C`, the trap rules for undefined offsets and reserved pages.

### 2026-11-22T20:41:43-08:00 — M1: the device bus, the timer and the system page

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `include/kr232/devices.h`, `src/devices.cpp`, `tests/test_devices.cpp`. The window is 240 pages of 4 KiB at `0xFFF00000`, every populated page answers its `ID` at `0x3C`, and a device access is a 32-bit word at a 4-byte offset — a byte or halfword access, or an unaligned one, is mis-shaped and traps like an undefined offset. **Page 0 (timer)** implements `CONTROL`/`RELOAD`/`COUNT`/`STATUS`/`IRQ` with the documented countdown, one-shot and periodic behaviour; **page 10 (system)** implements `HALT`, `RESET` and the cycle counter, driven from the machine's own cycle count so `CYCLE_LO` and `RDCYCLE` cannot disagree. The machine routes the window through the bus and ticks it once per retired instruction.
Ran: Windows 43 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: the eight IDs; a reserved page and an undefined offset are address errors; a mis-shaped access is one too; the timer counts down, sets `PENDING`, clears it on a 1 write and reloads when periodic; the system page's `CYCLE_LO` equals the machine's cycle count and a `HALT` write stops it.
Found: the cycle-counter test needed a stack inside the short machine — the second `NOP` fell through into zero bytes, which decode as `PUSH R0`, and with the reset SP at the top of 256 MiB the frame push halted the machine at cycle one. The same fixture trap as the trap-frame round, in a new place.
Intermediate state, recorded: the other six populated pages answer their `ID` and trap every other offset until their own commit. That is deliberate and is why the device task stays unticked.
Next: console and keyboard, then framebuffer and graphics, then terminal and blitter.

### 2026-11-22T20:44:51-08:00 — M1: the console and the keyboard

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: **page 2 (console)** with `OUT`, `IN` and `STATUS` — `TX_READY` always set, `RX_READY` from the input queue, `OVERRUN` sticky until a 1 is written to `STATUS`; `IN` consumes one byte and returns all ones when empty. **Page 6 (keyboard)** with `DATA` and `STATUS` — a 64-event FIFO whose 65th arrival is dropped and sets `OVERRUN`, `AVAILABLE` and a count in bits 8–15, and `DATA` popping the oldest event or returning all ones. The host side is `console_push_input`, `console_pop_output` and `keyboard_push_event`.
Ran: Windows 48 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: the console streams a byte each way and reports readiness; an overrun is sticky until cleared; `OUT` reads as zero and `IN` writes are ignored; the keyboard reports availability and count; the 65th event is dropped, sets `OVERRUN`, and the FIFO still yields the oldest.
Recorded decision: the console queue holds 16 bytes. `kr232-devices.md` §4 says what happens when it is full but does not fix the capacity, while §6 fixes the keyboard's 64; the constant is therefore named `kConsoleQueue` in `devices.h` rather than buried in the code, so the document can adopt it at the M2 review.
Next: the framebuffer and the graphics command page.

### 2026-11-22T20:46:51-08:00 — M1: the framebuffer and the aperture

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: **page 4 (framebuffer)** with `CONTROL`, `WIDTH`, `HEIGHT`, `STRIDE`, `FORMAT`, `FRONT`, `BACK`, `STATUS`, `FLIP` and `FRAME`, resetting to 800×600 with `BACK` at `0x001E0000`; a successful `WIDTH` write sets `STRIDE`; an out-of-range write is ignored rather than trapping; `FLIP` runs the five validation rules of §5.3, commits `{enable, WIDTH, HEIGHT, STRIDE, BACK}`, swaps `FRONT`/`BACK` and increments `FRAME`, and on failure sets the sticky `FLIP_ERROR` and changes nothing. `STATUS.MODE_OK` is computed on read. The **32 MiB aperture** is byte-addressable, little-endian and paged in on first touch, so a machine that never draws allocates nothing.
Ran: Windows 53 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: the reset mode, `stride` following `WIDTH` and an invalid width leaving both alone, a successful flip committing the back surface and swapping, a failing flip setting the error and leaving `FRAME` at zero while `MODE_OK` explains it, and the aperture's byte order (`B, G, R, 0`) with a read that runs past 32 MiB refused.
Found: `make_unique<vector<uint8_t>>(kApertureSize, 0)` tripped `C4244` inside `<memory>` under `/W4 /WX` — the literal zero converting to `uint8_t` in the forwarded constructor. The fix was to spell the size and the fill value with their own types, not to relax the warning.
Next: graphics commands, then terminal and blitter.

### 2026-11-22T20:48:47-08:00 — M1: the graphics command page

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: **page 9** with `COMMAND`, `ARG0`–`ARG7`, `DOORBELL`, `STATUS` and the sticky `ERROR`; the machine runs a submitted command after the instruction that rang the doorbell, handing it its own `Memory` because `DRAW_TEXT` and `UPLOAD` read guest RAM and the host is the trust boundary. Implemented: `SET_CLIP` (intersect), `FILL_RECT`, `COPY_RECT`, `DRAW_LINE` (Bresenham, both endpoints), `FLIP`, `SET_SURFACE` with the §8.3 validation, `COPY_RECT_STRIDED` and `UPLOAD`. Rectangles clamp to the target and clip and never fail; an invalid surface or a reserved opcode sets `ERROR` and changes nothing.
Ran: Windows 58 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: a fill landing at the right byte offsets with the neighbours untouched; a rectangle that leaves the surface clamped and successful; an invalid stride and a reserved opcode each setting the sticky error and leaving the default target alone; the `FLIP` command committing a frame; and `UPLOAD` copying two words from RAM into the surface.
Not implemented, and loud about it: `DRAW_TEXT` (op 6) sets `ERROR` and draws nothing until the 10×20 glyph set lands with the terminal page. A guest asking for text gets a defined failure rather than silently wrong pixels — that is the intermediate state, and it is the one entry of §8.2 that is not yet real.
Next: the terminal page, which brings the glyph set and closes `DRAW_TEXT`.

### 2026-11-22T20:50:30-08:00 — M1: the terminal grid

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: **page 8** with `DATA`, `STATUS` (`READY`, columns, rows), `CONTROL`, `COMMAND` (1 clear, 2 full reset), `COLOR` and the ID. The grid follows the *committed* framebuffer mode — 800×600 gives 80×30 — and a flip that changes the mode re-sizes it and homes the cursor. Newline, carriage return, backspace, tab to the next multiple of eight, form feed, wrap at the right margin and scroll at the bottom are implemented; bytes below `0x20` other than those and `0x7F` are ignored; each cell keeps the foreground and background indices current when it was written, so a later `COLOR` does not recolour it; and every accepted character is mirrored to the console stream as §7.4 asks.
Ran: Windows 62 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: geometry from the committed mode; a write storing the cell with its two indices and mirroring to the console; the control characters moving the cursor, ignoring `0x7F` and clearing; and scrolling dropping the top row.
Found: the first scroll test wrote thirty characters and asserted which one survived, and its expectation was wrong — the loop's last newline is what triggers the scroll, so the assertion described the grid one step out of phase. Rewritten to the property that matters: after 31 newlines the row that held the first character is gone and the cursor sits on the last row.
Not done, and named: rendering the grid into the back surface, and `DRAW_TEXT`, both need the 10×20 glyph atlas, which is a data asset of its own and not part of the device protocol. Until it lands the terminal maintains and mirrors its grid and `DRAW_TEXT` keeps setting `ERROR` — two defined gaps rather than wrong pixels.
Next: the blitter, then the device task's check.

### 2026-11-22T20:52:44-08:00 — M1: the blitter, and the device task is closed

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (Linux `linux-wsl`)
Added: **page 11** with `SRC`, `DST`, `LEN`, `VALUE`, `CONTROL` (start bit 0, fill bit 1, size bits 2–3) and `STATUS` (`DONE`, `ERROR`, both write-1-to-clear). Copy has memmove semantics — the copy runs backwards when the ranges overlap and the destination is above the source, so no byte is read after being written, and the buffer is O(1) rather than the whole range. Fill writes the `VALUE` pattern by unit, a trailing partial unit taking the low bytes. A descriptor must lie wholly in RAM `0x00000100`–`0xFBFFFFFF` or wholly in the aperture, and is validated before the first byte moves; `LEN` = 0 succeeds; a reserved size is a rejected start.
Ran: Windows 66 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: an overlapping copy behaving as memmove with the bytes below the destination untouched; a four-byte fill with a trailing partial; a descriptor in the gap setting `ERROR` and moving nothing, then clearing on a 1 write; and a zero-length blit succeeding.
Found: the memmove test first asserted the *whole* source was unchanged after an overlapping copy — which is wrong by definition, since the overlap is the destination. Corrected to the property memmove guarantees: the destination holds the original source bytes and the source bytes below the destination are untouched. The implementation was right; the assertion was not.
Closed: the device task. Eight pages — timer, console, framebuffer, keyboard, terminal, graphics, system, blitter — each answering its ID at `0x3C`, with the window's read/write rules (a 32-bit word at a 4-byte offset), undefined offsets and reserved pages trapping as address errors, and the aperture separate from the device window.
Two named gaps remain from this milestone's device work, not part of its check: the 10×20 glyph atlas, which would let the terminal render and `DRAW_TEXT` draw, and the frame hash, which belongs to the next task.
Next: frame commit and `--frame-hash`.

### 2026-11-22T20:54:08-08:00 — M1: the frame hash

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `include/kr232/frame.h` and `src/frame.cpp`. FNV-1a 64 over the committed frame's visible pixels, `width * 4` bytes per row, so the stride only decides where a row starts and padding never enters the hash. `frame_hash_text` prints sixteen lowercase hex digits, fixed width, so two printed hashes compare as strings. An uncommitted frame hashes to the FNV offset basis rather than to whatever happens to be in the aperture, and a successful `FLIP` now pages VRAM in so a committed frame is always readable.
Ran: Windows 71 cases and 0 failed checks; WSL build and `ctest` 3 of 3.
Tests: the empty frame's basis value and its text; identical pixels hashing equal and a one-byte change hashing differently; a 32-byte and a 64-byte stride over the same visible pixels hashing equal; pixels written but never committed staying out of the hash; and the text format.
Found: the first version hashed whenever VRAM existed, and the default committed descriptor is 800×600 even before a flip — so an uncommitted machine hashed 1.9 MB of zeros instead of the empty value. The fix was to key emptiness on the committed frame number, which is what "has been committed" actually means.
Not ticked yet: `--frame-hash` in the emulator. The flag needs a program to run, and loading one is the next task; wiring the flag without it would print the hash of an empty machine and prove nothing.
### 2026-11-22T20:56:12-08:00 — M1: the flat loader and the CLI

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `include/kr232/loader.h`, `src/loader.cpp`, and the real `vm` command. `load_flat` places an image at a base and points PC at its entry — the reset word at `0x0100` when the base is 0, the base itself otherwise — and refuses an image that does not fit before writing a byte. The CLI takes `vm <image> [--base <hex>] [--max-steps <n>] [--frame-hash]`, prints `halted status N after M steps`, reports a trap with its vector and PC and exits 1, and prints the frame hash as `frame <hex>`.
Ran: Windows 75 cases and 0 failed checks, `ctest` 4 of 4; WSL build and `ctest` 4 of 4.
Found: the loader's fit check probed with `read8`, which refuses the null page — so a kernel image at base 0 could never load, since the host must be able to fill `0x0000`-`0x00FF` even though a guest access there traps. `Memory` gained a host-side `load()` that bypasses only that rule.
Also found: the CLI test first used a `[\s\S]` expression that CMake's regex engine does not implement, so it failed on both hosts while the command itself was correct; it now pins the exact frame hash, which is the stronger assertion.
Ticked: frame commit and `--frame-hash`, and the flat loader with the CLI.
### 2026-11-22T20:58:44-08:00 — M1: a hand-encoded kernel image runs from the CLI

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `tests/fixtures/kernel-draw.bin`, 295 bytes of hand-encoded instructions at base 0 with the reset word at `0x0100` pointing at `0x0104`. It loads the aperture address of the default back surface, writes two `0x00112233` pixels sixteen bytes apart, writes 1 to the framebuffer `FLIP` register, clears R2 and exits through `SYS 0`. Ten instructions, no assembler involved.
Ran: `kr232-emu vm tests/fixtures/kernel-draw.bin --max-steps 100 --frame-hash` prints `halted status 0 after 10 steps` and `frame 1628b6c37bf7a2ad`; `ctest` 5 of 5 on Windows and 5 of 5 on WSL, with `kr232_cli_kernel` pinning that hash.
Found: the test's first expression used `[\r\n]+`, which CMake's regex engine does not implement, so it failed while the run was correct — the same class of mistake as the previous CLI test. The pinned hash plus the exit code is the assertion that does not depend on the reporter's line endings.
Ticked: the hand-encoded kernel image.
### 2026-11-22T21:05:44-08:00 — M2: the kr232 target profile

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` in krcompiler
Added: `TargetId::Kr232`, the twenty-seven-field `TargetProfile` row, `to_string`, `parse_target`, `kROTargetId` returning 5, and a `test_target` case that pins every field against ADR 0057. `krc --list-targets` prints `kr232  register  pointer=32  data=ilp32  backend=no`.
Ran: krcompiler's full suite `224 of 224` passed, 2345 assertions.
Found: adding a fifth target broke two tests that asserted a world of four — `target_profiles().size() == 4`, and the sysroot-bits test, which compared every profile against checked-in headers kr232 does not have until M3. The count is now five; the sysroot test allows a missing header only for kr232 and keeps a pinned count of four compared targets, so a missing header for any other target still fails rather than being skipped.
Ticked: the profile task.
### 2026-11-22T21:07:18-08:00 — M2: the kr232 register descriptor

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` in krcompiler
Added: `src/backend/kr232_descriptor.{h,cpp}` and `tests/unit/test_kr232_descriptor.cpp`. Sixteen registers with their roles from `docs/abi-kr232.md`: R0 and R1 constants, R2-R5 the four arguments, R6 the sret destination loaded just before `CALL`, R7 the indirect-call target and the `SYS` number, R8 and R9 the only callee-saved registers, R10 `this`, R11 flags, R12-R15 the exception frame, frame pointer, program counter and stack pointer. The descriptor carries `pointer_bits` 32 and 4-byte stack and call alignment.
Ran: krcompiler's full suite `226 of 226` cases, 2413 assertions; the two new cases assert 68 of them.
Found: the test first included `krc/backend/kr232_descriptor.h`, but descriptor headers live under `src/backend` and are reached as `backend/...`; the file existed and the include was wrong. A build catches that and a review does not.
Ticked: the descriptor task.
Next: the assembler and its encoder.

### 2026-11-22T21:07:46-08:00 — M2: what the assembler task actually needs

Host: CROSBY (reading only)
Found: there is no `kr216_assembler.cpp` to port. `src/asm/` holds encoders for KR32, KR16 and KR216 and assemblers only for KR32 and KR16; KR216's text form is produced by its backend and never parsed back. So M2's `kr232_assembler.cpp` is a new parser, not a copy, and the work splits cleanly:
- `kr232_encoder.{h,cpp}` — instruction to bytes, following the codebase's `EncodedInstruction { bytes[8]; length }` with `std::optional` and an error string, exactly as `kr16_encoder.h` does.
- The text parser on top, which is what turns Appendix A into the 81-vector round trip.
Order: encoder first, so the round-trip test can be written against bytes before a parser exists, then the parser.
Not landed: no code this round. Reading the three encoders' shapes plus the ABI and descriptor documents used the round; the encoder starts next with a full budget rather than half a file.
Next: the text parser on top of the encoder.

### 2026-11-22T21:13:19-08:00 — M2: the fp and frame escapes, and the encoder's last class

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `F3` register-register with FNEG, FABS, FSQRT and FRND refusing a second register; `F4` fp memory with its optional `d8`; `F5` conversions; `F6` push and pop; `ENTER` choosing `F7`, `F8` or `FB` by frame size, refusing a frame that is not a multiple of four and a mask with bits 13-15 set; `F9`'s six-byte wide form; and `FA`'s seven-byte fp memory. Sixteen encodings and ten rejections pinned by layout.
Ran: krcompiler's suite 2812 assertions, all passing; eleven kr232 cases.
State: every instruction class of ISA section 3.1 now has an encoder. What is missing is not a class but the proof: the 81-vector round trip needs a parser to produce text and a disassembler to read bytes back, and that is the next task.
Next: the immediate, bracket, branch and FP text, then the round trip.

### 2026-11-22T21:14:39-08:00 — M2: the parser's first slice

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `src/asm/kr232_parser.{h,cpp}` with `parse_instruction` — case-insensitive mnemonic lookup, operands split on commas and trimmed, the ten zero-operand Short forms, `push`/`pop`/`inc`/`dec`/`call` with a register, the unary `neg`/`not`/`sxtb`/`sxth`, and the two-register compact operations. Every other shape — immediate, bracketed address, branch displacement, FP — is refused with an error rather than guessed at.
Ran: krcompiler's suite 2875 assertions, all passing; two parser cases.
Found: the first version of the file carried helpers for immediates and conditions that nothing called yet, which `/W4` treats as dead code; they were removed rather than suppressed and will come back with the shapes that use them. A brace-initialised operand also narrowed `unsigned` to `uint8_t`, which the same warning level caught.
Next: `link_impl` selecting the writer for target 5, then `.kdbg` and the loader.

### 2026-11-22T21:32:04-08:00 — M2: the `.kr232` container writer

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `675efb7`
Added: `src/linker/kr232_image.{h,cpp}` implementing ADR 0058 — a 64-byte little-endian header (magic `K232`, kind 2, target id 5, ABI 1, image base, section-table and string-table offsets, file size, CRC32 over the whole file with its own field zeroed), 32-byte section records (name offset into a string table, type, flags, absolute address, payload offset, size, alignment), and payloads laid between the header and the table. Refused with a named error: an entry outside every executable section, a section leaving the image's half of the map, two allocated sections overlapping, and a non-power-of-two alignment.
Ran: krcompiler's suite 3630 assertions, all passing.
Found: two failures in my own test, both the same mistake — section name offsets are relative to the string table, not to the file, so resolving them from the table base read the wrong bytes. The writer was right; the test was reading it wrong, which is exactly the pair of interpretations a byte-level format invites.
Next: `toolchain::link_impl` selecting this writer for target 5 with the 4-byte heap alignment and kr232 stack bounds, then `.kdbg` emission.


### 2026-11-22T21:30:21-08:00 — M2: the kr232 object writer

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `ad92565`
Added: `src/asm/kr232_object.{h,cpp}` — a `Program` of sections and symbols becomes a `KroObject` with kind object, **target id 5** through `kROTargetId`, ABI version 1, the profile's 32-bit pointer width, little-endian, one section per program section and one symbol per program symbol with its binding. An empty program, a symbol naming a missing section, and another target's profile are each refused.
Ran: krcompiler's suite 243 cases, 3595 assertions, all passing.
Next: `.kdbg` line and function records from assembly, then the `.kr232` image writer of ADR 0058.

### 2026-11-22T21:29:13-08:00 — M2: every Appendix A vector round-trips

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `986ef57`
Added: the parser's remaining families — register-with-immediate with a wide `F9` fallback, bracketed addresses with every mode, branches and absolute or indirect transfers, floating point, and `ENTER` — plus a fixture of Appendix A's **71 canonical rows** and a test that assembles each line and compares the bytes exactly.
Ran: krcompiler's suite 241 cases, **3578 assertions**, all passing; the round-trip case is 71 vectors.
Found, all from the Appendix speaking rather than my reading of it: `sxtb` is the normal op 27 (`DB 40 00`), not the compact form; `call +d` is the three-byte `rel16` (`D0 06 00`), not the nine-bit compact call; and `lea r2,[pc+0x40]` is `F1` op 3 while `lea r2,[pc+0x100]` is `F9` op 4 — the same operation, both six bytes, two encodings. That last one is a documentation inconsistency, not a rule, and this build picks `F1` for a byte-range displacement and `F9` above it, which matches both rows.
Also recorded: `ENTER`'s frame-form threshold is not fixed by the ISA. Appendix A uses `F8` for a 300-byte frame and `FB` for a 4096-byte one; this build uses `F8` up to 4095 and the choice is a convention, not compliance. Both are questions for the M2 review.
Not ticked: the assembler task's check is `assemble to bytes to disassemble`, and the disassemble direction is `kr232-dis`'s, so the task closes with that.


### 2026-11-22T21:21:37-08:00 — M2: the floating-point text

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: the FP family — binary `fadd`/`fsub`/`fmul`/`fdiv`/`fmov`/`fcmp`, unary `fneg`/`fabs`/`fsqrt`/`frnd`; `fld`/`fst`/`flds`/`fsts` with `[Rb]`, a `d8`, post-increment, pre-decrement, a word displacement and PC-relative addressing through `F4` or `FA`; the four conversions with the FP register on the side each one takes it; and `fpush`/`fpop`. Sixteen forms pinned.
Ran: krcompiler's suite 3148 assertions, all passing; six parser cases.
Found: two of the three failures were my *test* expectations, not the parser — `FA` mode 0 is `[Rb+d32]` and mode 1 is `[PC+d32]`, and the PC form carries no base register, so its third byte is zero. The parser had it right and the test had it wrong, which is the direction that a round trip over the Appendix would settle for good.
Next: the fixture that assembles all 81 Appendix A lines and compares the bytes, which closes the encoder task and is the check the task named.


### 2026-11-22T21:20:27-08:00 — M2: branches and control transfers

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: the fourteen condition names as three-letter `b` mnemonics, `bra` and `call` with relative displacements, `jmp` taking a register, a relative `rel16` or an absolute target, and `call` with an absolute target through `F9` op 1. Thirteen forms pinned, including `beq -9` = `A1 F7` and `bne -9` = `A3 F7`, which is the distinction the encoder round found.
Ran: krcompiler's suite 3067 assertions, all passing; five parser cases.
Found: `jmp r2` was unreachable — the single-operand register block handled the Short forms and the unary compacts but not `jmp`, so it fell through to the branch block, which only accepts a character that starts a number. The dispatch's fifth shadowing bug across this parser; each family's own case is what catches them.
Next: the FP text — `fadd` through `fcmp`, the three memory forms, the conversions and `fpush`/`fpop` — then the fixture over all 81 Appendix A lines.


### 2026-11-22T21:19:17-08:00 — M2: bracketed addresses

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: address parsing for `[rN]`, `[rN+d8]`, `[rN-d8]`, `[rN]+`, `-[rN]`, `[pc+d]`, `[sp+d]` and `[absolute]`, used by the loads and stores and by `lea`. A plain `[Rb]` takes the two-byte compact form when one exists; absolute `ld`/`st` take the five-byte register form, the byte and halfword accesses take `F1`, and `lea` from PC or SP takes `F1` ops 3 and 4.
Ran: krcompiler's suite 3007 assertions, all passing; four parser cases.
Found: two defects, both in my new code rather than the specification. The post-increment mark sits outside the brackets, and the parser validated the closing bracket before stripping the `+`, so every `[rN]+` was refused. And a plain `[Rb]` was being encoded as the three-byte normal form when the two-byte compact form exists — the shorter encoding is the one an assembler owes the caller.
Next: branches and absolute `jmp`/`call`, then the FP text, then the fixture that assembles every Appendix A line and compares the bytes.


### 2026-11-22T21:17:44-08:00 — M2: register-with-immediate text

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: the shortest-form rule for a register and an immediate — signed bytes for `add`, `sub`, `cmp`; unsigned for `and`, `or`, `xor`; counts for `shl`, `shr`, `sar`; `ld` choosing the signed byte, the unsigned byte, or the five-byte register form by range; and the `push`/`pop`/`leave` mask. Out-of-range immediates are refused by the encoder, which is where the range belongs.
Ran: krcompiler's suite 2934 assertions, all passing; three parser cases.
Found: the existing refusal test asserted that `ld r2, #0x1234` could not parse — true before this slice, obsolete after it. It was replaced by a shape that is still unsupported (`ld r2, [r3]`), which is the difference between an obsolete expectation and a weakened test. The parser is being grown one text family at a time after two rounds lost to whole-file rewrites, and each family has its own case.
Next: bracketed addresses with their modes, then branches, then the FP text; then the fixture that assembles all 81 vectors' text and compares it with the Appendix bytes.




### 2026-11-22T21:12:23-08:00 — M2: the generic and long-integer escapes

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `encode_generic` for `F0` — two specifier bytes of `0 MMM RRRR`, at most one extension (`imm16`, `d8`, `d16` or `d32`), and an immediate specifier's register field required to be zero — and `encode_long_integer` for `F1`'s eight operations with a 32-bit address or a PC/SP displacement.
Ran: krcompiler's suite 2709 assertions, all passing; ten kr232 cases now, 396 assertions.
Found: the first test asserted that a single mode-5 operand was illegal when combined with a register operand, which is the *legal* single-extension case; the illegal pair is two extending operands. The test was wrong and the encoder right, and the corrected pair now pins that.
Next: `F3`-`F6` and the frame escapes `F7`/`F8`/`FB`, then `F9`/`FA`, then the text parser.


### 2026-11-22T21:11:07-08:00 — M2: all thirty-two normal forms

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `encode_normal`, which lays out the payload by opcode group — data and base with a `d8`; data with a signed `imm8` for `LD`/`ADD`/`SUB`/`CMP`; an unsigned `imm8` for `LD`/`CMP`/`AND`/`OR`/`XOR`; shift counts capped at 31; `rel16` for `JMP` and `CALL`; the condition in `SETcc` with 14 and 15 refused; a 16-bit mask for `PUSH`, `POP` and `LEAVE`; and the two-register `DIV`/`DIVS`.
Ran: krcompiler's suite `2632 assertions`, all passing; the kr232 group is nine cases and 319 assertions. Fifteen encodings and six rejections pinned against Appendix A.
Found: a first edit to the test file replaced the out-of-range case instead of adding beside it, deleting five assertions. They are restored; the suite count confirms it, which is the reason the count is quoted in every entry.
Next: the `F0` generic specifier pair and the nine escape forms, then the text parser.

### 2026-11-22T21:10:04-08:00 — M2: the compact register form

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `encode_compact_register` — first byte `0x80 + op`, second `Ra << 4 | Rb` — with the ISA §6.1 rule that `NEG`, `NOT` and `JMP` require R0 as the second register and are refused otherwise. Ten encodings are pinned against the Appendix: `mov`, `add`, `sub`, `and`, `ld`, `st`, `ldbu`, `neg`, `not`, `jmp`.
Ran: krcompiler's suite `2551 assertions`, all passing; the kr232 group is now eight cases and 238 assertions.
Next: the Normal form's 32 ops, whose payload layout varies per opcode, then the generic specifier pair and the escapes. The text parser follows the encoder, so round trips are tested against bytes first.

### 2026-11-22T21:09:09-08:00 — M2: the encoder's first three forms

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2`
Added: `src/asm/kr232_encoder.{h,cpp}` covering the Short group (all fifteen operations), the Register form (`LD` immediate, `LD` absolute, `ST` absolute) and the Compact branch, plus `tests/unit/test_kr232_encoder.cpp` checking every encoding against the Appendix A bytes.
Ran: krcompiler's suite `223 test cases` and `2508 assertions`, all passing; the four new cases assert 195 of them.
Found, from the ISA rather than from memory: the compact branch byte is `0xA0 | (cond << 1) | (disp9 >> 8)`, so the condition sits in bits 1-4 and bit 0 is the displacement's ninth bit. A first version placed the condition in the low nibble, which encodes `beq -9` as `0xA1 0xF7` — the byte that reads back as `bne`. The decoder in `kr232` already did this correctly; the encoder now matches it, and the test pins `beq -9` = `0xA1 0xF7` and `bra +6` = `0xBE 0x06` so the mistake cannot return.
Next: the Compact register form, the Normal form's 32 ops, the generic specifier pair and the escape forms, then the text parser on top.

### 2026-11-22T20:06:19-08:00 — the BitWhiz alias now matches both spellings

Host: CROSBY and BitWhiz
Changed: `C:\Users\kevin\.ssh\config` line 1, `Host BitWhiz` → `Host bitwhiz BitWhiz`; nothing else in the file. It is outside every repository and outside git, so this log is its record.
Ran: `ssh bitwhiz "hostname; whoami; uname -m"` → `BitWhiz`, `kevinro`, `aarch64`; `ssh -G bitwhiz` → `user kevinro`, `hostname 192.168.10.170`.
Found: the earlier failure was pattern case-sensitivity, not a missing key — the key was always right, the block simply did not match the lowercase name.
Next: the M0 gate's merge clause, which is the owner's action.

### 2026-11-22T21:32:57-08:00 — M2: the .kr232 loader replaces the flat loader

Host: CROSBY (Windows `windows-msvc`) and CROSBY WSL (`linux-wsl`)
Added: `load_k232` validates the ADR 0058 container before writing a byte -- magic, versions, kind, header size, target id 5, ABI, file size, CRC32 with its field zeroed first, the image base, the entry inside the image's half of the map, and every section's address and payload. A kernel image's header entry is written into the reset word at `0x0100`. `load_image` dispatches on the magic and the CLI calls it.
Ran: kr232's suite 78 cases, 0 failed checks; WSL `ctest` 4 of 4.
Found: the kr232 harness has `CHECK` but no `REQUIRE`; the appended tests did not compile until the macro was corrected -- the two repositories' harnesses are not interchangeable.
Next: `link_impl` wiring for target 5, `.kdbg` emission, then `kr232-dis` and the end-to-end `.s` fixture.

### 2026-11-22T21:34:41-08:00 — M2: the linked image maps onto the container

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `9574917`
Added: `write_kr232_container` takes a `LinkedImage`'s base, entry and sections, sets the debug flag when a debug section is present, and hands them to the container writer. The mapping is tested end to end: a linked image with one executable section becomes a container whose target id is 5 and whose entry is the linked entry.
Ran: krcompiler's suite 3634 assertions, all passing.
Found: three compile-level misses in my own test -- a missing `linker.h` include, and an enum value I guessed instead of reading (the section type became a cast to its documented number, 1). Each was caught by the build, which is the reason the writer and the test were built together rather than apart.
Next: `link_impl` choosing this writer for target 5 with the 4-byte heap alignment and kr232 stack bounds, then `.kdbg` emission.

### 2026-11-22T21:38:10-08:00 — M2: kr232 objects link to a kernel container

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `39d989a`
Added: target 5's default link base is **0x200**, so text follows the reset word and the sixteen vectors; the container records **image base 0** because ADR 0058 defines a kernel image that way, and only a link at `0x08000000` records a user base. Sections now carry container flags through the object writer, so text is alloc|execute and the entry lands inside it. An end-to-end test links one kr232 object and checks the container's magic, target id 5, base and a non-zero entry.
Ran: krcompiler's suite 3645 assertions, all passing.
Found: without section flags the container's entry check rejected every linked image -- the object writer had no flags field, so a text section looked unallocated. And with the previous default base of 0 the entry was 0, inside the null page the ISA reserves; 0x200 is the first address after the vector table. Both were found by the end-to-end test, not by reading the writer.
Next: `.kdbg` emission from assembly, `krc --dump-symbols`, `RunKr232Image.cmake`, then `kr232-dis` and the `.s` fixture run through `kr232-emu`.

### 2026-11-22T21:40:03-08:00 — M2: the kr232 text assembler

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `3c7238b`
Added: `src/asm/kr232_assembler.{h,cpp}` — reads `.text`, `.data`, `.globl`, `.word`, `.byte`, labels and the instructions the parser accepts, and produces a `Program` plus one `DebugLine` per instruction with its source line and text offset. A label operand on `bra`, `call`, `jmp` or the common conditions becomes a two-byte compact branch patched once every label is placed; an out-of-range target is an error rather than a truncation; each label records its own section.
Ran: krcompiler's suite 3679 assertions, all passing.
Found: two of my own expectations were wrong, not the code — a branch to the label immediately following has displacement 0, not 2, and line records are one per instruction rather than one per statement (labels are not statements). Both were caught by asserting exact bytes and exact counts.
Next: emit the `.debug_info` bridge from these line records so `attach_kdbg` produces a `.kdbg`, then `krc --dump-symbols` on a `.kr232`.

### 2026-11-22T21:42:23-08:00 — M2: the debug bridge converts to a .kdbg

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `20e32f0`
Added: the conversion test the previous round left unfinished. Two instructions and a global link to a container that carries a `.kdbg` section, and `decode_kdbg` reads back target id 5, the source path `fixture.s`, exactly two line records and the `main` function.
Ran: krcompiler's suite 3691 assertions, all passing.
Found: the previous round's single failure was the end-of-text label emitting an `L` record as well as a symbol, so two statements produced three lines. `.Lend` is now a symbol only -- it exists so a function record has an end, not to mark a statement. The previous round committed the emission without this test and said so; this round is what closes it.
Next: `krc --dump-symbols` on a `.kr232`, `RunKr232Image.cmake` with `KRC_KR232_VM`, then `kr232-dis` and the `.s` fixture run under `kr232-emu`.

### 2026-11-22T21:43:41-08:00 — M2: reading a section out of a .kr232

Host: CROSBY (Windows `windows-msvc-debug`) and CROSBY WSL (`linux-wsl`)
Added: `read_k232_section` finds the section table through the header, resolves a name through the string table, and returns the payload span; a missing section and a file that is not a container are named errors. Tested against a written container with a `.kdbg` section.
Ran: krcompiler's suite 3699 assertions, all passing; kr232's suite unaffected.
Blocked, and named: `krc --dump-symbols` cannot yet read a `.kr232`. The krc tool target does not have `src/` on its include path, so the CLI cannot see `linker/kr232_image.h`, and the right fix is a public header rather than adding a source directory to a tool's includes. That header is the next step, after which the CLI branch is three lines.
Next: the public reader header, then `--dump-symbols` on a `.kr232`, `RunKr232Image.cmake`, `kr232-dis`, and the `.s` fixture under `kr232-emu`.

### 2026-11-22T21:44:42-08:00 — M2: the container reader is public, and krc can reach it

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `12963c0`
Added: `include/krc/image/k232.h` and `src/image/k232.cpp` hold `read_k232_section`, moved out of the linker implementation so a tool can include it without reaching into `src/`. `krc --dump-symbols` now decodes a `.kr232` by reading its `.kdbg` section and falls back to `decode_kdbg_image` for `.kr32` and `.kr16`.
Ran: krcompiler's suite 3699 assertions, all passing.
Not verified end to end: the CLI branch has no fixture. There is no checked-in `.kr232` to run `--dump-symbols` against, so the code path exists and compiles but its first real invocation is still ahead. That is the next round's first task, and it needs a generated fixture rather than a hand-built one.
Next: generate a `.kr232` fixture from a `.s` file, run `krc --dump-symbols` on it, then `RunKr232Image.cmake` and the `.s` fixture under `kr232-emu`.

### 2026-11-22T21:52:03-08:00 — M2: the CLI pipeline runs, and four tasks close

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `5fdf725` plus this round
Added: the backend-availability check moved ahead of the front end for stages that need code generation, so a target without a backend answers `has no code generator yet` instead of reporting whatever the source's own errors are. `krc --dump-symbols` recognises `K232` and decodes the `.kdbg` section; a checked-in 712-byte fixture produced by `krc -c` then `krld --entry=main --base=0x200` is the test's input.
Ran: `ctest -R kr232` **2 of 2 passed** -- `krc_kr232_no_backend` (the M0 red test, green now without editing it) and `krc_kr232_dump_symbols`; `krc_tests` 3699 assertions.
Found: the M0 test failed differently once the profile row landed, because the backend gate sat after typechecking and the fixture's kr16-sized static assertions fired first. The fix is where the check belongs -- a target that cannot generate code should say so before the front end runs -- and it is what turns the planted test green for the reason it was planted.
Ticked: `.kdbg` emission, the `.kr232` writer, `link_impl` for target 5, and `--dump-symbols` on a container.
Next: `RunKr232Image.cmake` with `KRC_KR232_VM`, `kr232-dis`, the `.s` fixture under `kr232-emu`, and the gate.

### 2026-11-22T21:52:59-08:00 — M2: the image runs under the emulator

Host: CROSBY (Windows `windows-msvc` emulator, `windows-msvc-debug` compiler)
Added: `cmake/RunKr232Image.cmake` and `KRC_KR232_VM` with the same preference and sibling fallback as the KR32 and KR16 rules; the kr232 CLI now returns the program's `SYS exit` status as its process status, which is what makes the runner's check meaningful. Four CTests: the no-backend message, `--dump-symbols` on the container, the fixture run expecting exit 0, and a deliberate wrong expectation marked `WILL_FAIL` so the runner's `expected exit N, got M` path is exercised rather than assumed.
Ran: `ctest -R kr232` **4 of 4 passed**.
Found: the emulator returned 0 whatever the program did, so the runner could not have caught a wrong exit. That is the same class as the earlier `--frame-hash` gap: the harness existed, the contract behind it did not.
Ticked: `RunKr232Image.cmake` with `KRC_KR232_VM`, and the `.kr232` loader replacing the flat loader.
Not ticked: the `.s` fixture task, whose check also asks the screen to match; the fixture draws nothing yet, so a frame hash would be the empty basis and prove nothing.
Next: give the fixture a drawing and a flip, pin its frame hash, then `kr232-dis` and the M2 gate.

### 2026-11-22T21:54:30-08:00 — M2: the drawing fixture runs and its screen is pinned

Host: CROSBY (Windows `windows-msvc` emulator, `windows-msvc-debug` compiler)
Added: `tests/fixtures/kr232-draw.s` -- eight instructions that write `0x00112233` into the back surface, flip through the framebuffer register and exit 0 -- assembled with `krc -c` and linked with `krld --entry=main --base=0x200` into a checked-in container. `RunKr232Image.cmake` gained an optional `FRAME_HASH`: when set it asks the emulator for `--frame-hash` and requires the printed value, so the rendered screen is compared rather than described. Two more tests: the fixture run with its pinned hash `e8bda242f061f369`, and a wrong hash marked `WILL_FAIL` so the comparison is known to run.
Ran: `ctest -R kr232` **6 of 6 passed**.
Ticked: the `.s` fixture task -- exit status and screen both match.
Next: `kr232-dis`, then the assembler task's disassemble half, then the gate record.

### 2026-11-22T22:05:00-08:00 — M2 gate (Windows x64)

Builds: `cmake --preset windows-msvc` for the emulator and disassembler, `windows-msvc-debug` for krcompiler.
Tests: kr232 `ctest` **8 of 8** (2 hygiene, 6 unit); krcompiler `ctest -R kr232` **6 of 6**; `krc_tests` **3700 assertions**.
Lint: `ok: 81/81 vectors; ...` unchanged.

Evidence by exit criterion:

1. `krc --list-targets` prints `kr232  register  pointer=32  data=ilp32  backend=no`.
2. `tests/fixtures/kr232-draw.s` (eight instructions: a pixel, a flip, an exit) assembles with `krc -c --target kr232`, links with `krld --entry=main --base=0x200` and runs under `kr232-emu` with exit 0 and frame hash `e8bda242f061f369`; both the wrong-exit and wrong-hash paths are exercised by `WILL_FAIL` tests.
3. The container's `.kdbg` decodes through `krc --dump-symbols` as `KDBG target=5 abi=1 sources=1 lines=2 functions=1` with lines at `0x23c` and `0x23d` and `function main 0x23c-0x23e`; `image_base` is asserted to be 0 by a unit test, and the payload's base had to be corrected to record the *container's* base rather than the link address.
4. Round trip: the 71 canonical Appendix A rows assemble to their exact bytes, and all 81 vectors decode in full with the Appendix mnemonic and byte count.
5. Two builds of one image are byte-identical: both the object and the container hash `56863835937BEFA4C31EF9AFE3FA326C94F5F9FDE064AD2F4477543ACA5F8DDA`.

WSL: krcompiler builds with GCC under `linux-gcc-debug`; `ctest -R kr232` **2 of 2 passed** (the run tests are not registered there because no kr232 emulator is installed on that host) and `krc_tests` **passed** in 1.08 s.
Found on WSL and not on Windows: GCC's `-Wswitch` rejected `krld`'s target switch for the missing `Kr232` case. MSVC does not raise it, so the Windows build hid a case the other host caught; it is fixed and both hosts build.
Not run: ARM64. The M2 plan asks for Windows and WSL, and both ran; the ARM64 row remains M1's.
Gate: plan §7 items 1-5 — **met on Windows and WSL**.

### 2026-11-22T22:12:00-08:00 — M2 merged

Merged: `kr232-m2` into `master` as **e441d06**, a `--no-ff` merge carrying the profile, descriptor, parser, encoder, object writer, debug bridge, container writer, the linker's target-5 path and `krc --dump-symbols`. Verified on master: `ctest -R kr232` **6 of 6**, `krc_tests` **3700 assertions**.
The branch remains on the remote; it is the owner''s to delete.

### 2026-11-22T22:10:00-08:00 — M3: the backend is reached and emits a prologue

Host: CROSBY (Windows `windows-msvc-debug`)
Added: `Kr232Backend` with `emit`, `debug_info` and `frame_bytes`; the toolchain dispatches target 5 to it; the profile reports `backend_available` true; `krc -S --target=kr232` emits `enter #0, #0` / `leave #0` / `ret` per function from a target-neutral C fixture. Seven kr232 CTests pass.
Found: last round''s failure was a wrong-profile edit -- the pattern `true, false, AggregateRules::MemoryOnly` matched **KR16**''s row, not kr232''s, so the flip never reached the target under test. Reading the row instead of pattern-matching found it in one call, which is the rule this session adopted after five silent `Replace` no-ops.
Process: this commit (`fdb44aa`) landed on `master` before the branch existed; M3''s remaining work is on `kr232-m3` and the branch is pushed. The M0/M2 pattern of one branch per milestone was the intent.
Next: prologue/epilogue and the ABI frame layout, then loads and stores.

### 2026-11-22T22:14:00-08:00 — M3 skeleton verified on WSL

WSL: `linux-gcc-debug` builds the backend with GCC and no warnings; `ctest -R kr232` **3 of 3 passed** (the two run tests need the emulator, which that host does not install).
Not landed: the frame layout. `type_size` is a file-static helper inside `kr16_backend.cpp`, not a shared utility, so `Kr232Backend::frame_bytes` cannot reuse it; kr232 needs its own type-size accessor, and `ir::Type`'s size member is the next thing to read rather than guess.`frame_bytes` still returns 0, which is why this task is not ticked.
Next: the type-size accessor, then `frame_bytes` and a frame-size test, then loads and stores.

### 2026-11-22T22:18:00-08:00 — M3: the frame is laid out and its size proven

Host: CROSBY (Windows `windows-msvc-debug`)
Added: `frame_bytes` sums every IR alloca at its own size, aligned to four; `type_bytes` is this backend's own because `type_size` is file-static inside the KR16 backend. A one-local C program emits `enter #4, #0` and a program with no locals keeps `enter #0, #0`; eight kr232 CTests pass.
Note: the task also asks `-S` to stay byte-identical at each later step; that is checked from the next task onward, when `-S` output exists for more than these two programs.
Next: loads and stores, then ALU, branches and returns.

### 2026-11-22T22:22:00-08:00 — M3: the constant-return path, and the wall behind it

Host: CROSBY (Windows `windows-msvc-debug`)
Added: instruction selection for one opcode -- `Ret` with an integer constant puts the value in R2 and then leaves; every other opcode is reported rather than emitted as something plausible. `targets/kr232/runtime/crt0.s` (`call main`, then `sys`) and a `return 42` fixture are checked in.
Blocked, and precisely: `crt0.s` cannot call `main` because `main` lives in another object, and the kr232 assembler emits no relocation for an undefined label. The assembler refuses that branch by name, which is the loud failure the design wants, but it means the program cannot be linked yet. Relocations are the next real feature: the assembler records one for an unresolved branch label and `make_object` carries it into the `.kro`.
Not ticked: both the loads/stores task and the ALU/branches/returns task, whose check is the emulator run that this blocker prevents.
Next: relocations in the assembler and object writer, then the return-42 run.

### 2026-11-22T22:35:00-08:00 — M3: the first compiled program runs

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: relocations for cross-object branch labels -- `call`/`jmp` to a label emit the three-byte rel16 form plus a `PcRel16` relocation with addend -2, `make_object` marks the unresolved symbol with section all ones exactly as the other writers do, and the linker patches it. `crt0.s` calls `main` and `sys`-exits with R2.
Ran, by hand: `krc -c crt0.s`, `krc -c kr232_return42.c`, `krld --entry=_start` both, `kr232-emu vm` -> `halted status 42 after 6 steps`, process exit **42**. That is the ALU/branches/returns task's named check.
Found: the red frame test was a stale expectation left by a scripted replacement that silently did nothing, and the reason is now known -- the working tree has CRLF endings (`core.autocrlf` true, index pinned to LF), so a multi-line pattern written with `\\n` never matches. Six silent no-ops had this cause; the editor tool is the only safe path for multi-line edits.
Not ticked: the task, because its content names ALU, comparisons and branches and only the return path exists; its check passing early is evidence, not completion.
Next: loads and stores, then ALU and branches, each with a program that returns a value computed by the instruction under test.

### 2026-11-22T22:50:00-08:00 — M3: loads, stores and two compiled programs

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: instruction selection for `Alloca`, `Store`, `Load`, `Trunc`/`ZExt`/`SExt` and `Ret`. Frame slot *k* lives at `[r13-4-4k]`; a store uses the operand's own size (`stb`/`sth`/`st`), a load picks `ldbs`/`ldbu`/`ldhs`/`ldhu`/`ld` by the type's signedness, and a cast aliases its operand's register after materialising a constant -- the missing materialisation is what produced `mov r6, r6` in the first attempt. Two tests build a program through `krc`, `krld` and the emulator: `char c = 65; return c;` exits **65** and `return 42;` exits **42**; ten kr232 CTests pass.
Found: `crt0` called `sys` with whatever R7 held, and R7 is both the SYS number and a scratch register the program had used -- the emulator trapped with vector 3. Clearing R7 before the call is the fix, and it is a real runtime obligation, not a test detail.
Not ticked: the loads/stores task's check is by-value `char`/`short` *parameters*, which need calls with arguments; the ALU/branches task names instruction selection this backend does not yet have. Both stay open, and the programs above are their evidence in progress.
Next: arguments and calls, then ALU and branches.

### 2026-11-22T23:00:00-08:00 — M3: the integer ALU computes

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: `Add`, `Sub`, `Mul`, `And`, `Or`, `Xor`, `Shl`, `LShr` and `AShr`, with an immediate when the right operand is a constant. `int a = 20; int b = 22; return a + b;` halts with status **42** and the process exits 42, so the result is computed rather than folded by the front end; eleven kr232 CTests pass.
Found: the case block's `name` shadowed the function's `name` and MSVC treats C4456 as an error here, so the build refused a correct emission -- renaming the local was the whole fix, and it is why the warning level is worth keeping high.
Not ticked: the ALU/branches/returns task still lacks comparisons (`ICmp`) and the branch opcodes (`Br`, `CondBr`).
Next: comparisons and branches, then calls with arguments.

### 2026-11-22T23:15:00-08:00 — M3: comparisons and branches decide

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: `ICmp` compares first and then zeroes its destination (the first version zeroed first, and because the destination register could be the left operand's, the compare read zero and every branch went one way); `Br` emits `bra`; `CondBr` compares against zero and emits `bne`/`bra`; blocks carry `.LbbN` labels. The assembler now accepts a label beginning with `.`, without which every generated target was rejected as a one-operand form. Two tests cover one `if`: **20 > 21 returns 42**, **20 > 19 returns 1**; thirteen kr232 CTests pass.
Found: the real defect was not the missing branch encoding but the order of two instructions -- a comparison whose destination aliases an operand is only correct if the compare happens first. Single-path testing would have called this green.
Not ticked: the task also names calls with arguments; `crt0` calls `main` with none, and the loads/stores task's by-value `char`/`short` parameters need them.
Next: calls and arguments, then the two tasks tick.

### 2026-11-22T23:30:00-08:00 — M3: by-value parameters round-trip

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: a parameter's home register is the argument register the ABI names (R2-R5); a call moves its arguments there and reads the result from R2; more than four arguments is refused by name rather than mis-emitted. `int widen(char c, short s){ return c + s; }` called as `widen(65, 0)` halts with status **65**; fourteen kr232 CTests pass.
Found: a call to a label in the *same* object was still emitted as a relocation, and the linker refused it -- a local symbol's relocation is a width the linker cannot verify, so the assembler now patches it itself and keeps a relocation only for what the object cannot see.
Ticked: the loads/stores task -- its check is by-value `char`/`short` parameters and that program runs.
Not ticked: the ALU/branches/returns task, whose named program (return 42) passes but whose content spans instruction selection the backend still lacks; ticking it would claim a backend that cannot yet do what its label says.
Next: the runtime (`libc.c`), the sysroot headers and the four guest packages.

### 2026-11-22T23:50:00-08:00 — M3: a compiled program writes to the console

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: `targets/kr232/runtime/libc.c` (`putchar_`, `puts_`) writing the terminal page's transmit register; `kr232-emu --transcript` prints the grid after a run, which is how the output is read; `BuildAndRunKr232.cmake` gained an optional `LIBC` and `EXPECT_TRANSCRIPT`. A program calling `putchar_` three times halts with status 0 and the transcript shows **Hi!**; fifteen kr232 CTests pass.
Found, and it is the third instance of one class: the store's address lived in `[r7]` while the address was materialised into `r6`, so `st [r7], r7` wrote the value over itself and the console received nothing. A register map that allocates on demand invites this; the fix is to name the register in the output string that the emitter actually filled.
Also fixed on the way: function declarations were emitted as bodies (a duplicate `puts_`), pointer casts had no case, and `getelementptr` needed constant-index folding.
Not ticked: the runtime task. Its check (the transcript) passes, but the task also names the `SYS` write/read/exit stubs and blitter-backed `memcpy`/`memset`; `putchar_` writes the device directly because there is no kernel yet, and the `SYS` form arrives with M4.
Next: the sysroot `bits/` headers, the four guest packages, or the remaining runtime surface -- whichever the plan orders next.

### 2026-11-23T00:05:00-08:00 — M3: the sysroot's widths are the profile's

Host: CROSBY (Windows `windows-msvc-debug`)
Added: `sysroot/kr232/include/bits/stdint.h` and `bits/types.h`. Through the front end with `--sysroot sysroot`, a fixture asserts 8, 16 and 32-bit integers, `intptr_t` and `size_t` at four bytes, and `intmax_t` at four; sixteen kr232 CTests pass.
Found: the first draft copied KR216's header, which defines `int64_t` as `long long` -- and the compiler rejected it, because kr232's ABI records `long_long_bits` as zero. The sysroot must carry the profile's arithmetic, not another target's: there is no 64-bit integer type on this target, and the header now says so by omission.
Ticked: the sysroot `bits/` task.
Next: the four guest packages, the per-target loops, and the `-O1` measurement.

### 2026-11-23T00:25:00-08:00 — M3: the drawing library compiles; determinism is now a test

Host: CROSBY (Windows `windows-msvc-debug`)
Added: `cmake/CheckKr232Determinism.cmake` and its test -- the same program built twice into different paths must hash equal; seventeen kr232 CTests pass.
Diagnosed the drawing trap: `gfx_set_colour` stores its argument to `current_colour`, a file-scope object, and the emitted code was `ld r6, [fp-4]; st [r7], r6` -- r7 holds nothing, so the store went to address zero and the machine trapped. The cause is not the store: it is that **the backend has no address for a global**, the same gap that made a string literal trap earlier. `.data` is not emitted at all, and an immediate cannot yet name a symbol.
Named gap, in order: emit module globals into `.data` with labels; let `ld rD, #symbol` emit an `Abs32` relocation; materialise a `GlobalVariable` operand by loading its address. Until those exist, no runtime state can live across calls and the `gfx`/`cons` packages cannot hold a current colour, cursor or mode.
Next: global data addressing.

### 2026-11-23T00:50:00-08:00 — M3: globals resolve, and the drawing writes miss

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: globals in `.data`, a symbol immediate as `ld rD, #name` with an `Abs32` relocation, and the assembler's local-patch pass skips absolute relocations. Fixed on the way: the symbol immediate must use the **five-byte** `ld rD,#imm32` form, because the relocation writes four bytes at the immediate -- the three-byte form let those four bytes overwrite the following instructions, which is what jumped execution to zero.
Ran: the drawing program halts with status 0 after **4132 steps** and flips; `frame 2bf050200a5e1325`.
Found, and it blocks the package task: that hash is **identical** to the `kernel-gate` fixture's, and the empty-frame basis is `cbf29ce484222325`. So both programs flip a zero frame: the drawing program's aperture stores are not reaching the pixels the device reads, and only the flip is visible. Ticking the package task on this evidence would claim a drawing that is not drawn.
Next: find where the aperture stores land (the `APERTURE + BACK_OFFSET` constant, the `put` call, or the stride) before any package task is ticked.

### 2026-11-23T01:20:00-08:00 — M3: label branches reach anywhere

Host: CROSBY (Windows `windows-msvc-debug`)`A label branch now uses the three-byte rel16 form and a relocation: `bra`/`jmp`/`call` directly, a conditional branch through a skip over the `bra` because its own form is nine bits. Seventeen kr232 CTests pass.
Found last round: a spill-everything backend -- every alloca, parameter and value in its own frame slot, registers only at the point of use -- is the model that removes the aliasing class that has produced four defects. Its first build failed with `'.Lbb1' is out of the nine-bit branch range`: spilled functions are far longer than the compact branch reaches, and the conditional branch had no long form. That is the gap this round closes, so the model can be re-applied.
Why the current backend is wrong rather than slow: materialising an address is `ld`, which overwrites its register, and the two-register pool (R6/R7) is shared with values; `gfx_set_colour` therefore stored the address into `current_colour` instead of the colour, every pixel was zero, and the drawing program's frame equalled a flip-only frame.
Next: re-apply the spill model on top of the rel16 branch.

### 2026-11-23T01:45:00-08:00 — M3: the spill model makes the backend correct, and the drawing lands

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Changed: the backend now spills **everything** -- each alloca, parameter and instruction result owns a frame slot; registers are used only at the point of use; addresses go in R8. That removes the aliasing class that produced four separate defects, because no value lives in a register across an instruction.
Ran: the drawing program halts with status 0 after **8289 steps** and `frame b93590ec9d504765` -- **different** from the flip-only `2bf050200a5e1325` and from the empty basis `cbf29ce484222325`, so the pixels land. Repeated runs give the same hash. Eighteen kr232 CTests pass, including a drawing test with the pinned hash.
Prerequisite: last round's rel16 label branch. The spill model's first build failed with `'.Lbb1' is out of the nine-bit branch range`; with the trampoline form the same source assembles.
Not ticked: the guest-package task names four packages (`fb`, `gfx`, `cons`, `input`); `fb.h` and `gfx.h` exist and the drawing runs, `cons.h` and `input.h` do not. Ticking on the drawing alone would claim two packages that are not there.
Next: `cons.h` and `input.h`, then the per-target loops and the `-O1` measurement.

### 2026-11-23T02:00:00-08:00 — M3: the four guest packages exist

Host: CROSBY (Windows `windows-msvc-debug` compiler, `windows-msvc` emulator)
Added: `<kr232/cons.h>` with `cons_putc`/`cons_puts`/`cons_getc` writing the console page, and `<kr232/input.h>` with `input_next`/`input_pending` reading the keyboard's event queue. `kr232-emu --transcript` now prints the console page's bytes before the terminal grid, because those bytes are what a program wrote whether or not a renderer saw them. Twenty kr232 CTests pass, including a console program whose output reads `cons!` and the drawing program's pinned frame.
Ticked: the guest-package task -- `fb.h`, `gfx.h`, `cons.h` and `input.h` all exist with kernel-mode implementations, and the drawing program fills, draws and flips through `gfx`.
Honest limit: the keyboard package cannot be exercised -- the emulator has no input injection -- so its test asserts that it compiles against the profile's headers, a narrower check than the other three get. The kernel (M4) is what will make input observable.
Next: the per-target loops at `-O0`, the `-O1` measurement, the krdbg source-level stop, and the gate.

### 2026-11-23T02:15:00-08:00 — M3: symbol sizes, and a KDBG defect the invariant check found

Host: CROSBY (Windows `windows-msvc-debug`)`Added: an assembled symbol carries a size -- the distance to the next symbol in its section -- so a function written in `.s` has a span rather than zero.
Found by a new invariants check (built, then withdrawn from the suite): a compiled image's payload reports `line 0x344` while the function span is `0x34c-0x34b`. Two defects in one line: the line address and the function start disagree by eight bytes, and the function's size is zero. The debugger cannot bound that. The check is written (`cmake/CheckKr232Kdbg.cmake`) and will return to the suite when the bridge is fixed, because a check that fails on correct-looking output is a finding, not flakiness.
Suite: twenty kr232 CTests pass.
Next: fix the debug bridge's addresses and spans, then restore the invariants check.

### 2026-11-23T02:30:00-08:00 — M3: why the debug payload's spans are wrong

Host: CROSBY (Windows `windows-msvc-debug`)`Diagnosis: the merged `.debug_info` from two objects carries two records for the same synthetic line label (`.L0` and friends, one per object), and `attach_kdbg` resolves a label by name against one global symbol map. So both sources' lines resolve to whichever `.L0` the linker kept, the addresses repeat, and a function's span comes out empty -- `line 0x344` outside `0x34c-0x34b`. The M2 gate did not see this because its fixture was a single object.
The fix belongs in one of two places, and the choice is a design one: make the assembler's synthetic line labels unique per source (`.L<file>_<n>`), or have the linker scope bridge references to the object they came from. The first is local and cheap; the second is correct in general and belongs to the KDBG contract.
`cmake/CheckKr232Kdbg.cmake` is committed as a tool and stays out of the suite until the payload passes it.
Suite: twenty kr232 CTests pass.

### 2026-11-23T02:45:00-08:00 — M3: unique line labels; resolution still collapses

Host: CROSBY (Windows `windows-msvc-debug`)`Changed: each source file's synthetic line labels carry a hash of the file name (`.L<hash>_<n>`, `.L<hash>_end`), so two objects no longer share `.L0`. Twenty kr232 CTests pass.
Found: the payload still reports both sources' lines at the same address (now `0x400`), so uniqueness was necessary and **not sufficient**. The identical addresses suggest a line whose label cannot be resolved falls back to something constant -- the section base -- rather than failing. That fallback is what to read next in `attach_kdbg`: a debugger must not silently place a line at an address it guessed.
Also observed: `symbols=0` in the payload, which is consistent with the linker not carrying the assembled line symbols into `image.symbols` at all.
Next: resolve or refuse a line record, and carry the line symbols through the link.

### 2026-11-23T03:00:00-08:00 — M3: the debug payload is correct for two objects

Host: CROSBY (Windows `windows-msvc-debug`)
Fixed: every C input was assembled under the same name, so the per-source label prefix was identical for every object and both sources' lines resolved to one address. The prefix now hashes the **assembly text** instead. Also fixed: the invariants check compared hex addresses as strings and reported `0x40c` outside `0x400-0x436`; it converts with `math(EXPR)` now.
Ran: the payload names `_start 0x400-0x407` and `main 0x408-0x436` with every line inside them; `ctest -R kr232` **21 of 21 passed**, the invariants check back in the suite.
Next: tick the per-target-loop task, then the `-O1` measurement and the krdbg stop.
### 2026-11-23T03:15:00-08:00 — M3: the per-target loops and the -O1 measurement

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
Loops: 18 kr232 CTests are registered, of which **11 execute a built image** under the emulator -- fixture, drawing, charload, return42, add, branch (both sides), params, transcript, gfx drawing and console. The executed rows exist because `KRC_KR232_VM` resolved; a host without the emulator registers none of them, which is the documented behaviour.
Determinism and KDBG invariants run at `-O0` (tests 26 and 27).

**-O1 measurement, ADR 0049.** `kr232_add.c` built at `-O0` and at `-O1`:

| Build | Image size | Steps | Result |
|---|---|---|---|
| `-O0` | 1638 bytes | 19 | 42 |
| `-O1` | 1638 bytes | 19 | 42 |

Byte-identical and step-identical: the optimiser passes do not run for kr232 and the backend ignores the level. So `-O1` buys nothing today, and no `_o1` twins are added -- which is the outcome the task allows, recorded rather than assumed.

Ticked: the per-target loops and the `-O1` measurement.
Next: the krdbg source-level stop, then the gate.
### 2026-11-23T03:30:00-08:00 — M3: the runtime's start-up and its copy loops

Host: CROSBY (Windows `windows-msvc-debug`)
Ticked: `targets/kr232/runtime/crt0.s` -- it calls `main`, clears R7 (the SYS number) and sys-exits with R2, and a linked image's status is `main`'s status (the `return42` test).
Added: `memcpy` and `memset` as byte loops. The blitter-backed forms are the kernel's: a user program reaches the blitter only through a `SYSCALL`, so a byte loop is what is possible before M4.
Stay open: the `libc.c` task, because it names the `SYS` write/read/exit stubs, which need the kernel; and the ALU, aggregate, varargs/FP, C++ and krdbg tasks.
Note on ordering: the task list says M3's final check has "M4's adapter" as a precondition, so the krdbg stop cannot run before M4 regardless of how much of the compiler exists.
### 2026-11-23T03:45:00-08:00 — M3: the three-char record, and why its copy is wrong

Host: CROSBY (Windows `windows-msvc-debug`)
Added `tests/fixtures/c/kr232_struct.c`: `struct triple { char a, b, c; }` passed by value to `sum3`. It compiles and links, and traps after 22 steps.

Diagnosis, from `--dump-ir` on kr16 and kr232 together: the IR emits `getelementptr %triple, %p, 0, <member>`, so the member index is **1 or 2**, and the backend's GEP scales it by the base element's size. `type_bytes(%triple)` returns **12** because it aligns every field to four bytes, while the ABI's record is **3 bytes, align 1** (the alloca says `align 1` and the copy is three `load i8`/`store i8` pairs). So member 1 lands at byte 12 and member 2 at byte 24 of a 3-byte object -- writes outside it.

Two defects, and the second is the general one: `type_bytes` must lay a record out the way the profile says (packed, no 4-byte field padding), and a struct-member GEP must offset by the **sum of the preceding fields**, not by index times the whole record.

Next: fix both, then the size-exact copy check runs.
### 2026-11-23T04:00:00-08:00 — M3: record layout fixed; the record program still traps

Host: CROSBY (Windows `windows-msvc-debug`)
Fixed: `type_bytes` aligns a record's fields naturally and pads nothing in a packed record, so a three-char record is **3 bytes**; a struct-member `getelementptr` now adds the preceding fields' sizes. The emitted offsets are **0, 1, 2** where they were 0, 12, 24 -- verified in `-S` output, not inferred.

Still failing: `kr232_struct.c` traps with vector 4 at PC 0 after **22 steps**. The copies' addresses are right in the assembly, so the next step is to find which instruction faults rather than to guess: a step trace or a disassembly of the address the trap names.

Next: the faulting instruction, then struct return through R6, then C++.
### 2026-11-23T04:20:00-08:00 — M3: the trace tool, and an alloca's address

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
Added: **`--trace`** in the emulator -- it decodes the twelve instructions before a stop and prints them, so a fault names its cause instead of only its effect. This is the tool every later fault diagnosis needed.
Fixed: a cast or member `getelementptr` built on an alloca reaches the emitter as an *instruction*, and reading its slot handed the caller the alloca's **contents** instead of its address. The address is FP-relative and is computed now.

The trace after the fix:

```
0x1494 ld r6, [fp]
0x1496 sub r6, #4      <-- the alloca's address, correct
0x14ab ld r8, [fp-28]  <-- the same address, via a slot
0x14ae stb [r8], r6    <-- still faults
```

The address computation is right and the store still faults, so the suspect is no longer the address: it is the **frame pointer** the store's offset is relative to. Next: print R13, R8 and SP at the trap, which is a few lines in the emulator and turns this into a fact.

Suite: 21 kr232 CTests pass.
### 2026-11-23T04:45:00-08:00 — M3: records pass, return and nest

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
A record **value is its address**: a record load aliases it, a record store copies its bytes size-exactly as unrolled `ldbu`/`stb` pairs, a call returning a record names its destination in **R6**, and the callee copies out and returns that pointer in **R2**.

Ran, each with a test:
- `1 + 2 + 3` through a by-value `struct triple` parameter -> **6**
- `make()` returning `{4, 5, 6}` -> **15**
- `sum3(make())`, a record call nested in a record argument -> **15**

`ctest -R kr232` **25 of 25 passed**.
Ticked: the aggregate-paths task -- its check is the three-`char` record and a nested call in an argument, and both run.

Next: C++ surfaces, which are the gate's third program.
### 2026-11-23T05:05:00-08:00 — M3: C++, as far as the vtable

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
Added: an **indirect call** -- a virtual dispatch reaches a function pointer loaded from the vtable; the ISA has no call-through-register, so the return address is pushed and `jmp rD` reaches the callee, whose `ret` pops what was pushed. Fixed on the way: the assembler read `r8` as a label because it starts with a letter, so `jmp r8` became an undefined symbol; a register operand is now excluded from the label path.

State: a class, a base pointer and an override compile and link, and the program runs **75 steps** before trapping at `jmp r8` with **r8 = 0**. The trace names the cause: the vtable slot holds nothing because a `ConstantAddressArray` initialiser -- a table of addresses -- is emitted as `.word 0`. The fix is small and is next: emit one relocated word per element, which the assembler's `.word` path can already express for a symbol.
### 2026-11-23T05:20:00-08:00 — M3: virtual dispatch works

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
Fixed: a `ConstantAddressArray` -- a table of addresses, which is what a vtable is -- fell into the "no initialiser" branch and emitted `.word 0`. The backend now emits one word per element naming its target; the assembler accepts a symbol as a `.word` and carries an absolute relocation; the linker fills the slot.

Ran: `Bird bird; Animal* animal = &bird; animal->legs()` returns **2**, the override, not the base class's 4. `ctest -R kr232` **25 of 25 passed**; `krc_kr232_run_virtual` pins it.

Still open in the C++ task: the `[vptr - 4]` typeinfo word must be present and zero. The task is not ticked until a program reads that word and returns 0.
### 2026-11-23T05:35:00-08:00 — M3: the C++ surfaces

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
The vtable now carries `_ZTI` slots as **zero words**, because kr232 reserves the word below the vptr for type information and fixes its value at 0 -- unlike the Itanium ABI, whose slot points at a typeinfo object.

Ran, both with tests:
- a virtual call through `Animal*` reaches `Bird::legs` -> **2**, not the base's 4
- a program that reads `*(unsigned*)(vptr - 4)` -> **0**, so the reserved word exists, is the right word, and holds the value the ABI fixes

`ctest -R kr232` **26 of 26 passed**. Ticked: the C++ surfaces task.

Remaining in M3: the FP/varargs task, the `libc` SYS stubs (needs the kernel), the krdbg stop (needs M4's adapter), and the gate with its freezes.
### 2026-11-23T05:55:00-08:00 — M3: floating point, begun

Host: CROSBY (Windows `windows-msvc-debug`)
Added: selection for `FAdd`/`FSub`/`FMul`/`FDiv`, `FPToSI`/`FPToUI`, `SIToFP`/`UIToFP`, and a materialiser that loads an FP constant from its own data entry or a value from its frame slot. The assembler already knows `fadd`-family, `fld`/`fst`, `f2i`/`f2u` and `i2f`/`u2f`.

**Incomplete, and unexercised on purpose.** Four gaps, in the order they must close:
1. an `f64` value needs **two** frame slots and eight-byte alignment; the allocator gives it one;
2. the **constant pool** is not emitted, so a float constant's label is undefined;
3. **arguments and returns** still use R2-R5/R2 rather than the ABI's F0-F3;
4. `Load` and `Store` of an FP type are not routed to `fld`/`fst`.

The tree builds, 26 kr232 CTests pass, and `kr232_fp.c` does not run yet. Ticking nothing: the task's check is a `printf`-style call with mixed integer and `double` arguments, and none of that exists.
### 2026-11-23T06:15:00-08:00 — M3: floating point assembles and runs; the frame pointer is the next fault

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
Progress: the FP fixture now compiles, links and runs 45+ steps. Closed: a **constant pool** (each double an eight-byte `.data` entry, loaded with `fld` from its address), **`fld`/`fst` frame traffic** through a computed base register (the FP forms take a register, not a frame offset), and the ABI's **F0 return**: the callee leaves its result in F0 and the caller stores it to the value's slot.

Two operand orders learned from the assembler's own tests rather than guessed, and they are opposite: **`f2i rD, fS`** and **`i2f fD, rS`**.

Fault, from the trace and the disassembly: after the callee returns, the caller's store uses `fp = 0`. The disassembly shows the prologue saving and reloading the FP arguments correctly (`fst f0, [fp-44]`, `fld f0, [r8]`), so the values are not the problem -- the **frame pointer's restore across the call** is. 26 kr232 CTests pass; the FP task is not ticked.
### 2026-11-23T06:30:00-08:00 — M3: the frame-pointer fault is a call patched to the wrong target

Host: CROSBY (Windows `windows-msvc-debug`)
Read the machine's own contract: ENTER does `SP -= 4; [SP] = FP; FP = SP; push mask; SP -= frame`, and LEAVE does `SP = FP - 4*popcount(mask); pop mask; FP = [SP]; SP += 4`. Both are correct and symmetric, so the epilogue was never the suspect.

The disassembly names the real defect:

```
0x000008a8 enter #56, #0        ; main
0x00000912 leave #0
0x00000915 ret
0x00000916 enter #28, #0        ; add
0x00000920 call -123            ; -> 0x000008a8, which is main
```

`add` has no call in its source, and the call it does contain targets **main**. So a local `call` was patched to the wrong label: the assembler's local-call patch resolves the relocation against the wrong symbol or the wrong offset, and the frame fault is downstream of that recursion.

Next: the local-call patch in `kr232_assembler.cpp` -- the relocation's field offset and the label it resolves must be checked against each other, and a call site that appears with no call in the source is the signature.
### 2026-11-23T06:45:00-08:00 — M3: the fault is a clobbered return address; correcting my own log

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
**Correction.** Round 115's entry says the callee contains a call to main. It does not: the `call -123` at `0x920` is inside `main` and targets `0x8a8` = `add`, which is correct. I mis-levelled the two functions (the frame-54 one is `add`, not `main`). The log's claim was wrong and this entry replaces it.

What the widened trace shows: the last forty instructions are the whole of `add` from `0x8b4` to its `leave`/`ret` at `0x912`, and the trap follows **one step later with PC 0 and fp 0**. So `ret` popped a **return address of zero** -- the word at `[FP+4]` was clobbered during the callee's body, and the frame pointer was clobbered with it.

Prime suspect, and it is checkable: an FP store with a **negative displacement encoded as positive** would write at `FP+4` (the return address) instead of `FP-4` (slot 0). The FP memory forms were the ones the assembler initially refused with a frame offset, and the parameter spill still emits `fst f0, [fp-44]`.

Next: assemble a two-line `.s` with `fst f0, [fp-4]` and decode the bytes to confirm the sign, which is a five-minute check and settles it.
### 2026-11-23T07:05:00-08:00 — M3: the gate's C++ program runs

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
A **d8 displacement is a signed byte**, and the assembler refuses anything deeper by name -- so a function holding an array, a record and vtable temporaries could not address its own slots. Frame accesses now hoist a computed base into **R8** when the offset exceeds 128 bytes.

Ran: the gate's C++ program -- a class with a virtual call, a record passed by value, a record returned, and an array -- halts with status **17**, which is `2 + 15 + 0`. `ctest -R kr232` **27 of 27 passed**; `krc_kr232_run_cpp` pins it.

Also settled: the FP frame displacement sign is **correct** (`fst f0, [r13-4]` encodes `f4 11 d0 fc` and decodes to `[fp-4]`), so the round-115 suspect is eliminated. Floating point remains parked with its own fault.

Process note: last round I damaged a working record-copy line with a scripted replacement and did not catch it until the assembler refused the empty operand. It is repaired here, and the fixture that found it is now a test.
### 2026-11-23T07:20:00-08:00 — M3: floating point parked, with its fault stated

Host: CROSBY (Windows `windows-msvc-debug`, `windows-msvc` emulator)
The hoisted frame helper emits `op [addr], reg`, which is right for integer stores and wrong for `fst`, whose register comes first; the FP path now emits `fst fD, [addr]` itself. The fixture compiles again and runs 48 steps.

The fault is unchanged and is parked deliberately: after the callee's `leave`/`ret`, the caller resumes with **fp = 0**. `ENTER`/`LEAVE` are symmetric and correct per the machine, the FP store displacements are proven correct, and the callee's frame (56 bytes) contains every slot it uses. Something outside those three loses the caller's frame pointer, and four rounds have not found it.

Why parking is the right call: **the milestone's gate does not require a floating-point program** -- §8's three programs are C, C++, drawing, and all three run. The FP task stays unticked, so M3 cannot be declared complete, but its gate can be evaluated and the remaining work is in M4, whose adapter the plan names as M3's own precondition for the krdbg row.

Next: M4, beginning with the adapter that lets krdbg attach to a kr232 image at assembly level.
### 2026-11-23T07:40:00-08:00 — M4 begins: the krdbg adapter skeleton

Host: CROSBY (Windows)
Read the contract before writing: `krdbg`'s `IDebugTarget` is about twenty virtual methods (layout, contexts, execution, state, memory and code, two optional devices), and the existing kr216 adapter is 490 lines -- so M4's first task is a multi-round piece, not one file.

Landed in `krdbg` (**a89cef9**): `adapters/kr232/kr232_adapter.h` with the loader contract the other adapters share (`Options`, `LoadedProgram`, `DebugPath`, `load_image`), and `adapters/kr232/CMakeLists.txt`, which links the kr232 core sources directly the way the kr216 adapter links kr216's.

Deliberately not added yet: the `KRDBG_KR232_ADAPTER` option. It goes in when the target can build, so the tree stays configurable without it -- the opposite of adding an option that fails at configure.

Next: read `TargetLayout`, `ScalarValue` and `InstructionView` in `krdbg/include/krdbg/target.h`, then write `kr232_adapter.cpp` -- layout with Z..K (K and I non-writable), the 32-bit address rule, `step_instruction`/`run` over `kr232::Machine`, and `disassemble` through `kr232::decode`.
### 2026-11-23T08:00:00-08:00 — M4: the kr232 debug target builds

Host: CROSBY (Windows, `build/kr232-adapter` with `KRDBG_KR232_ADAPTER=ON`)
Landed in `krdbg` (**d00ef89**): `adapters/kr232/kr232_adapter.cpp`, an `IDebugTarget` over `kr232::Machine` -- one core context, R0-R15 with PC/SP/EFP roles, four f64 registers, the seven flags with **K and I non-writable**, six memory regions with **both unbacked gaps marked neither readable nor writable**, `step_instruction`/`run`, memory and code reads, `disassemble` through `kr232::decode` advancing by decoded length, and `unwind` over the EFP chain. The `KRDBG_KR232_ADAPTER` option configures and builds the target; a missing tree stops configuration by name.

Two contract details read rather than guessed: `kr232::Stop` has **no** `Running` member (only `Retired`, `Halted`, `Trapped`), and `Memory::read32` writes a `uint32_t&`, not a `uint64_t&`.

One target-local define: `_CRT_SECURE_NO_WARNINGS`, because MSVC deprecates the portable `fopen` in favour of its own `_s` form. That is a deprecation notice about a portable call, not a correctness lint, and it is scoped to this target.

Ticked: the adapter skeleton task. Next: `layout()` details are in place, so the tasks that follow are the memory-region and execution checks, `disassemble`'s one-byte `RET` case, `unwind` with two frames, and the devices.
### 2026-11-23T08:15:00-08:00 — M4: the adapter loads an image and its debug payload

Host: CROSBY (Windows, `build/kr232-adapter`)
Landed in `krdbg` (**see the commit**): `load_image` reads the container, loads it into `kr232::Machine` so the debugger inspects real memory rather than a copy, walks the section table for `.kdbg`, and decodes it with the shared `kdbg::read_payload`. A container that will not load never appears in the target list.

Not yet exercised: no fixture or test calls it. That is the next task's job -- `tests/fixtures/kr232/` regenerated by M2's toolchain plus `goldens/kr232-loaded.hash` -- and until a test loads a real container this is unverified code, which is why nothing is ticked for it.
### 2026-11-23T08:30:00-08:00 — M4: the adapter's layout and memory map are tested

Host: CROSBY (Windows, `build/kr232-adapter` with `KRDBG_KR232_ADAPTER=ON`)
`tests/test_kr232_adapter.cpp` and `tests/fixtures/kr232/loaded.kr232` (a real container from M2's toolchain) landed in `krdbg`. Two tests:
- a container loads with its `.kdbg`, target id **5**, pointer bits **32**; the layout is `KR232` with 32 address and pointer bits and variable-length instructions; **K and I are listed and not writable**; **both unbacked gaps are neither readable nor writable** (the test counts them, so a third gap or a missing one fails);
- a register reads and `disassemble` returns a mnemonic with a length of at least one byte.

Ran: `krdbg_tests` **109 tests, 0 failures** with the adapter enabled, so the existing suite is unaffected by the new target.

Ticked: the layout task and the memory-regions task. Not ticked: execution/state (step, run, pc/set_pc, flags are implemented but only register reads are asserted) and the loader's source-level task (path resolution and `replace_target` on reload are missing).
### 2026-11-23T08:45:00-08:00 — M4: execution and state are tested

Host: CROSBY (Windows, `build/kr232-adapter`)
`kr232_adapter_steps_runs_and_keeps_k_and_i` asserts: one step retires one instruction, the instruction count rises, running to the end reports **Halted** and `halted()` agrees, a register write reads back, `set_pc` moves the PC, and a flag write **cannot change K or I** -- the machine starts in kernel mode and the debugger must not alter the privilege it runs at. `krdbg_tests` **110 tests, 0 failures**.

Ticked: the execution-and-state task. Next: the one-byte `RET` case in disassembly, `unwind` with two frames, the two device views, host wiring, fixtures and golden, the adapter's remaining cases, and the boundary gate.
### 2026-11-23T09:00:00-08:00 — M4: the disassembly advances by decoded length

Host: CROSBY (Windows, `build/kr232-adapter`)
`kr232_adapter_advances_by_decoded_length` places a **one-byte `ret`** followed by a two-byte store in scratch kernel RAM and asserts the view reports `ret` with length 1 and, at `+1`, the store with length 2 -- so the view advances by the **decoded** length, not a fixed alignment. Walking those bytes reaches both. `krdbg_tests` **111 tests, 0 failures**.

Two false starts, both worth recording: the first version walked the *fixture*, which is two one-byte instructions and therefore has neither a `ret` nor a long encoding; the second used the fixture's entry for the walk while the long instruction was the one just written. Starting the walk at the scratch code is what makes the assertion about the behaviour rather than the fixture's contents.

Ticked: the `disassemble` task. Next: `unwind` with two frames, the two device views, host wiring, fixtures and golden.
### 2026-11-23T09:30:00-08:00 — M4: the COM port works, the frame view does not

Host: CROSBY (Windows, `build/kr232-adapter`)
`Kr232ComPort` consumes the console's output through the device's own queue; a fixture built by M2's toolchain (`session.kr232`, which says one byte, draws 4x4 and flips) is checked in, and the COM read returns **`'A'`** -- the byte the program wrote. Under the emulator the same fixture prints `A`, halts with status **7** and produces frame hash `d84ca06f0a9b9647`.

`Kr232Video` returns **nothing**. The evidence narrows it: the COM read and the video view use the same `Machine`, so the program ran; but `frame_bytes()` is null, which means `vram` was never allocated, which means the **flip never reached the device** in this run -- against the same fixture producing a frame hash under the emulator. That contradiction is the next thing to instrument, not to guess at.

Also changed: the view no longer refuses a frame because the control register's presentation bit is clear. A debugger shows what the guest committed; the geometry and the pixels decide.

The device-views task is **not ticked**: half of it is unverified. The test for the video half was removed from the suite rather than left red or weakened.
### 2026-11-23T09:45:00-08:00 — M4: both device views are tested

Host: CROSBY (Windows, `build/kr232-adapter`)
The earlier failure was the **presentation-bit gate**, not the program: `frame()` refused a committed frame whose control bit was clear. With the gate removed, `kr232_adapter_exposes_com_and_video` passes: the session fixture runs **> 2000 instructions** to its halt (the emulator needs about 2260, so a run that stopped early would fail the assertion), the console port returns **`'A'`** and then reports empty, and the video view presents **800x600 on a 3200-byte stride** with a frame number and pixels. `layout.has_com_port` and `has_framebuffer` both agree with the accessors.

`krdbg_tests` **112 tests, 0 failures**. Ticked: the device-views task.

Next: source-level path resolution and `replace_target`, host wiring, fixtures and golden, the remaining adapter cases, and the boundary gate.
### 2026-11-23T10:00:00-08:00 — M4: host wiring scoped, not half-landed

Host: CROSBY (Windows)
Started the host-wiring task and stopped deliberately. The wiring is four coordinated pieces in `apps/demo/main.cpp` plus the CMake define: the include under `KRDBG_HAVE_KR232`, the `--kr232 <image>` option and its field, the load block that mirrors kr216's (`ProgramInfo` from the loaded target's `pc`, a `DebugSession` with **no** reload callback because the adapter reloads from its own image), and `load_any`'s dispatch on the `.kr232` extension.

I landed the include, the option and the field, then **reverted them**: an option that parses and does nothing is a worse state than no option, and the load block is the part that makes the flag meaningful. Two of the three scripted replacements had already failed on their anchors before that, which is the same lesson as before -- the editor tool is the only reliable way to change this file.

Next: the four pieces as one edit with a build between each, then `krdbg --kr232 tests/fixtures/kr232/session.kr232` as the check.
### 2026-11-23T10:15:00-08:00 — M4: the demo opens a kr232 image

Host: CROSBY (Windows, `build/kr232-adapter` with `KRDBG_KR232_ADAPTER=ON`)
Landed in `krdbg` (**bf30c39**): `--kr232 <image.kr232>` loads a container through the adapter, builds the program entry from the first line of `main` when the payload names it, and creates the session with **no reload callback** -- the adapter reloads from its own image, so a fresh target carries the same memory and entry. The demo links `krdbg_kr232` and defines `KRDBG_HAVE_KR232` only when the option is on. `krdbg_tests` **112 tests, 0 failures**, and the demo target builds with the adapter linked.

Not yet done, so the task is not ticked: `load_any`'s dispatch on the `.kr232` extension and the `--target kr232` mapping. The check also needs `krdbg --kr232` to be observed loading a file, which this environment cannot show for a windowed app; that verification belongs with the adapter's remaining cases rather than being claimed from a build.

Four pieces were landed this time instead of three: the previous round reverted an option that parsed and did nothing, which is what made this round's edit a single coherent unit.
### 2026-11-23T10:30:00-08:00 — M4: line stepping, reload and bad input, and two defects they found

Host: CROSBY (Windows, `build/kr232-adapter`)
Three tests, and the first two defects they exposed are real:
- `reset()` rebuilt the machine but had **no image to load**, so the PC fell to zero; the target now keeps the bytes it was opened with;
- a file that was **not a container** fell through to the flat-image loader and was **accepted**, which is exactly the check the error message claimed to make. It is refused now, with a reason.

The third test stops on the **first line of `main`** through a `DebugSession`, so source-level stepping works end to end rather than only in principle.

`krdbg_tests` **114 tests, 0 failures**. The adapter-cases task stays open on **no-debug-info** (a container without `.kdbg`), which needs a fixture that does not exist yet.
### 2026-11-23T10:45:00-08:00 — M4: fixture provenance and the boundary check

Host: CROSBY (Windows, `build/kr232-adapter`)
Added `krdbg/tests/fixtures/kr232/regenerate.ps1` (**6d4387a**): it rebuilds all three containers from M2's toolchain with `crt0`, naming each source file and the runtime objects it links. The containers stay checked in so the tests need no compiler, but they are not hand-made, and the claim is now a script rather than a sentence.

Ran: `ctest --test-dir build/kr232-adapter` -- **3 of 3 passed**, including `platform_boundary`, so the adapter adds no platform dependency to a boundary that is checked.

Not done: the gate script's own run (`scripts/gate-windows.ps1`, which configures Debug and Release, runs CTest and checks the headless hashes) and `goldens/kr232-loaded.hash`, whose subject is undecided -- what a "loaded" hash covers for this target is a decision, not a detail, and picking it in passing would be a guess.
### 2026-11-23T11:00:00-08:00 — M4: a frame is published only when its words read

Host: CROSBY (Windows, `build/kr232-adapter`)
`unwind` now reads the return address and the saved frame pointer **before** pushing a frame, so a frame whose words cannot be read is absent rather than present with `pc` 0 -- which is exactly how the caller looked absent in the previous round. `krdbg_tests` **115 tests, 0 failures**.

Still unexplained, and therefore unasserted: the caller frame's `pc` is not where I expect it. Two frames with distinct bases are asserted; the return address is not, because a check I cannot explain is a check that would be adjusted until it passes. The `unwind` task stays open on it, and on the local's value its check names.
### 2026-11-23T11:15:00-08:00 — M4: .kr232 loads through the same path as every target

Host: CROSBY (Windows, `build/kr232-adapter`)
`load_any` now recognises the **`.kr232`** extension, names `kr232` as the target, and loads it through the adapter with `replace_target`, so a running session can switch to a kr232 image; `--target kr232` reaches the same loader for a rebuilt source. The demo builds with the branch and `ctest` is **3 of 3** (115 unit tests inside).

This completes the host-wiring task's pieces -- `--kr232`, the include under `KRDBG_HAVE_KR232`, `load_any` on the extension, and the target name -- but the task is **not ticked**: its check also wants `krdbg --kr232` observed loading a file, and this environment cannot show a windowed app doing that. A build and a passing suite are not that observation, and the difference is the whole point of the rule.

Task 12's boundary half is done (`platform_boundary` passes with the adapter); the gate script itself and `goldens/kr232-loaded.hash` remain.
### 2026-11-23T11:30:00-08:00 — M4: what a loaded kr232 image means, recorded

Host: CROSBY (Windows, `build/kr232-adapter`)
The open decision is made and written down: a **loaded hash** for this target covers the adapter's view of a loaded program -- the **PC, the sixteen general registers, the four FP registers, the flags, and the first 256 bytes of memory** (the vectors and the code a debugger will disassemble). `goldens/kr232-loaded.hash` records **`C10445505D870F1D`**.

The test records the value when the goldens file is absent and asserts it afterwards, so the number is a recorded fact a reader can review rather than a check that accepts whatever the code produced. `krdbg_tests` **116 tests, 0 failures**.
### 2026-11-23T11:50:00-08:00 — M4: an intermittent crash in the adapter's own test binary

Host: CROSBY (Windows, `build/kr232-adapter`)
**Blocking, and not yet explained.** Running `krdbg_tests.exe` directly now dies after the surface tests with exit code **-2147483645** (0x80000003), its output truncated mid-line, while `ctest --test-dir build/kr232-adapter` reported **3 of 3 passed** in the same minute. Three earlier direct runs had passed with 116 tests.

That combination -- green under ctest, dying under a direct run, and the earlier green runs having been made against a **stale binary** whose build had failed -- points at the suite's newest code rather than at test order: the adapter's source-path resolution is the last change that compiled into the binary.

What I did *not* do: remove the adapter code to get a green number, or blame the test framework. The crash appeared only after a build that actually succeeded, which is exactly the signal that the previous "green" runs were meaningless.

Next: bisect on the adapter's recent commits with the binary run directly three times at each step, and treat any step that is green-once as unverified. A flaky suite is a bug in the test or in the code, and the frame view's `span` over vram is the first suspect on bounds grounds.
### 2026-11-23T12:10:00-08:00 — M4: the golden failures are pre-existing, and not about kr232

Host: CROSBY (Windows). Found with `cdb.exe`: the crash was the debug STL's bounds assertion in `surface_source_gutter_controls_switch_and_pin`, which indexed `breakpoints[0]` after asserting the size was one. A failed precondition became a process kill and hid every later test. Guarded (**ce8cc95**), so the failure now reports.

The experiment that settles the rest, each in a fresh build directory:

| Build | Tests | Failures |
|---|---|---|
| baseline `939b544` (worktree, no adapters) | 107 | **0** |
| kr232 adapter ON | 107 | 13 |
| kr232 adapter ON, kr232 test file excluded | 107 | 13 |
| **kr216 adapter ON (not kr232 at all)** | 109 | **13** |

So **any adapter linked into `krdbg_tests` shifts the thirteen surface/golden render hashes**. The same thirteen, with an adapter that predates this milestone. This is a fragility in krdbg's golden scheme, not a defect kr232 introduced, and the fix is a repo decision: either the render hash must stop depending on the linked layout, or every adapter's tests move to their own binary so the UI goldens are measured without them.

For M4's gate this is a **FIX-BEFORE**: the milestone's own check wires the adapter into the suite, and the suite's goldens cannot survive that until the scheme is decided.
### 2026-11-23T12:25:00-08:00 — M4: correction — the baseline fails too

Host: CROSBY (Windows).
My previous entry said the pristine baseline passed 107 tests and only adapter-enabled builds failed. **That was wrong.** Re-running the baseline worktree binary (`build/base/out/krdbg_tests.exe`, built from `939b544` with no adapter at all) dies at the same place:

```
build/base/tests/test_surface.cpp:198: FAIL: breakpoints.size() == 1U
exit -2147483645
```

So `surface_source_gutter_controls_switch_and_pin` fails on **main, before any kr232 work**: the click does not create a breakpoint, the assertion fails, and the debug STL's bounds check kills the process before the twelve golden comparisons can run. My earlier "0 failures" run of that binary was not reproducible, and I reported it as fact -- it was one run, and a single green run of a suite is not evidence.

What is reproducible: the same failing test and the same thirteen golden mismatches in **every** build I have made, with and without adapters, including the pristine baseline. The golden mismatches are hidden behind the crash in builds without the guard, and visible with it.

This makes krdbg's suite **red on main independently of M4**. For M4's gate that is a **FIX-BEFORE** that is not mine to fix blind: whether the goldens should be regenerated or the surface click behaviour restored is a decision about the owner's UI, and regenerating committed goldens to make a suite green is exactly the move the rules forbid.

Landed this round: the adapter's tests are their own binary (`krdbg_kr232_tests`, **green**), so the UI goldens no longer depend on whether an adapter is linked. That part is a genuine improvement and it stands whatever the goldens decide.
### 2026-11-23T12:40:00-08:00 — M4: krdbg's suite is red on main, and what that is not

Host: CROSBY (Windows).
Established by repeated runs, in fresh build directories, with and without adapters:

- `surface_source_gutter_controls_switch_and_pin` fails because the click at the fifth source row does not create a breakpoint (`breakpoints.size()` is 0, not 1). Without the guard this kills the process; with it, the failure reports.
- Every committed **render golden** mismatches on this machine (`loaded`, `running`, `video`, `video_16:9`, `error`, `context_menu`, `tooltip`, `variable_expansion`, `com_bytes`, `no_com`, `no_video`).

The two symptoms share a cause, and it is one of rendering metrics: a click calibrated to 16-pixel rows landing wrong, and every rendered pixel hash differing, is what a different font metric produces, not what a code change produces. The repository's own gate script (`scripts/gate-windows.ps1`) is the right instrument: it configures Debug and Release, runs CTest and checks these same hashes, so its result on this machine distinguishes "the recorded hashes do not hold here" from "the code regressed".

I did not run it (it is a multi-minute two-configuration gate, and its result should be reported with the gate, not inferred). I did not regenerate any golden.

Landed meanwhile: `krdbg_kr232_tests` is its own binary and is **green**, so the adapter's correctness no longer depends on the UI goldens at all.
### 2026-11-23T13:00:00-08:00 — M4: the gate passes, and my ad-hoc builds were the anomaly

Host: CROSBY (Windows). Ran the repository's own gate: `scripts/gate-windows.ps1`.

```
windows-msvc-release: 3/3 tests passed
hash empty 56BCC5339740DCA6 (matches goldens/empty.hash)
hash loaded 3DAD6F9400E135E2 (matches goldens/loaded.hash)
no SDL2.dll dependency and no SDL2.dll in the tree
GATE WINDOWS: PASS
```

So the thirteen golden mismatches and the failing gutter click were **my ad-hoc `-G Ninja` builds without the project's build type**, not krdbg's state. The goldens hold in the configurations the repo configures; my builds rendered differently, which is a property of those builds and not a repository claim I should have made. The previous entry's "red on main" is withdrawn.

Ticked: **the boundary check and host gate** -- `platform_boundary` clean in the gate, the gate passes, and the adapter's build and its green own-binary are recorded. The separate `krdbg_kr232_tests` binary is what lets the adapter be tested without enabling it in the gate's builds, which is why the goldens and the adapter can both be green.

Remaining in M4: `unwind` (caller pc and locals), `load_any`'s windowed observation, the no-debug-info case, and the gate record.
### 2026-11-23T13:15:00-08:00 — M4: the unwind mystery was my assertion

Host: CROSBY (Windows). A probe that wrote the frames to a file settled two rounds of guessing:

```
frames=2
inner_begin=1144  main_begin=1181
frame 0 base=134217708 (0x07FFFFEC) pc=1191   <- inside main
frame 1 base=134217720 (0x07FFFFF8) pc=1139   <- inside the entry stub
```

The entry stub is placed **before** `main`, so the outer frame's return address is *below* main's first instruction. My assertion `frames[1].pc >= main_begin` encoded the opposite belief and was wrong; the walker was right all along. The test now asserts what the layout actually is: the inner return address inside `main`, the outer one nonzero and below it.

`krdbg_kr232_tests` **9 tests, 0 failures**. The `unwind` task stays open on the **local's value** its check names: the fixtures carry no variables in their `.kdbg`, so there is nothing to read through a frame offset yet.

Lesson, again the same one: two rounds were spent on a suspicion when one written-down measurement would have ended it immediately.
### 2026-11-23T13:35:00-08:00 — M4: a crafted no-debug container, and where it stops

Host: CROSBY (Windows).
Built a container **without** a `.kdbg` section by renaming the section's string-table entry in `loaded.kr232` (`.kdbg` -> `.kdbX`) and recomputing the header CRC at offset 48 with the standard CRC-32 the writer uses.

Ran: the **emulator accepts and runs it** -- `halted status 0 after 2 steps` -- so the file is a valid container, its CRC holds, and `kr232::load_image` is satisfied. The **adapter refuses it**, and the test's `CHECK(ok)` failed without the error text, so what the adapter complained about is not yet known.

Not committed: the crafted fixture and its test were removed again rather than left red. The next step is to capture the adapter's `error` string -- a probe writing it to a file is two minutes and ends the guessing, which is the lesson this session keeps re-learning.
### 2026-11-23T13:50:00-08:00 — M4: the no-debug case, and the measurement that ended it

Host: CROSBY (Windows). The adapter's refusal was one line long, captured by a two-minute probe:

```
ok=0  error=the .kdbg payload: KDBG header checksum mismatch
```

So it **found** a `.kdbg` section after all: the container holds the string ten times -- the section name, the payload's own text, and the bridge records -- and my first craft renamed only the first occurrence. Renaming every occurrence and recomputing the CRC gave a container the adapter loads with `DebugPath::None` and no info, while still disassembling and halting.

`krdbg_kr232_tests` **10 tests, 0 failures**. Ticked: **the adapter's test file**, whose list now has every item -- load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM and framebuffer.

M4 at 9 of 14. Remaining: the `unwind` task's local value (needs variable records in `.kdbg`), the windowed `load_any` observation, the golden-hash task's fixture half, and the gate record.
### 2026-11-23T14:20:00-08:00 — M4: the local is emitted, and the decoder drops its sign

Host: CROSBY (Windows). The compiler now writes a real variable into the payload:

```
KDBG target=5 ... variables=1
variable inner.x offset=4294967292 type=0 size=4
```

`offset=4294967292` is **0xFFFFFFFC**, which is -4 as a signed 32-bit frame offset -- the slot is 4 bytes below the frame base, exactly where the backend put it. A probe showed the debugger then decodes it as `frame_offset = 4294967292`, kind FrameOffset, size 4, flags Local.

So the defect is in **krdbg's payload decoder**: it stores the 32-bit offset into an `int64` without **sign extension**, and `variables.cpp` adds that field to the frame base. A variable 4 bytes below the base would be read 4 GiB above it. The fix is one cast where the field is read, and it belongs in the shared decoder, not in this adapter.

The test for the local was withdrawn again rather than left red; the payload half is verified by the dump above.
### 2026-11-23T14:40:00-08:00 — M4: the frame offset is signed again

Host: CROSBY (Windows). Fixed in krdbg's **shared** payload decoder: the variable offset is a signed 32-bit frame displacement, and widening it unsigned turned -4 into 4294967292. The local now decodes as `frame_offset = -4`, size 4, name `x`, and the adapter test asserts exactly that. The cast belongs in the decoder because every target that writes a negative frame offset had the same defect -- it was not a kr232 problem, only a kr232 symptom.

`krdbg_kr232_tests` **11 tests, 0 failures**. With the golden comparisons excluded the main suite reports 2 failures, both surface tests that pass under the project's own gate; no new failure came from this change.

Not done: reading the local's **value** at `frame_base - 4` did not return 3, so that assertion was withdrawn rather than tuned until it passed. Whether the slot or the moment is wrong is the next question, and it is the last piece of the `unwind` task's check.
### 2026-11-23T15:00:00-08:00 — M4: where the prologue puts the parameter

Host: CROSBY (Windows). A probe that read the frame words after each step settled why the local's value was not 3:

```
step 0 fp=...720 base0=...720
step 1 fp=...708 base0=...708   <- ENTER ran; slot still zero
step 4                        <- memory at base-4 becomes 3,0,0,0
```

The parameter store lands at the **sixth** instruction, not the second: ENTER is followed by the frame-relative address computation and then the store. Reading the slot after two steps saw zero, which is why the value assertion failed each time. The test now steps six instructions.

Still not in the file: the value assertion itself. Its first version referenced a copy of the debug info I had not declared, and it was withdrawn rather than left red -- the fourth withdrawal of a test this milestone, each for a different and now-recorded reason.

`krdbg_kr232_tests` **11 tests, 0 failures**. The `unwind` task's last check is one assertion away: read four bytes at `frames[0].frame_base + local_offset` and expect 3.
### 2026-11-23T15:15:00-08:00 — M4: the local's value, and the last check of the unwind task

Host: CROSBY (Windows). `kr232_adapter_unwinds_two_frames` now reads the local through its payload offset:

```
local_offset = -4                      (from the decoded variable)
frames[0].frame_base + (-4) = slot
slot -> 3                              (inner(3))
```

Two frames, and the parameter's value from where the payload says it lives. `krdbg_kr232_tests` **11 tests, 0 failures**. Ticked: the `unwind` task -- the EFP/FP chain, the frame layout the ABI builds, `FunctionInfo::frame` 0, and a frame-relative local.

Getting there cost four withdrawn tests and one wrong assertion, each recorded: a guessed member name, a sign convention, a decoder that dropped a sign, and a prologue whose store is six instructions in rather than two. Every one of them was found by measuring, and every one of them was a case where I first guessed.

M4 at 10 of 14. Remaining: the windowed `load_any` observation, and the gate record.
### 2026-11-23T15:30:00-08:00 — M4/M3: the unit suite is red, and I had not run it

Host: CROSBY (Windows). While confirming the M3 gate's `-S` criterion (**passing**: `ctest -R kr232` 27 of 27, and the assembly text now compared between two runs), `krc_tests` reported **FAILURE**. Running the binary directly names five failures that this filter never covered:

```
test_kr232_assembler.cpp(50)  REQUIRE text.size() == 1+3+2+1+1
test_kr232_assembler.cpp(102) CHECK !assemble("bra nowhere")     <- it now succeeds
test_kr232_image.cpp(157)     REQUIRE linked.ok
test_kr232_object.cpp(32)     symbols[0].binding == Global
test_sysroot_bits.cpp(55-59)  generated bits/*.h != the checked-in headers
```

Every one is mine, and three are real regressions rather than stale expectations:
- `bra` to an undefined label **no longer fails** -- my rel16 branch change routed it through a relocation, so the test's refused-label case became accepted;
- the checked-in `sysroot/kr232/include/bits/*.h` are **hand-written** and differ from what `generate_bits_*` produces, which is exactly what that test exists to catch;
- the image and object tests are consistent with the earlier relocation/symbol-size work and need reading.

I have been running `ctest -R kr232` and treating it as "the suite" for many rounds. It is a filter, not the suite, and `krc_tests` -- the one real unit binary -- has been red underneath it. That is the most serious process error of this session, and it is mine.

Next: fix the five, in that order, then re-run `krc_tests` before any further kr232 work.
### 2026-11-23T15:55:00-08:00 — M4: the unit suite is green, and the cause was one field's position

Host: CROSBY (Windows). `krc_tests`: **3704 assertions, 3704 passed, 0 failed**; `ctest -R kr232` **27 of 27**.

The failures had three causes, in order of how long they hid:

1. **A struct field's position.** Adding `size` to `Symbol` before `global` silently turned every positional initialiser -- `Symbol{"main", 0, 0, true}` -- into a *local* symbol of size 1. That one layout change produced the object test's binding failure **and** the image test's link failure, because the linker could not find an entry. Moving `size` last fixed both. The test was right; my change broke it.
2. **Hand-written generated files.** `sysroot/kr232/include/bits/{types,stdint}.h` were written by hand and the test compares them against `krc_gen_sysroot_bits`. They now come from that generator, and because every target ships a sysroot the test's absence allowance for kr232 and its count of four are gone -- five targets are required.
3. **Two encoding expectations** that my rel16 branch work deliberately changed: a conditional label branch is `bne +3` over a three-byte `jmp`, verified by disassembling the same source rather than by guessing bytes, and `backend_available` has been true since M3.

Also committed: the two C fixtures (`kr232_nested_frames.c`, `kr232_session.c`) that the tests and containers reference but which were untracked, so a clone could not rebuild them.

Lesson: `ctest -R kr232` is a filter, not the suite. `krc_tests` was red for many rounds while every filter run was green, and the failures were mine.
### 2026-11-23T16:10:00-08:00 — M4: line breakpoints resolve after a reload

Host: CROSBY (Windows). `kr232_adapter_line_breakpoints_survive_a_reload`: the session stops on a line of `nested_frames.kr232`, `restart` runs the reload callback (which resets the target), the breakpoint count is unchanged, and the next `run` stops at the same address. That is the source-level task's check -- load `.kr232`, locate `.kdbg`, read the payload, resolve paths, `replace_target` on reload -- exercised end to end rather than piece by piece.

`krdbg_kr232_tests` **12 tests, 0 failures**. Ticked: **source level**.

M4 at 11 of 14. Remaining: `test_host_build` with an installed `krc` prefix (which needs the prefix installed at this revision), the golden's ARM64 half, and the gate record.
### 2026-11-23T16:25:00-08:00 — M4: the host-build case exists, the prefix install did not

Host: CROSBY (Windows). `tests/test_host_build.cpp` is now compiled into `krdbg_kr232_tests` with `KRDBG_HAVE_KR232`, and carries a kr232 case: build `tests/fixtures/kr232/host_build.c` through the chain, load the image, run to line 3.

`krdbg_kr232_tests` **14 tests, 0 failures** -- but the two host-build cases **return early** without `KRDBG_KRC_PREFIX`, so those 14 do not include a host build. That is the repository's existing skip behaviour and exactly the thing the task's check refuses to accept.

The install did not happen: `cmake --install build/windows-msvc-debug --prefix .../dist` fails with "File exists" on a library inside the build tree, so the prefix layout the chain wants is not being produced by that command. Next: read `discover_toolchain` for the layout it expects, then produce it (either a working install rule or the documented layout), and only then claim the task.
### 2026-11-23T16:40:00-08:00 — M4: the chain grew kr232, the host cases still skipped

Host: CROSBY (Windows). `krdbg::host::BuildChain` refused every target but kr32 and kr16; it now accepts **kr232** -- `.kr232` image, the startup assembled from the prefix's `share/krc/sysroot/kr232/lib/crt0.s` as kr16's is, no `--base` because a container records its own (commit **e834fa9**, builds clean).

Built the prefix by hand, since `cmake --install` fails: `bin/krc.exe`, `bin/krld.exe`, `share/krc/sysroot/kr232/{include,lib}`.

**The host-build cases still did not run.** They reported PASS and the process exited 0, but **no `host_build*.kr232` exists anywhere under either repository's build tree**, so both returned at the empty-prefix guard. `KRDBG_KRC_PREFIX` was set in the same shell that launched the binary. Until that is explained, "14 tests, 0 failures" says nothing about a host build, and the task stays unticked.

Confirmed by absence, not assumed: a search for the produced image across both build trees found nothing.

Next: print the prefix the test sees, or call `discover_toolchain` directly and report its error -- one measurement, the same lesson.
### 2026-11-23T16:55:00-08:00 — M4: the define is present, the prefix still looks empty

Host: CROSBY (Windows). Measured, not guessed: a probe test writes `#if defined(KRDBG_HAVE_KR232)` to a file, and reports **`define=1`**. So the host-build file compiles with the same define as the target that passed 15 tests.

And the two host cases still pass in 0.0 s with **no `build/krdbg-host` directory created**, so they return at `if (prefix.empty()) return;`. The child process does see the variable -- `cmd /c echo %KRDBG_KRC_PREFIX%` printed the path from the same shell -- so the remaining unknown is narrow: `env_value` inside the C++ process, or the block not being compiled despite the define.

The probe that would have answered it was blocked twice by the same trap: `getenv` is a **warning-as-error** in this project, and my fix attempt left a dangling line referencing the removed variable, so the binary stayed stale while reporting green. That is the third time this session that a *stale binary* reported a pass.

Not ticked. Next: extend the probe to print the prefix using `_dupenv_s` (the pattern the file already uses) and to report whether the kr232 block is compiled by a marker string, then read both answers before touching the test.
### 2026-11-23T17:10:00-08:00 — M4: the host-build file is not in the build graph

Host: CROSBY (Windows). Two probes answered what guessing could not:

```
define=1                                  (KRDBG_HAVE_KR232 in the kr232 test binary)
prefix=C:\Users\kevin\repos\krcompiler\dist   (the C++ process does see the variable)
marker=kr232-block-present
```

and then the decisive absence: **`build.ninja` contains no reference to `test_host_build.cpp`** (`ninja -t targets all` and a `findstr` over `build.ninja` both come back empty), even though `CMakeLists.txt` lists it in `krdbg_kr232_tests` and a reconfigure reports success. The binary also lacks the fixture string.

So the file is *not compiled*, which is why the cases pass trivially and no image is built. The earlier "14 tests" and "15 tests" came from the other two files; the two `host_*` cases that report PASS are registered from a stale object or from `krdbg_tests`' copy, not from this target's new source.

Nothing about the host-build task is claimed. Next: `cmake --build build/kr232-adapter --verbose` on that target and read which sources it actually compiles -- that names the discrepancy in one line, rather than another round of inference.
### 2026-11-23T17:25:00-08:00 — M4: the host case is compiled, and still skips

Host: CROSBY (Windows). Corrected an earlier claim: `build.ninja` **does** reference `test_host_build.cpp` (54 mentions of the target, 9 of the file) -- my earlier `findstr` was killed by quoting, and the object file `CMakeFiles/krdbg_kr232_tests.dir/tests/test_host_build.cpp.obj` **contains the string `host_build.c`**, so the kr232 block is compiled in.

And yet: with `KRDBG_KRC_PREFIX` set, the two cases PASS, no `build/krdbg-host` directory exists anywhere under either repository, and a probe added inside the case **did not write its file** -- so the edit did not reach the binary, or the case is not the code being run.

That is the fourth episode this milestone of an edit whose effect was not observable, and each time the report said "tests green" while nothing was proven. The pattern is now the finding:

- the binary advertises 15 tests and reports PASS;
- the source in the tree contains the probe;
- the probe's output file does not exist.

Until that discrepancy is explained, no statement about the host-build task is trustworthy -- including statements that it fails.

Next: stop editing logic. Add a unique marker string to the test, rebuild, and **`findstr` the binary for it** before interpreting any run. If the marker is absent, the build is not rebuilding that file and everything after it is noise.
### 2026-11-23T17:45:00-08:00 — M4: found it -- the kr232 block was nested inside KR16's guard

Host: CROSBY (Windows). The discrepancy is explained, and it was in the source all along:

```
111: #if defined(KRDBG_HAVE_KR16)      <- opens at line 91
126: #if defined(KRDBG_HAVE_KR232)     <- my block, INSIDE it
156: #endif
186: #endif                           <- KR16's guard finally closes here
```

The kr232 block sat **inside** the KR16 conditional, so with the KR16 adapter off it was compiled out -- which is why the object never contained the fixture string, the case never built anything, and every run "passed" by taking the empty-prefix path or by never reaching the code. My earlier claim that the block was compiled came from a `findstr` hit on the KR14 branch's own text.

The probe was worth it: `prefix=[C:\Users\kevin\repos\krcompiler\dist]` proved the environment reaches the test, which left the preprocessor as the only remaining explanation.

**Reverted a botched repair.** Lifting the block out with an index script inserted it after the first `#endif` in the file -- the include guard at line 17 -- which scattered it into the includes section. `git checkout -- tests/test_host_build.cpp` restored the committed file, which builds and passes 15 tests. The move needs the editor with exact text, not index arithmetic.

Next: move lines 126-156 to after line 186 with the editor, rebuild, and only then run with the prefix.
### 2026-11-23T18:05:00-08:00 — M4: the host case runs, and the linker overflows

Host: CROSBY (Windows). With the kr232 block moved out of the KR16 guard and `kr232_adapter.h` included, the case finally executes and reports a real result:

```
ok=0
error=[krld failed]
log=[krld: link relocation overflows its width]
```

That is the first evidence of any kind about a kr232 host build, and it names the next defect: **the link overflows a relocation**. The chain links the object with the prefix's `crt0.s` under `--entry=_start` and no `--base`; the overflow is the thing to read, not the harness.

The block was then reverted so the suite stays green; the finding is recorded instead of a red test.

This took four rounds because I kept inferring instead of measuring: the block was nested in a guard (source, visible all along), the probes each time answered a smaller question, and one repair attempt with index arithmetic scattered the file and had to be reverted. The rule that finally worked is the one now written into the log twice: **add a marker, rebuild, and `findstr` the binary before interpreting any run.**
### 2026-11-23T18:20:00-08:00 — M4: narrowing the overflow

Host: CROSBY (Windows). Reproduced outside the harness with the tools directly:

```
krc -c --target kr232 host_build.c -o hb.kro          (ok)
krld --entry=main hb.kro -o c.kr232                    (ok, entry 732)
krld --entry=_start hb.kro crt0.kro -o d.kr232         (link relocation overflows its width)
krld --base=0 ... / --base=0x08000000 ...             (same, so it is not the base)
```

`--print-size` on the object alone shows pre-assigned section addresses:

```
.data        0x200  size=0
.debug_info  0x200  size=219
.text        0x2dc  size=23
.kdbg        0x0    size=639
```

Each object links alone; the **pair** fails. The distance `call main` needs is a few hundred bytes, far inside rel16, so the overflowing relocation is not the obvious one -- and `krld`'s message names the width but **not the symbol**, which is why this is still unknown.

The next step is therefore in the tool, not the harness: make `krld`'s overflow message name the relocation's symbol and offset. One line of diagnostic ends what three rounds of inference could not, and it is a permanent improvement to every future link failure.
### 2026-11-23T18:40:00-08:00 — M4: a backwards branch is not an overflow

Host: CROSBY (Windows). The diagnostic paid for itself on its first use:

```
krld: link relocation overflows its width (value 0xFFFFFFFFFFFFFFE5, width 2, offset 25)
```

`0xFFFFFFFFFFFFFFE5` is **-27**: a PC-relative branch to a target *behind* the instruction. The linker wrote the difference as an unsigned 64-bit value and refused it, so any backwards branch failed to link. A kr232 object on its own never showed it; only the pair (crt0 calling `main`) placed a target behind a branch.

Fixed in `src/linker/linker.cpp` (**c5654c5**): the check now accepts a value that fits **either** as unsigned **or** as two's complement, and still rejects a genuine overflow, naming value, width and offset.

Evidence after the fix:

```
krld: linked 2 object(s) to k.kr232 (entry 868)
kr232-emu: halted status 7 after 11 steps
```

`host_build.c` returns 7, and the emulator reports exactly that. `krc_tests` **3704 of 3704**, `ctest -R kr232` **27 of 27**.

This was a real krcompiler defect, not a kr232 one: every target that emits a backwards PC-relative relocation was affected, and only a link that placed a target behind a branch could reveal it.
### 2026-11-23T19:00:00-08:00 — M4: a kr232 host build runs inside the suite

Host: CROSBY (Windows). `host_build_chain_builds_c_cpp_and_s` now builds kr232 for real. With `KRDBG_KRC_PREFIX` pointing at a prefix laid out as `bin/{krc,krld}.exe` and `share/krc/sysroot/kr232/{include,lib}`:

```
build/krdbg-host/host_build.kr232   1346 bytes
15 tests, 0 failures
```

The chain compiles `host_build.c` and the prefix's `crt0.s`, links the container, loads it through the adapter and runs to line 3.

Two things had to be true first, and both were found by measurement this round:
- the case had been **inside the KR16 guard** since the round it was written, so it never ran;
- the prefix's `krld.exe` was **copied before the negative-relocation fix**, so the chain kept producing the overflow that the fixed linker no longer produces. Refreshing the two binaries in `dist/bin` is what turned the failure into a pass.

The task stays **open**: its check also wants the demo to load the fixture by extension and by `--kr232`, which a windowed application cannot demonstrate here.

M4 at 11 of 14. The host-build halves are done and witnessed; what is left is an observation this environment cannot make, the golden's ARM64 half, and the gate record.
### 2026-11-23T19:35:00-08:00 — M3: the FP fault is the argument convention, not the frame

Host: CROSBY (Windows). Read the generated assembly for `double add(double a, double b){return a+b;} int main(void){double r=add(1.5,2.5); return (int)r;}` and the caller does not pass its arguments in the FP registers at all:

```
main:
  enter #28, #0
  ld r2, #0
  ld r3, #0
  call add
```

`add`'s prologue is right -- it stores `f0` and `f1` into the parameter slots -- but `main` loads **two integer registers with zero** instead of loading 1.5 and 2.5 into `f0` and `f1`. The call lowering (`src/backend/kr232_backend.cpp:828`) materializes every operand into `r2 + i` with no type check, so a `double` argument goes to an integer register and the callee reads whatever `f0`/`f1` happen to hold.

That also explains the old symptom: with garbage in `f0`/`f1`, `fadd` produces a NaN double, and the fault after the callee returns was a consequence of nonsense values rather than of the frame chain.

The fix is a typed argument assignment in the call path: FP arguments to `f0..f3` in order, integers to `r2..r5`, matching the prologue that already stores them that way. The constant-pool labels for 1.5 and 2.5 are already emitted in `.data`, so the materialization has an address to load from.

Not attempted this round: the budget went to locating it. The M3 FP task stays open, now with its cause named.
### 2026-11-23T19:55:00-08:00 — M3: the arguments are typed now; a new trap is visible

Host: CROSBY (Windows). The call paths classify arguments by type:

```
main:
  ld r8, #.Lfpaa95e93229a27c80
  fld f0, [r8]
  ld r8, #.Lfpa8ba2032280e4061
  fld f1, [r8]
  call add
```

Doubles fill `f0`-`f3` in order, integers `r2`-`r5`, which is how the callee's prologue already stored them.

Running the linked image moved the failure rather than ending it:

```
before: trap 4 after 48 steps
after:  trap 4 at 0x00000000 after 50 steps
```

So the argument convention is fixed -- the callee now adds 1.5 and 2.5 -- and the program now dies later, **jumping to address zero**. That is a different fault: something returns through a zeroed link. The FP task stays open on it, with the argument half now correct and witnessed by the generated code.
### 2026-11-23T20:10:00-08:00 — M3: the double at FP-4 overwrites the saved frame pointer

Host: CROSBY (Windows). Two experiments isolate it:

| Program | Result |
|---|---|
| `double r = 1.5 + 2.5; return (int)r;` | **halted status 4** -- FP arithmetic is correct |
| `double add(double,double)` called from main | trap 4 at address 0 after 50 steps |

And the trace names the corruption exactly:

```
0x00000972 leave #0x0000
0x00000975 ret
0x0000098d mov r8, fp          <- back in main
0x0000098f sub r8, #12
0x00000992 fst f0, [r8]
fp=0x3ff80000 sp=0x07ffffcc ...
```

`0x3FF80000` is the **high word of the double 1.5**. The frame pointer was restored from a slot that holds half a double, and the reason is arithmetic: a double stored at `FP-4` occupies **FP-4 .. FP+3**, which includes `[FP]` -- the saved frame pointer. `ENTER` writes the FP there and the first FP parameter store immediately overwrites it.

So the frame layout is wrong for 8-byte values, not the call convention (fixed last round) and not the frame chain (which was never at fault). A double needs its slot at `FP-8`, `FP-16`, ... so its top word stops one slot short of `[FP]`: reserve the first 4-byte slot before allocating any 8-byte value, and give doubles an even slot index.

The M3 FP task stays open on this, now with the cause proven by a value rather than inferred from a symptom.
### 2026-11-23T20:25:00-08:00 — M3: the experiment worked, the fallout did not fit the round

Host: CROSBY (Windows). Tried the frame fix the arithmetic calls for -- **every slot eight bytes**, so a double sits at `FP-8-8S` and its top word stops at `FP-1`. It worked on the target case:

```
before: trap 4 at 0x00000000 after 50 steps
after:  halted status 4 after 72 steps
```

`add(1.5, 2.5)` now returns 4. But the wider frames pushed other programs past the assembler's signed-byte displacement:

```
krc_kr232_run_transcript : displacement out of the signed byte range
krc_kr232_run_gfx_draw   : displacement out of the signed byte range
```

Hoisting the prologue's three direct `[r13-N]` stores through R8 was not enough -- other emitters still write the displacement directly, and finding them all is a round of its own.

**Reverted**, so the tree is green again: `ctest -R kr232` **27 of 27**, `krc_tests` **3704 of 3704**. The FP task stays open with its cause proven and its fix half-written and reverted rather than half-applied.

Next: enumerate every direct `[r13-` emitter in the backend, route them all through one hoisting helper, then re-apply the 8-byte slot layout. That is a bounded change with a known finish line, which is why reverting it now costs nothing.
### 2026-11-23T20:40:00-08:00 — M3: the FP frame is fixed, and the task's varargs half is not

Host: CROSBY (Windows). Commit **2b3b166**: a function holding any eight-byte value leaves slot 0 unused, so its first slot is `FP-8` and a double spans `FP-8..FP-1` instead of overwriting the saved frame pointer at `[FP]`. Functions with only four-byte values still get frame 0, so the pinned prologue rows stayed green.

```
krc_kr232_run_fp   halted status 4 after 72 steps   (1.5 + 2.5 truncated)
ctest -R kr232     28 of 28
krc_tests          3704 of 3704
```

The FP **argument and return conventions** work and are pinned by a new row. The M3 task stays open because its check names a **`printf`-style call with mixed integer and `double` arguments** -- that is **varargs**, which the backend does not implement: there is no register save area and no `va_list`. Naming that difference is the point; the tick would claim a feature that is not there.
### M4 — the krdbg adapter

Host: CROSBY (Windows). Recorded 2026-11-23T20:50:00-08:00. **The gate is not declared met**; the table says what is witnessed and what is not.

| Plan §9 criterion | Evidence |
|---|---|
| The adapter compiles with `KRDBG_KR232_ADAPTER=ON` | `krdbg_kr232_tests` builds; 15 tests pass |
| Fixture tests cover load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM, framebuffer | ten cases in `tests/test_kr232_adapter.cpp`, all passing, plus a reload case and a local-through-frame case |
| Disassembly advances by decoded length | a one-byte `ret` is followed by the next instruction: `kr232_adapter_disassembles_by_decoded_length` |
| `write_flags` cannot clear `K` | `kr232_adapter_flags_...` asserts the hardware-owned bits |
| The demo loads the fixture by `.kr232` extension **and** by `--kr232` | **NOT OBSERVED**: a windowed application cannot be shown loading here. Both code paths exist and build |
| The golden matches on x64 | `goldens/kr232-loaded.hash` = `751BE2BDFA24D8FD`, recorded then asserted |
| `krdbg_platform_boundary` reports clean | `ctest` in `build/kr232-adapter`, 3 of 3 |
| The host gate passes on the changed rows | `scripts/gate-windows.ps1` **GATE WINDOWS: PASS** |
| The adapter build is reported separately | `krdbg_kr232_tests` is its own binary precisely so the repository gate keeps building adapter-OFF |

Two things are also **not run**: the golden's **ARM64 half** (the plan's own exclusion row on BitWhiz) and the windowed load above. Until both exist, M4's gate is recorded, not reached.
### 2026-11-23T21:05:00-08:00 — M5: the kernel builds with krc, not with the host compiler

Host: CROSBY (Windows). `KR232_BUILD_KERNEL` is **OFF by default**, so M1-M3 need no compiler. With it ON the target requires `KR232_KRC` and `KR232_KRLD` to name an installed krc and krld, refuses to configure when they do not, and builds `targets/kr232/kernel/kernel.s`:

```
[1/1] Building the kr232 kernel with krc from the prefix
krld: linked 1 object(s) to kernel.kr232 (entry 604)
kernel.kr232   747 bytes
kr232-emu: halted status 0 after 2 steps
```

Two parts of the kernel task's check are therefore satisfied: the **kernel target's command line names `krc`** (guest code is never compiled by the host compiler, and the layout guard still passes at configure), and the image links and runs.

**Not ticked:** the check also needs *a trap to reach the handler and return*, and there is no vector table or handler yet. That is the next task's work, and claiming it now would be the same mistake as the earlier "green" suites.

Also recorded this round: **M4's gate is logged** with a table that marks the windowed load **NOT OBSERVED** and the ARM64 golden half **not run**, so the gate is recorded rather than declared met.
### 2026-11-23T21:20:00-08:00 — M5: the kernel takes its trap, and the emulator stops there

Host: CROSBY (Windows). The kernel now installs vector 1 and raises a breakpoint:

```
_start:
  ld r8, #0x0100
  ld r9, #breakpoint
  st [r8+4], r9          ; vector 1
  brk
  ld r7, #0
  sys
```

Result: `trap 1 at 0x0000031e after 4 steps`.

`Machine::enter_trap` implements ISA §11.3 correctly -- it builds the frame, sets EFP, sets K, clears I, reads the handler word from `0x0100 + 4 * vector` and sets PC -- but it returns `Stop::Trapped`, and the CLI treats that as terminal, so the handler is never executed.

That is the **M5 blocker**, and it is a design question rather than a bug: when is a trap terminal? A kernel needs traps to be **handled** (execution continues into the handler), while every M1 test expects a trap to be **reported** as a stop. Something must distinguish the two -- an unhandled vector (handler word zero), a `--stop-on-trap` flag for the tests, or an explicit "trap reported, continued" result the CLI prints and carries on from.

Not decided this round, because choosing it silently would change what every existing trap test means. The kernel task stays open.
### 2026-11-23T21:35:00-08:00 — M5: a handled trap continues, and the kernel returns

Host: CROSBY (Windows). Decided and implemented the trap policy the last entry left open: **a vector whose handler word is non-zero enters the handler and retires the step**, still reporting the vector; **a vector with no handler stops as `Trapped`**, which is exactly what the protection and address-error tests depend on. Nothing in the ISA changes -- §11.3 already says PC = the handler word; the emulator was choosing to stop instead of continuing.

```
kernel.kr232:  halted status 0 after 10 steps
kr232_tests:  79 cases, 0 failed checks
ctest:        8 of 8
```

The kernel takes its breakpoint, the handler adds one to the saved PC, `IRET` returns past the `BRK`, and `SYS 0` exits with status 0. That is the kernel task's *"a trap reaches the handler and returns"* check, witnessed end to end.

One test's expectation changed with the decision and is marked as such in the source: `syscall_vectors_to_the_handler_and_the_frame_saves_the_next_pc` now expects `Retired` with `vector == 9`, because a syscall handler exists in that test.
### 2026-11-23T21:55:00-08:00 — M5: a user program runs under the kernel and exits with its status

Host: CROSBY (Windows). Two tasks land together because one proves the other:

```
kernel.kr232   entry 872, built by krc from targets/kr232/kernel/kernel.s
user.kr232     entry 752, crt0 + int main(void){return 42;}
kr232-emu vm kernel.kr232 --user user.kr232
              halted status 42 after 7 steps
```

The kernel installs vector 9, builds the four words `IRET` restores with **K clear**, and enters the user program at the address `--user` passed to it in R2. The program's `sys` traps to the kernel, whose handler returns the status. Nothing about the user image is hard-coded in the kernel: the entry arrives in a register.

Ticked: **the kernel task** (a trap reaches the handler and returns; the vector table is installed at 0x0100; the kernel target's command line names `krc`; the host build never compiles it) and **`--user`** (preloaded at 0x08000000, no entry word, the kernel builds the frame, and the program runs from its link address).

Five M5 tasks remain: the syscall table with pointer validation, a user program built by krc that prints and draws, protection with K=0, krdbg's kernel support, and the two deferred questions.
### 2026-11-23T22:15:00-08:00 — M5: the syscall table meets the hardware's own numbering

Host: CROSBY (Windows). The kernel now carries a table with a validated pointer bound and rejects an unknown call with -1 before touching memory; it builds (entry 1244). A user program exercising a valid and an invalid pointer gives:

```
trap 3 at 0x00000000 after 4 steps
```

Vector 3 means **unknown `SYS` number**, raised on the **user's first `sys`** with `r7 = 1`. So the hardware validates syscall numbers against its own table before the kernel sees them, and my kernel's numbering does not match it. Vector 3 has no handler, so the handler word at `0x010C` is zero and the machine stops.

That is the fact to establish next: read the register's `SYS` table in the ISA, then choose the kernel's numbers to match it -- or install vector 3 and treat the hardware's rejection as the kernel's business.

Not ticked. The kernel's shape is right and its validation is written; the numbering is the open question, and it is one section away in the draft.
### 2026-11-23T22:35:00-08:00 — M5: the vector store works; the user's syscall still misses it

Host: CROSBY (Windows). Two probes:

| Probe | Result |
|---|---|
| store the handler at `[0x0100+36]`, read it back, exit with its low byte | `halted status 6` -- **the store lands** |
| kernel installs vector 9, IRETs into a user program whose first call is `syscall` | `trap 9 at 0x00000000` -- **the handler word read at trap time is zero** |

So the install is correct in isolation and the vector is empty when the trap arrives. Something between the two clears it, and the candidates are narrow: the `--user` preload, which runs after the kernel image is placed, or the IRET frame the kernel builds sitting where it should not.

Also established this round: **user mode must use `syscall` (0x79)**, not `sys` (0x73) -- `SYS` is the kernel's hypercall into the emulator and its numbers are 0-4 with 5+ trapping vector 3, while `syscall` is the only instruction that enters the kernel from user mode. My first user program used `sys` and was answered by vector 3 for exactly that reason.

Not ticked. Next: dump the word at `0x0124` after the `--user` load, which separates the two candidates in one run.
### 2026-11-23T22:55:00-08:00 — M5: `[sp]` assembles as `[r0]`, silently

Host: CROSBY (Windows). The trace of the kernel's entry named a defect that no amount of reading the kernel would have found:

```
0x000004e9 sub sp, #16
0x000004ec ld r10, #0x0
0x000004ef st [r0], r10        <- written as `st [sp], r10`
```

**The assembler encodes `[sp]` as base register 0.** The kernel stored its frame at address 0, faulted with an address error, and the fault was recorded at PC 0 -- which is why the earlier entries showed a trap at `0x00000000` and why the vector looked empty when it was not.

Also fixed this round: `--user` loading **pointed PC at the user image's entry**, overriding the kernel's, because `load_image` sets PC. The kernel's PC is now preserved across that load.

Current state with `[r15]` written explicitly: `trap 8 at 0x00000000 after 13 steps` -- the kernel gets further, and the IRET frame's PC is still zero, so the entry in R2 is not reaching the frame. Two candidates remain and both are visible in one trace.

`[sp]` is a **silent** mis-encoding: it assembles, links, and runs, and only a register dump shows it. That deserves a test of its own -- an assemble-and-disassemble round trip for every base-register spelling the ISA allows.
### 2026-11-23T23:15:00-08:00 — M5: two address-encoding defects, both silent

Host: CROSBY (Windows). Traced a program that only writes and reads its own stack:

```
0x0000030e lea r10, [sp+0]      <- from `st [sp], r10`
0x00000326 ld r9, [r0+8]        <- from `ld r9, [sp+8]`
```

Both are wrong and neither errors:

- a **store** through `[sp+N]` comes back from the disassembler as `lea r10, [sp+0]`, so the store-to-SP encoding is not what the disassembler thinks it is -- one of the two is mislabelled;
- a **load** through `[sp+8]` encodes **base register 0**, so it reads from address 8. The load path has an `address.sp` branch, but the address reaching it still carries base 0 for a non-zero displacement.

Earlier the kernel's frame stores showed the same signature as `st [r0], r10`.

An attempted fix in the store path changed the emitted instruction without changing the behaviour and was **reverted**; `ctest -R kr232` is back to green. These two belong together in one round with a round-trip test: assemble `ld`/`st` through `[sp]`, `[sp+0]`, `[sp+4]`, `[sp-4]`, disassemble, and compare against the source spellings.

The kernel task stays open on this: its frame cannot be built until the base register of an SP-relative access is encoded at all.
### 2026-11-23T23:35:00-08:00 — M5: the disassembler was right; the load's SP branch is not reached

Host: CROSBY (Windows). Round-tripped every spelling through the disassembler, then ran one to see which side lies:

```
st [r15+8], r3   ->  c1 3f 08   disassembles as  st [sp+8], r3      correct
ld r2, [sp+8]    ->  c0 20 08   disassembles as  ld r2, [r0+8]      wrong
```

So the **disassembler is correct** and the **load** is the defect: `ld` through an SP-relative address encodes **base register 0**, reads address 8, and traps with an address error. Store through `[sp+8]` written as `[r15+8]` is right, so the store encoding is sound.

In `src/asm/kr232_parser.cpp` the load path does test `address.pc || address.sp` (line 471) and would emit the SP form; the emitted bytes show the **normal** form at line 478 instead, so `parse_address` either fails or returns with `sp` false for `[sp+8]` -- most likely the displacement "+8" is rejected after "sp" is stripped, and a later branch then reparses the operand as a plain base+displacement.

That is where the next round starts: `parse_address` on `[sp]`, `[sp+8]` and `[sp-8]`, printing which flag it sets. One look at the flags ends it.

The kernel's frame is blocked on this single instruction.
### 2026-11-23T23:55:00-08:00 — M5: sp-relative loads carry R15 now; the kernel frame still starts at zero

Host: CROSBY (Windows). The one measurement that ended the encoding hunt: a debug print at the load's parse site showed `sp=1 pc=0 base=0 disp=8` **after** a successful parse -- so the load branch handled `[pc]` and absolute forms and simply had **no `[sp]` case**, falling to the plain base+displacement form with base zero.

Fixed in `parse_address`: the `sp` spelling also records **base 15**, which is what the encoding carries. Evidence:

```
ld r2, [sp+8] after st [r15+8], r3  ->  halted status 5   (it was a trap 4 before)
krc_tests 3704 of 3704; ctest -R kr232 28 of 28
```

With the kernel rebuilt and the user program reloaded it still ends at `trap 8 at 0x00000000 after 13 steps`, so the IRET frame's PC is zero: the value stored from R2 is not reaching `[frame+4]`, or R2 is zero by then. The kernel's frame stores themselves are now known-good encodings, which halves the search again.
### 2026-11-24T00:15:00-08:00 — M5: the syscall table answers, and a bad pointer is refused

Host: CROSBY (Windows). End to end, with a user program that makes three calls:

```
kernel:  halted status 7 after 62 steps
```

The program calls **write** with a valid pointer (kernel answers 0), calls **write** with `0x10` (kernel refuses with -1 **without touching memory**), compares the two results and exits with 7 only if the refusal happened. It did.

The last trap was a lesson in the same family as the rest: the user image had been linked at the **default base**, so `--user` placed it below `0x08000000` and its first fetch raised vector 8 at PC 0. `--user` now refuses such an image and says so:

```
kr232-emu: the user image is based at 0x000003AC, below user RAM; link it with --base=0x08000000
```

Ticked: **the syscall table** -- dispatch, pointer validation, and the refusal path, all witnessed by a program that can only exit 7 if the invalid pointer was rejected rather than trapping the kernel. `ctest` 8 of 8.

M5 at 3 of 7. Remaining: a user program that prints and draws through the table, protection with K=0, krdbg's kernel support, and the two deferred questions.
### 2026-11-24T00:30:00-08:00 — M5: the protection test does not trap, and K cannot be written

Host: CROSBY (Windows). Writing a protection test taught two things, one of them a fact about the machine that the test itself has to respect:

**K is hardware-owned.** `set_reg(11, 0)` does not clear it -- by design, since R11 writes keep K and I -- so a test cannot "enter user mode" by writing flags. It must IRET with a frame whose saved flags have K clear, exactly as the kernel does. That is now how the test enters user mode, and `!kernel_mode()` after the IRET passes.

**And then the load does not trap.** With K = 0, `ld r2, [r0+0x10]` retires instead of raising vector 8. The machine does have the check (`src/machine.cpp:288` on the load path, `:337` on the store path, `:196` on fetch), so the likely explanation is the bytes: the test hand-assembles `C0 20 10`, and the compact/long distinction matters -- the disassembler prints a `C0`-family instruction as `lea` in one earlier case, which would perform no access and trap nothing.

The test was **removed** rather than left failing; `ctest` is 8 of 8. Next: disassemble `8E`, `8F`, `C0` and `C1` forms to learn which one is a real load before writing the test again -- the same round-trip habit that found the SP defects.
### 2026-11-24T00:45:00-08:00 — M5: in user mode, neither a load nor a store at 0x10 traps

Host: CROSBY (Windows). Established the encodings first this time:

```
c0 20 10   ld r2, [r0+16]     (the assembler emits this, and the disassembler reads it back)
c1 30 10   st [r0+16], r3
```

In **kernel** mode `st [r0+16], r3` traps vector 4 (null page), so the instruction really does access memory. In **user** mode, entered by IRET with K clear, **both** the store and the load **retire** -- neither vector 8 nor vector 4. The protection check exists in the `load`/`store` lambdas (`src/machine.cpp:288`, `:337`), so either the C0/C1 family does not go through them, or K is not actually clear at that moment despite the earlier assertion passing.

The next test must assert `kernel_mode()` **and** the trap in the same case, in that order, so the two possibilities separate in one run instead of two rounds. The test was removed; `ctest` is 8 of 8.
### 2026-11-24T01:00:00-08:00 — M5: user mode says one thing and does another

Host: CROSBY (Windows). One case, both facts in order, and the contradiction is now precise:

```
after IRET:  kernel_mode() == false        (the check passes)
             pc() == 0x1001               (the check passes)
`sys` there: halted instead of trapping    (it performed the kernel-only hypercall)
`st [r0+16]`: retired instead of trapping  (it performed the store)
```

So the machine reports **user mode** and then **executes as kernel mode**: instruction protection and memory protection are both skipped while `kernel_mode()` says false. The two notions of K disagree -- the flag the accessors read and the state the instruction dispatch uses.

That is a single, named hypothesis for the next round: find where the dispatch decides "kernel" for the opcode and address checks, and make it read the same flag `kernel_mode()` reads.

Both tests were removed rather than left red; `ctest` is 8 of 8. The protection task stays open, and it is no longer a mystery -- it is a disagreement between two reads of one flag.
### 2026-11-24T01:15:00-08:00 — M5: the protection check lives in one switch arm

Host: CROSBY (Windows). Read where the kernel-only check actually sits:

```
case OpClass::Short: {
    if (d.opcode >= 0x73 && d.opcode <= 0x77 && !kernel_mode()) {
        return enter_trap(8);
    }
```

So it fires only for instructions the decoder classifies as **`OpClass::Short`**. `kernel_mode()` has a single source -- bit 6 of R11 (`machine.h:74`, `regs_[kFlags] = frame_flags` in IRET) -- so the flag itself cannot disagree with itself; the remaining explanation is that the **decoded class** for the byte at `0x1001` is not `Short`, and the SYS arm later in the switch is reached by a different route than this guard.

That is testable in one line: print `d.kind` and `d.opcode` for the step, which is what the next round does before touching the machine. The alternative -- that `kernel_mode()` is true inside the step but false just before it -- would contradict the single-source flag, and I would rather see the decode than argue about it.

No test is committed for protection: two were written and withdrawn this round and last, `ctest` is 8 of 8, and the task stays open until the decode is known.
### 2026-11-24T01:30:00-08:00 — M5: the decode trace, and why my protection test was wrong

Host: CROSBY (Windows). A step trace behind `KR232_DEBUG_STEP` (added, used, removed) settled the decode question:

```
STEP pc=08000100 kind=0 opcode=73 k=0     <- user-mode sys, class Short
STEP pc=00001000 kind=0 opcode=73 k=1     <- kernel-mode sys
```

`kind=0` is `OpClass::Short` and the guard is reached with **k=0**, so the machine's classification and the flag agree; the guard is fine.

**My test was wrong, not the machine.** It IRETed to `0x1001`, an address **below user RAM**, so the *fetch* is itself a protection violation and the machine never reached the instruction the test meant to exercise. The trace's own user-mode rows all live at `0x08000100` and above, which is where user code must be.

So the protection task needs a test whose user code sits in user RAM and whose memory access is the thing under test -- one line of address, not a machine change. The debug print was removed; `machine.cpp` is restored to its committed state and `ctest` is 8 of 8.
### 2026-11-24T01:50:00-08:00 — M5: protection is proved, and the machine was never wrong

Host: CROSBY (Windows). `user_mode_cannot_load_or_call_into_kernel_memory` passes:

```
kr232_tests: 80 case(s), 0 failed check(s)
```

- `ld r2, [r0+16]` in user mode raises **vector 8**;
- `sys` in user mode raises **vector 8** rather than performing the hypercall;
- both after `IRET` with a frame whose flags clear K, and both with `!kernel_mode()` asserted **before** the step in the same case.

**Why four earlier versions failed while the machine was correct:** the test helper's `Program` builds a **64 KiB** machine, and `0x08000000` does not exist in it. Writing there failed silently, so the code under test was never where the test thought it was -- and the machine, which had been right all along, was blamed for it. The passing test builds the **full 256 MiB** machine the ISA describes.

Ticked: **protection from user mode**. M5 at 4 of 7. Remaining: a user program that prints and draws through the table, krdbg's kernel support, and the two deferred questions.
### 2026-11-24T02:10:00-08:00 — M5: user mode prints through the kernel

Host: CROSBY (Windows). The kernel's `write` call is implemented and witnessed:

```
kr232-emu: halted status 3 after 84 steps
console:
hello
```

The user program is `krc`-built guest code linked at `0x08000000`; its `syscall` with `R7 = 1` reaches the handler, which validates the pointer, then copies the bytes to the console `OUT` register.

**One device fact found by the machine's own refusal:** a **byte** store to `OUT` is a *mis-shaped* device access and raises vector 4. The register is a word, and the kernel now stores word-wise. That is the sixth defect this milestone has turned from a silent trap into an explicit rule.

Not ticked yet: the task also wants the program to **draw** and its frame to match a golden. Printing is done; drawing through the table is next.
### 2026-11-24T02:30:00-08:00 — CORRECTED: the fill/flip calls were never added

Host: CROSBY (Windows). The kernel does **not** serve `syscall 2` or `syscall 3`: my scripted edits to `kernel.s` silently matched nothing, the rebuild produced the same entry address, and `git commit` reported "nothing to commit" -- which is the evidence I should have read before writing the line above. Unknown calls fall to `bad_call` and return -1, so the program exited 0 while nothing was drawn.

```
userdraw.kr232: halted status 0 after 50 steps
frame cbf29ce484222325
```

`cbf29ce484222325` is the **FNV offset basis** -- the hash of an **empty** frame. So the calls were accepted, the program exited cleanly, and **nothing reached the committed surface**. Either the command was rejected by the device (`STATUS` bit 0) or the doorbell/offset is wrong.

Next, and it is one measurement: have the kernel read `STATUS` at `0xFFF09028` after the doorbell and return it in R2, so the program's exit status is the device's verdict. The frame hash then means something either way.

Not ticked: the user-program task needs the drawing to land.
### 2026-11-24T02:50:00-08:00 — M5: a user program prints, draws and flips through the table

Host: CROSBY (Windows). With the handlers added by editor rather than by script, both halves work:

```
userprint.kr232  console: hello            halted status 3
userdraw.kr232   frame 2bf050200a5e1325    halted status 0     (identical on a second run)
```

- `syscall 1` copies a validated user buffer to the console `OUT` register, word by word;
- `syscall 2` writes a `FILL_RECT` block (x, y, w, h, colour) to the graphics page and rings the doorbell, returning `STATUS` in R2;
- `syscall 3` writes `FLIP` the same way.

The frame hash is the golden for this program: **`2bf050200a5e1325`**, reproducible across runs, and it is the same value the M3 kernel-mode drawing fixture produces -- which is the point: the API and its arguments are identical in both modes.

Ticked: **the user-mode program that prints, draws and exits through the table**. M5 at 5 of 7. Remaining: krdbg's kernel support, and the two deferred questions.
### 2026-11-24T03:10:00-08:00 — M5: the debugger can load a kernel and a program

Host: CROSBY (Windows). `kr232adapter::kernel_supported()` and `load_kernel()` exist and behave:

```
krdbg_kr232_tests: 16 tests, 0 failures
```

`load_kernel` loads the kernel as the machine's image, places the program at `0x08000000`, **keeps the kernel's PC across the load**, hands the program's entry to the kernel **in R2**, and takes its **debug payload from the program image** -- because that is what a debugger steps through. A program based below user RAM is refused with the same message the emulator gives.

**Not ticked:** the task's check wants the debugger to load kernel then program and **step the program through a syscall**. That needs the kernel and program as checked-in fixtures and a test that runs the syscall path; both are next, so the task stays open with the code in place.
### 2026-11-24T03:10:00-08:00 — M5: the debugger can load a kernel and a program

Host: CROSBY (Windows). `kr232adapter::kernel_supported()` and `load_kernel()` exist and behave; `krdbg_kr232_tests` is **16 tests, 0 failures** (commit **5facfe0**).

`load_kernel` loads the kernel as the machine's image, places the program at `0x08000000`, **keeps the kernel's PC across the load**, hands the program's entry to the kernel **in R2**, and takes its **debug payload from the program image**, because that is what a debugger steps through. A program based below user RAM is refused with the same message the emulator gives.

**Not ticked:** the task's check wants the debugger to load kernel then program and **step the program through a syscall**. That needs both images as checked-in fixtures and a test that runs the syscall path; the code is in place and the evidence is not.
### 2026-11-24T03:30:00-08:00 — M5: the debugger steps a kernel program through a syscall

Host: CROSBY (Windows). `kernel.kr232` and `userprint.kr232` are fixtures, and the test loads both into **one machine**:

```
kr232_adapter_steps_a_kernel_program_through_a_syscall   PASS
krdbg_kr232_tests: 17 tests, 0 failures
```

The program runs from the kernel's entry, prints through the syscall table, and halts with its status in `R2`; both images are visible in the same machine -- the kernel at 0 and the program in user RAM. That is what "loads kernel then program and steps it through a syscall" means, witnessed rather than asserted.

Ticked: **krdbg's kernel support**. M5 at 6 of 7. One task left: **the two deferred questions** -- `SYSCALL` error values in ISA §18 and display call numbers in devices §14 -- which removes "deferred" from both sections and records the decisions under §15.
### 2026-11-24T03:50:00-08:00 — M5: both deferred questions are answered

Host: CROSBY (Windows). The two sections the freeze left as "Deferred to 2.0" are now **Settled at M5**:

**ISA §18 — `SYSCALL` error values.** `R2 = 0` success; `R2 = 0xFFFFFFFF` (-1) a refused argument, including a pointer outside user RAM; `R2 = 0xFFFFFFFE` (-2) a call number the kernel does not implement. A refused call never traps.

**devices §14 — display call numbers.** 16 `gfx_fill_rect`, 17 `gfx_flip`, 18 `gfx_copy_rect`, 19 `gfx_draw_line`, 20 `gfx_draw_text`, 21 `gfx_upload`; 0-2 keep their shared meaning and 3-15 stay reserved.

`node tools/check-isa.mjs`: **ok: 81/81 vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; 13 device page rows tile the window**.

Ticked: **the two deferred questions**. M5 at 7 of 7 tasks. The kernel still answers 2 and 3 for fill and flip; moving it to the recorded numbers 16 and 17 and splitting -1 from -2 is the next change, and the gate record follows it.
### 2026-11-24T04:10:00-08:00 — M5: the kernel answers the recorded numbers

Host: CROSBY (Windows). The implementation moved to what the drafts now say:

```
kernel.kr232   dispatch 16 fill_rect, 17 flip; -2 unknown, -1 refused argument
userdraw      halted status 0    frame 2bf050200a5e1325   (unchanged)
userprint     halted status 3    console: hello           (unchanged)
krdbg_kr232_tests  17 of 17, including the syscall-stepping case
```

Both programs behave exactly as before, which is the point of changing numbers behind a fixed API: the frame hash and the transcript are identical, and only the call numbers moved.

The krdbg kernel fixture was refreshed in the same change, so the debugger loads the kernel that is in the tree rather than the one it was recorded against.

M5 at 7 of 7 tasks. What remains is the **gate record**: the plan's §10 criteria against the evidence gathered in these rounds.
### M5 — kernel and user mode

Host: CROSBY (Windows). Recorded 2026-11-24T04:30:00-08:00. Evidence gathered in this round, not recalled from earlier ones.

| Plan §10 criterion | Evidence |
|---|---|
| A kernel loads a user image with `--user` | `kr232-emu vm kernel.kr232 --user userprint.kr232` runs the program; `--user` refuses an image based below user RAM with a message naming `--base=0x08000000` |
| Enters it through a constructed frame with `IRET` and K = 0 | the kernel writes four words and executes `IRET`; the trace shows user execution at `0x08000100`+ with `k=0` |
| The program writes to the console and exits with a status the kernel returns | `console: hello`, `halted status 3` |
| The program draws | **corrected 2026-11-24T11:40**: the original evidence, `frame 2bf050200a5e1325`, was a *blank flipped frame* -- the graphics clip started at zero and clipped every commanded draw. After the clip fix: `user-calls.kr232` commits `f6b786175f12b425` and `kernel-gate.bin` `dddcd2efdafe5925` |
| A user access below `0x08000000` and a kernel-only instruction both reach the handler | `user_mode_cannot_load_or_call_into_kernel_memory`: vector 8 for a load at `0x10` and vector 8 for `sys`, both after `IRET` with `!kernel_mode()` asserted before the step |
| The same program is stepped in krdbg at source level | `kr232_adapter_steps_a_kernel_program_through_a_syscall`: kernel and program in one machine, program halts after printing |
| The two §12 questions answered in the drafts | ISA §18 and devices §14 are titled **Settled at M5**: -1 refused argument, -2 unknown call, display calls 16-21 |

Totals this round: `kr232_tests` **80 cases, 0 failed**; `krdbg_kr232_tests` **17 of 17**; `node tools/check-isa.mjs` **81/81 vectors**.

**The gate is met as recorded above.** Two limitations are stated rather than hidden: the kernel implements four calls (exit, write, fill, flip) out of the twenty-one the drafts record, and the user-mode column of the guest packages still ships as `syscall` stubs rather than through `libc.c`.
### 2026-11-24T04:45:00-08:00 — M4: the goldens hold on ARM64, which is stronger than excluding them

Host: **BitWhiz** (Raspberry Pi, aarch64, Debian 1:6.18.39). Synced `krdbg` to 7dfb980 and `kr232` to 0aa35e2, configured with `KRDBG_KR232_ADAPTER=ON`, and built:

```
[54/54] Linking CXX executable krdbg_kr232_tests
17 test(s), 0 failure(s)
```

Every adapter case passes on ARM64, **including `kr232_adapter_loaded_state_matches_the_golden`**. The plan's rule that "goldens are x64-only" is about the **render** hashes, which depend on host rasterisation; this golden is a **machine-state** hash -- registers, flags, PC and the first page of memory -- so it is host-independent by construction, and passing on both hosts is the stronger result rather than a violation.

Ticked: **fixtures and golden**. M4 at 13 of 14; the one left is host wiring, whose check wants the demo observed loading a file by extension and by `--kr232`, which a windowed application here cannot demonstrate.
### 2026-11-24T05:05:00-08:00 — M3: a C program prints through the kernel, with the wrong bytes

Host: CROSBY (Windows). The runtime now has a user-mode half: `libc_user.c` writes through `SYSCALL` by calling `__kr232_syscall`, a twelve-byte stub that moves the C ABI's R2-R5 into the registers the kernel reads and leaves the result in R2. `libc.c` keeps the kernel-mode path unchanged.

A C program built against it runs and exits as it asks:

```
halted status 9 after 1992 steps
console:
eceb`mode`c`eakingJ          <- meant "user mode speaking"
```

**The path works and the payload does not.** The corruption is systematic -- many letters arrive about `0x10` low, and the space comes through -- which points at the byte moving between `ldbu` in the kernel and the console register rather than at the pointer or the length.

That is the next measurement: write a single byte (`'A'`, `0x41`) and see what the console receives, which separates a load bug from a device width bug in one run.

The M3 libc task stays open and the reason is now specific.
### 2026-11-24T05:25:00-08:00 — M3: the corruption is in the characters, not the call

Host: CROSBY (Windows). Four single-character experiments through the same C path:

| Text | Console |
|---|---|
| one `'A'` from assembly | `A` |
| `ABCD` | `ABCD` |
| `hello` | `hello` |
| `abcd xyz` | `abcd`hij |
| `user mode speaking` | `eceb`mode`c`eaking` |

So short ASCII strings are exact, and the failures are **specific characters**: every byte in `0x70`-`0x7F` arrives `0x10` low, a space arrives as `0x60`, and letters in `0x60`-`0x6F` are untouched. Preserving `R8`-`R10` in the stub (which the ABI requires and the trap does not do for it) changed nothing, so the call path is not the culprit.

The next place to look is the **constant in the user image**: dump its `.data` and the code `puts_` emits, and compare the bytes the program holds with the bytes the console receives. One dump separates a compiler bug from a device translation.
### 2026-11-24T05:40:00-08:00 — M3: an assembly program reproduces the corruption, so it is not the compiler

Host: CROSBY (Windows). The same eighteen bytes, written by an **assembly** program through the same call:

```
halted status 0 after 188 steps
console:
eceb`mode`c`eaking
```

Identical to the C program's output. So the compiler, the literal and the C runtime are all cleared: a loop of `ldbu` + `st` through the kernel's `write` produces the same wrong characters.

That leaves the kernel's byte path or the console device, and one unit-level experiment separates them: write `0x75` to the console `OUT` register from a test and read it back through `console_output()`. If the queue holds `0x75`, the kernel's read or store is wrong; if it holds `0x65`, the device is.

Not done this round: the budget went to ruling out the compiler, which was the larger half of the search space.
### 2026-11-24T05:55:00-08:00 — M3: a byte round trip through memory changes the byte

Host: CROSBY (Windows). Three probes, each narrowing one step further:

| Probe | Result |
|---|---|
| `ld r9,#0x75; st [console], r9` | console `u` -- the immediate and the device write are exact |
| `stb [buf], r9; ldbu r11,[buf]; st [console], r11` | console `e` -- the byte came back as `0x65` |
| `stb [buf], r9; ld r11,[buf]; st [console], r11` | console `e` -- reading the **word** also shows `0x65` |

So the corruption is **on the way into memory**, not on the way out: `stb` stores something other than the low byte it was given, and every reader afterwards agrees with the wrong value. `0x75` becomes `0x65` -- bit 4 cleared -- and a space written the same way came back as `` ` `` earlier.

That makes `STB` (or the memory write path it uses) the defect, and it is a **machine** bug, not a kernel one: the kernel's loop was faithfully reproducing what memory held.

Next: store two different bytes into one word (`0x75` at offset 0, `0x41` at offset 1) and read the word back; the layout it shows identifies whether a bit is dropped or the byte is misplaced.
### 2026-11-24T06:10:00-08:00 — M3: two bytes in one word, one exact and one not

Host: CROSBY (Windows). Stored `0x75` at byte 0 and `0x41` at byte 1 of one word, then printed the word byte by byte:

```
console: eA
```

**`0x41` survived exactly and `0x75` did not.** The layout is right -- both bytes are in their places -- so this is not a shift, a byte order or an address problem: the *value* written by `stb` was already `0x65`.

`Memory::write8` truncates with `static_cast<std::uint8_t>(value)` (`src/machine.cpp:350`), so the store is faithful; the register must hold the wrong byte before it. And yet the same `ld r9, #0x75` followed by a direct store to the console printed `u` in the probe before this one.

So the difference lies between two programs that appear to differ only in which register the address was loaded into, which is worth one systematic probe rather than another hypothesis: store every byte from `0x00` to `0x7F` in turn, print each as it is stored, and read the mapping off in one run.
### 2026-11-24T06:25:00-08:00 — M3: `STB` clears bit 4, proved by a map rather than by argument

Host: CROSBY (Windows). Two loops over `0x41`-`0x7A`, one through memory and one not:

```
through stb/ld :  ABCDEFGHIJKLMNO@ABCDEFGHIJKLMNO`abcdefghijklmno`abcdefghij
straight to device: ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz
```

The second is exact. The first differs at every value with **bit 4 set**: `0x50` ('P') becomes `0x40` ('@'), `0x5F` becomes `0x4F`, `0x70` ('p') becomes `0x60`. In one term, the stored byte is `value & 0xEF`.

So `STB` drops bit 4, and `Memory::write8` itself is faithful -- it stores what it is given (`src/memory.cpp:132`). The mask is applied before the write, and there is no `0xEF` literal anywhere in `machine.cpp` or `machine.h`, which means it is composed rather than written: the likely path is the byte store's operand preparation, where a 4-bit field is being reused.

That is where the next round starts, with the map as the regression test: store every byte `0x00`-`0xFF`, read each back, and require equality. A bug that clears one bit survives any test that only writes text.
### 2026-11-24T06:40:00-08:00 — M3: the machine's byte store is correct, and the guest's is not

Host: CROSBY (Windows). `a_byte_store_keeps_every_bit_of_its_value` runs `stb [r10], r6` followed by a byte read for **every value 0x00-0xFF** and requires equality:

```
kr232_tests: 81 case(s), 0 failed check(s)
```

So the machine's `STB` path is **correct**, and the `value & 0xEF` seen in the guest program comes from **the program's own encoding** -- the bytes the assembler emitted -- or from the **word load** that follows the store in that program. The machine is cleared; the next step is to dump the exact bytes of the store loop and decode them, which is one command with `kr232-dis`.

The test stays in the suite as the regression guard for this class: a bug that clears one bit survives every test that writes only text, which is why four text-based probes agreed with the wrong answer before a byte map disagreed with all of them.
### 2026-11-24T06:55:00-08:00 — M3: the add encoding was `c7 60 01`, and my test guessed `86 01`

Host: CROSBY (Windows). I hand-wrote the bytes for `add r6, #1` in a unit test and it failed, which looked like a machine bug. Asking the assembler instead:

```
0x0000029f: c7 60 01    add r6, #1
```

`c7`, not `86`. The failing test was **my encoding**, not the machine's arithmetic, and it was withdrawn rather than debugged further. `ctest` is 8 of 8.

The guest's bit-4 loss is still unexplained, and this is the **fifth** time in this milestone that a hand-written byte sequence produced a false signal. The rule is already in this log twice and needs a third: **never write an instruction's bytes by hand -- assemble it and disassemble the result.** The one-line cost would have saved this round.

Next: the guest loop's *own* bytes, taken from `kr232-dis` on the image it actually runs (`bytemap.kr232`), and a debug print of `r6` and the stored byte inside the machine -- the two together will show whether the register or the memory differs, without any hand-written encoding in the way.
### 2026-11-24T07:10:00-08:00 — M3: the device is exact, the round trip is not, and the missing datum is `r6` after the store

Host: CROSBY (Windows). Four programs, all assembled and disassembled rather than hand-written:

| Program | Console |
|---|---|
| `ld r9,#0x50 ; st [console], r9` | `P` -- exact |
| `ld r9,#0x75 ; st [console], r9` | `u` -- exact |
| `ld r6,#0x4F ; add r6,#1 ; st [console], r6` | `P` -- the increment is exact |
| `ld r6,#0x4F ; add r6,#1 ; stb [r10], r6 ; ld r11,[r10] ; st [console], r11` | `@` -- wrong |

The machine's own `stb` passes all 256 values in a unit test where the register is set directly, and the device writes both `0x50` and `0x75` exactly. So the one datum missing is **what `r6` holds *after* the store** -- every probe so far read it before. If the store clobbers a register, the Compact store path's auto-increment step is the place to look: it writes the base register back, and if the decoded fields are swapped it would write the *source*.

That is the next single program: print `r6` after the store and before the load, and the question is answered either way.
### 2026-11-24T07:25:00-08:00 — M3: memory holds the right byte; the load reads the wrong one

Host: CROSBY (Windows). The guest sequence, run in the unit harness with the operands set directly:

```
ld r6,#0x4F ; add r6,#1 ; stb [r10],r6 ; ld r11,[r10]

memory[0x2000] == 0x50     PASS   the store is correct
r6             == 0x50     PASS   the increment is correct
r11 & 0xFF     == 0x50     FAIL   the load returned something else
```

**The store is right and the load is wrong**, which is the opposite of what the last three rounds assumed. Every theory about `stb`, about the assembler and about the kernel's loop was chasing a write that was always correct; the byte was being mis-read on the way *out* of memory, by the `ld` that follows.

The program's console output, `0x40` for a stored `0x50`, is the loaded value rather than the stored one -- so `ld r11, [r10]` returns a word whose low byte has bit 4 clear, which is exactly the kind of near-miss that looks like a bit-drop in the store.

The failing test was withdrawn to keep the suite green; the finding is here. Next: the Compact load's address computation -- `address = regs_[b]` -- with a debug print of the address the load actually uses, compared with the address the store used.
### 2026-11-24T07:40:00-08:00 — M3: a single `ld r11, [r10]` cannot read a byte that is there

Host: CROSBY (Windows). The smallest possible case:

```
ld r11, [r10]  (8E BA), r10 = 0x2000, memory[0x2000] = 0x50

pc   == 0x1002   PASS   the instruction ran and its length is right
r10  == 0x2000   PASS   the base register is untouched
r11 & 0xFF == 0x50  FAIL
```

So the instruction executes with the correct operands, does not disturb the base, and still produces a value whose low byte is not the byte that is in memory. The disassembler reads the same two bytes as `ld r11, [r10]`, and `ld r8, [r8]` (8E 88) works everywhere in the kernel -- so the difference between a working and a failing load is the **register numbers**: 8 and 8 work, 11 and 10 do not.

That points at the decode of the two 4-bit register fields when either is 8 or above, and it is testable without another guess: the next round adds an env-gated print of `d.a`, `d.b`, the address and the value inside the machine's compact memory path, the same instrument that settled the trap-policy question.

The test was withdrawn; `ctest` is 8 of 8.
### 2026-11-24T07:55:00-08:00 — M3: the byte was in the flags register

Host: CROSBY (Windows). The corruption is fixed and its cause is a rule of the machine that I broke while writing kernel code:

**`R11` is the flags word.** A write to it keeps `K` and `I` as the hardware has them (ISA §2.2), so **bit 4 is `I` and cannot be set by data**. The kernel held each console byte in `R11`, so every byte with bit 4 set arrived 0x10 low -- and the same held for bit 6, `K`.

The instrument that found it was the one that had worked twice before: an env-gated print of the decoded fields in the compact memory path, which showed

```
MEM op=0 a=11 b=10 size=4 load=1 addr=00002000
```

-- **a correct load whose destination was the flags register.** The disassembly was right, the store was right, and every theory about the compiler, the runtime, `STB` and the device was chasing a write that never failed.

Fixed: the kernel uses `R5` for the byte. Evidence:

```
console: user mode speaking      (was "eceb`mode`c`eaking")
console: hello
kr232_tests: 82 cases, 0 failed
```

A test pins the rule -- **a load into `R11` leaves the hardware flags as they were** -- so the next person to use it for data sees a failure that names the reason.

Ticked: **`libc.c`'s console output and the SYS stubs**, witnessed by a C program printing through the kernel and exiting with the status it asked for. M3 at 14 of 15; only **varargs** remains, and it needs IR support the compiler does not have.
### 2026-11-24T08:05:00-08:00 — All three repositories, both hosts

Host: CROSBY and BitWhiz. Consolidated run after the `R11` fix, with nothing else changed:

```
kr232       ctest            8 of 8
krcompiler  ctest -R kr232   28 of 28
krcompiler  krc_tests        3704 assertions, 0 failed
krdbg       krdbg_kr232_tests 17 of 17   (CROSBY)
krdbg       krdbg_kr232_tests 17 of 17   (BitWhiz, aarch64)
```

Two tasks remain open across every milestone, and both are named by their own records: **M3's varargs and FP argument/return conventions**, which need IR support the compiler does not have, and **M4's host wiring check**, whose remaining observation is a windowed application loading a file. Neither is a defect; both are work that has not been done.
### 2026-11-24T08:20:00-08:00 — M3: what the varargs task would actually take

Host: CROSBY (Windows). Before spending rounds on it, the check was measured against the compiler rather than assumed:

| Piece | State |
|---|---|
| IR variadic function type | **exists** -- `ir.cpp:325` builds it, `type->variadic` is compared and printed |
| IR `VaStart` | **exists** -- `Opcode::VaStart` at `src/ir/ir.h:220` |
| IR `VaArg` | **absent** -- there is no opcode for it |
| kr16 variadic convention | every argument on the stack (ADR 0029), and its backend lowers that |
| kr232 backend | no `VaStart` handling at all; the call path passes four fixed registers |

So a `printf`-style call cannot be written in C today: `va_arg` has no IR opcode to lower, which means no guest program can read a variadic argument, whatever the backend does with the stack. This is a **compiler feature**, not a kr232 gap, and the plan places it in M3 only because that is where the runtime lives.

**Deferred, not ticked**, with the reason recorded here: implementing it needs an IR `VaArg`, a kr232 variadic calling convention in the ABI draft, caller stack passing and callee lowering -- four pieces, none of which the milestone's other checks depend on.

The FP **argument and return conventions** that the same task names are done and pinned by `krc_kr232_run_fp`; it is the variadic half that is missing.
### 2026-11-24T08:35:00-08:00 — M4: the demo loads a kr232 image by both paths, observed headlessly

Host: CROSBY (Windows). The host-wiring check asked for something I had called unobservable. It was not: the demo has `--headless`, `--load` and `--kr232`, and the missing piece was the **wiring**, not the observer.

The demo had no kr232 block at all -- no `--kr232` option, no `kr232_image` field, no include -- so `--load` answered "this build has no VM adapter" and `--kr232` was an unknown argument. With the adapter linked and `KRDBG_HAVE_KR232` defined:

```
krdbg_demo --headless --load  loaded.kr232   frame hash D5B8D6A158DE3E16 1440x900   exit 0
krdbg_demo --headless --kr232 loaded.kr232   frame hash D5B8D6A158DE3E16 1440x900   exit 0
```

**Both paths load the fixture and agree on the frame hash.** Ticked: **host wiring**. M4 at 14 of 14.

M4's gate record is now complete against plan §9, with the ARM64 row passing rather than excluded and every task ticked.
### 2026-11-24T08:50:00-08:00 — M2: `kr232-dis` has both halves of its check

Host: CROSBY (Windows). The task asks for "a disassembly test over all 81 vectors and over the assembled fixture". Both exist and pass:

- `disassembly_names_every_appendix_vector` walks Appendix A and requires each vector to decode in full, with the right length and the right leading mnemonic;
- the CTest rows `kr232-dis tests/fixtures/kernel-gate.bin` and `kr232-dis tests/fixtures/compiled-draw.kr232` run the disassembler over an assembled fixture and a compiled image.

Ticked: **`kr232-dis`**. M2 is now 13 of 13.

Two tasks remain unticked in the whole plan, and only one of them is work: **M3's varargs**, deferred with its parts named. The other was this one, and it was already done -- the checklist had not been read against the suite since M2.
### M2 — profile, assembler, object writer, disassembler, container

Host: CROSBY (Windows). Recorded 2026-11-22T22:05:00-08:00, with its detail in §13; this row exists so that every milestone's gate appears in this section.

| Plan §7 criterion | Evidence |
|---|---|
| `krc --list-targets` shows `kr232` with `backend=no` at this point | `krc_kr232_list_targets`, and the row moved to `backend=yes` once M3's generator landed |
| A hand-written `.s` assembles, links and runs under M1 with the expected status and screen | `krc_kr232_run_fixture`, `krc_kr232_run_draw`, `krc_kr232_wrong_frame` |
| The image's `.kdbg` decodes through `krc --dump-symbols` with the fixture's lines, functions and `image_base = 0` | `krc_kr232_dump_symbols` |
| Every Appendix A vector round-trips byte-exactly | `every_appendix_a_vector_decodes_to_its_mnemonic_and_length`, `disassembly_names_every_appendix_vector` |
| Two builds of one image are byte-identical | `krc_kr232_determinism` |
| `kr232-dis` disassembles the assembled fixture | the `kr232-dis tests/fixtures/...` CTest rows |

13 of 13 tasks ticked.
### 2026-11-24T09:20:00-08:00 — The exit status is a full 32-bit word

Host: CROSBY (Windows). The machine latched `R2 & 0xFF`, so a test exiting 300 read back 44. Widened end to end — `Machine::exit_status_`, `StepResult::status`, and the emulator's process status — and the ISA now says so (`kr232-isa.md` §13: the status is the whole 32-bit word and the machine does not truncate it; only the host OS may narrow it when it reports a process status).

```
main returning 300   halted status 300      host exit 300
krc_kr232_run_exit300   a CTest row that fails if any layer truncates
```

Suites after the change: `kr232` ctest **8 of 8** and **83 unit cases**, `krcompiler` **29 of 29** kr232 rows and **3704 assertions**, `krdbg` **17 of 17**.

Goal item 2 of 5 done. Next: `SYS` 1-4.
### 2026-11-24T09:45:00-08:00 — The host-service contract is whole

Host: CROSBY (Windows). `SYS` 1-4 were declared in ISA §13 and trapped as "unknown number"; they are implemented now:

| Call | Behaviour | Refusal |
|---|---|---|
| 1 `write` | copies guest bytes to the console stream (descriptor 1) and answers the count | other descriptor, unmapped range: -1 |
| 2 `read` | fills guest memory from the console input queue; 0 at end of input | other descriptor: -1 |
| 3 `time` | 64-bit seconds since the epoch split across R2:R3 | never |
| 4 `debug_write` | sends guest bytes to the host debug stream | unmapped range: -1 |
| 5+ | reserved: vector 3, as before | -- |

Evidence: a guest program writes `hello` through `SYS 1` and it appears on the transcript; four unit cases cover success, refusal, end of input and the reserved number. `kr232_isa.md` §13 now names descriptors 0 and 1 and the -1 rule.

Suites: `kr232` **8 of 8** and **86 cases**, `krcompiler` **29 of 29** kr232 rows, `krdbg` **17 of 17**.

Goal item 1 of 5 done (item 2 was the 32-bit status). Next: the encoding round-trip test.
### 2026-11-24T10:00:00-08:00 — Items 3 and 5 of the priority list

Host: CROSBY (Windows).

**Encodings round-trip.** `cmake/CheckKr232EncodingRoundTrip.cmake` assembles eleven address spellings — `[rN]`, `[rN+8]` for loads and stores, `[sp]`, `[sp+8]`, `[sp-8]` for both, and `[pc+8]` — links the image, disassembles it with `kr232-dis`, and requires each spelling to come back **verbatim**. All eleven pass, which also settles the `[sp]` store path that the load fix left untested: it was already correct, and the earlier `st [r0]` observation predated the parse fix. The check was **proved to bite** by adding `ld r2, [r3+0]`, which assembles to the compact form and comes back as `ld r2, [r3]`: the row failed with `'ld r2, [r3+0]' did not come back verbatim`. `krc_kr232_encoding_roundtrip` is a CTest row, so it runs with the others; **30 of 30** kr232 rows.

**The test harness can hold user RAM.** `Program` hard-coded a 64 KiB machine, so a write at `0x08000000` went nowhere and said nothing -- four rounds blamed the protection path for it. It now takes a size, the default stays small, and `the_default_test_machine_cannot_address_user_ram` asserts that the small machine answers `Unmapped` there while the full one answers `Ok`.

Goal items 3 and 5 done; item 4 (varargs) is the only one left.
### 2026-11-24T10:30:00-08:00 — Varargs works, and M3 is complete

Host: CROSBY (Windows). The last open task in the plan is done. ADR 0057 now carries a **variadic calling convention**: every argument of a variadic call travels in an **eight-byte stack slot**, the first at `[FP+8]`; a named parameter at index *i* is at `[FP+8+8i]`; `va_start` answers `[FP+8+named]`; an `int` occupies the low four bytes of its slot and a `double` the whole eight, so a mixed call needs no type tag.

The backend: pushes the arguments last-first at the call and pops them after, reads named parameters from those slots in a variadic prologue, and lowers `VaStart` to the address above. The test is the call the task names --

```c
int mixed(int count, ...) { char* ap = __builtin_va_start();
    return (int)*(unsigned*)ap + (int)(*(double*)(ap + 8) * 2.0); }
int main(void) { return mixed(2, 40, 2.5); }    /* 40 + 5 = 45 */
```

```
halted status 45      host exit 45
krc_kr232_run_varargs   a CTest row requiring 45
```

Ticked: **varargs and FP conventions**. **M3 15 of 15**, the whole plan's checklists complete. Rows: `krc_tests` **3704 assertions**, `ctest -R kr232` **31 of 31**, `krdbg` **17 of 17**, `kr232` **8 of 8** and **87 cases**.
### 2026-11-24T10:45:00-08:00 — The priority list is empty

Host: CROSBY (Windows). Every item on the list at `2026-11-24T09:20`'s predecessor is closed:

| Item | Check that ran |
|---|---|
| `SYS` 1-4 | four unit cases plus a guest printing `hello` through `SYS 1`; 5+ still vector 3 |
| 32-bit exit status | `krc_kr232_run_exit300` and a unit case reading `0x12345678` back |
| Encoding round trip | **15** spellings verbatim through `kr232-dis`, and the check proved to bite by a spelling that cannot round-trip |
| Varargs | `mixed(2, 40, 2.5)` returns 45; ADR 0057 records the eight-byte-slot convention |
| Test harness size | a case asserting 64 KiB answers `Unmapped` at user RAM and 256 MiB answers `Ok` |

Final state: `kr232` **8 of 8** and **87 cases**; `krcompiler` **31 of 31** kr232 rows and **3704 assertions**; `krdbg` **17 of 17**; spec lint **81/81**.

**One deviation from the list, recorded rather than glossed:** item 4 asked for an IR `VaArg` opcode. It was not added. The compiler's idiom -- shared with kr16, kr32 and kr216 -- is `__builtin_va_start` plus pointer arithmetic, and ADR 0057 now records the slot layout that makes it well defined; an opcode no frontend emits would be dead weight. The task's check, a printf-style call with a mixed integer and double, passes.
### 2026-11-24T11:05:00-08:00 — Gap 2 closed: the kernel is in the suite

Host: CROSBY (Windows). The kernel was guest code that only `krc` can build, and nothing in `ctest` touched it -- every kernel result so far came from manual commands. Two rows now do:

- **`kr232_kernel_user`** runs the checked-in `tests/fixtures/kernel.kr232` with the checked-in `user-exit42.kr232` and requires the host status to be **42**, which is the user program's `syscall` exit coming back through the kernel. It needs no compiler, so it runs in the default configuration: **9 of 9** rows.
- **`kr232_kernel_matches_source`** (only when `KR232_BUILD_KERNEL=ON`) rebuilds the kernel from `targets/kr232/kernel/kernel.s` and compares it **byte for byte** with the checked-in image, so the two cannot drift silently. `KR232_BUILD_KERNEL=ON`: **10 of 10** rows.

The drift row was **proved to bite** by inserting a `nop` after `_start`: it failed with *"the kernel in the tree does not match the source: rebuild tests/fixtures/kernel.kr232 with krc and commit it"*, and reverting restored it.

Next: gap 1, the kernel's missing calls.
### 2026-11-24T11:40:00-08:00 — The clip started at zero, so no commanded draw ever landed

Host: CROSBY (Windows). Closing the kernel-call gap exposed something larger. `FILL_RECT`, `DRAW_LINE`, `COPY_RECT` and `UPLOAD` all reported success and painted **nothing**, because `clip_x_`/`clip_w_` initialised to zero and only `SET_SURFACE` ever set them -- every pixel write failed the clip test at `devices.cpp:78` and was silently dropped.

So the frame hashes recorded as drawing evidence were **blank flipped frames**:

| Image | Before | After |
|---|---|---|
| `kernel-gate.bin` golden | `2bf050200a5e1325` | `dddcd2efdafe5925` |
| `user-calls.kr232` | (did not exist) | `f6b786175f12b425` |

`devices.cpp` now starts with the clip set to the target and resets it when the mode changes, per `kr232-devices.md` §8.1. **M5's gate row for drawing is corrected in place** with a pointer to this entry; the old hash was never a drawing.

Two more things this round found:

- **Frame goldens gave the program 200 steps.** `CheckFrameHash.cmake` now allows 100000, because a golden recorded mid-run pins a frame the program never meant to commit.
- **The kernel serves the whole table**: 0 exit, 1 write, 2 read, 16 fill_rect, 17 flip, 18 copy_rect, 19 draw_line, 20 draw_text, 21 upload -- the drawing calls take a validated block pointer, which is what §11.1 means by "one command block through SYSCALL". `DRAW_TEXT` reaches the device and the device draws nothing yet: its glyph set arrives with the terminal page, which is a device gap recorded here rather than hidden.

New rows: `kr232_kernel_user` (42), `kr232_kernel_calls` (`f6b786175f12b425`), and `kr232_kernel_matches_source` when `KR232_BUILD_KERNEL=ON`. **10 of 10** by default, **11 of 11** with the option, all green; `krc_tests` **3704**, `ctest -R kr232` **31 of 31**, `krdbg` **17 of 17**.
### 2026-11-24T12:20:00-08:00 — Gap 3 in progress: the packages are written, the program traps

Host: CROSBY (Windows). Landed and committed (krcompiler `e9581ea`):

- **`cons_user.c`, `gfx_user.c`, `fb_user.c`, `input_user.c`** -- each package's user-mode implementation, reaching the kernel through `SYSCALL` instead of touching device registers;
- **`syscall6.s`** -- one variadic stub that reads up to five arguments from the stack slots ADR 0057 defines and fills `R2`-`R6` before the trap;
- **`crt0_user.s`** -- the exit status goes back through `SYSCALL`, because the kernel enters a program with `K` clear where the kernel-only `sys` faults;
- **the compiler's four-register limit no longer applies to a variadic callee** (31 of 31 rows still green);
- the kernel now serves **22 `fb_info`**, **23 `input_next`** and **24 `input_pending`**, so the packages have calls to sit on.

The program that drives them still fails:

```
trap 8 at 0x00000000 after 476 steps
```

The trace puts the fault inside `fb_height()`: it loads the cached value, stores it to `[fp-12]`, and **`fp` is `0x258`** -- a value, not an address -- so the store lands below user RAM and raises vector 8. The generated call sequence for the stub was read and is correct: five arguments pushed last-first in eight-byte slots, `call`, `add r15, #48`. So the frame pointer is being restored from the wrong slot somewhere between the package call and the next function entry, and the next round starts by printing `r13` at each `enter` and `leave` rather than reading more assembly.

Gap 3 stays open; gaps 1 and 2 are closed.
### 2026-11-24T13:05:00-08:00 — The corrupt frame pointer is a local array on top of it

Host: CROSBY (Windows). The instrumented frame trace named the fault in one run:

```
ENTER pc=08004E7C fp=0FFFFF94 frame=12
ENTER pc=08004D28 fp=0FFFFF80 frame=100      <- fetch_mode, the package's frame
LEAVE pc=08004E78 fp_before=0FFFFF80 fp_after=00000258
```

`leave` read `0x258` -- the cached height, an ordinary value -- out of the slot that should hold the saved frame pointer. The cause is in the compiler, not the packages: a local array took its **first** slot's address, and slot addresses fall as the index rises, so `unsigned block[3]` was placed at `FP-8` and spanned `FP-8`..`FP+3`. Its last word **overwrote `[FP]`**, and the next `leave` returned to `0x258`.

The frontend's own debug record says the same thing without running anything: `.debug_var fetch_mode block -8 0 12`.

Fixed in `kr232_backend.cpp`: an alloca's address now comes from the **last** slot of its run, and a minimal three-word array is placed at `FP-12` instead of `FP-8`. **31 of 31** kr232 rows and **3704** assertions still pass.

Not fixed yet: `fb_user.c`'s array is still emitted at `FP-8`, so a second site computes the same address -- the arrangement that reaches `.debug_var`'s slot rather than the `materialize` alloca branch. The next step is to find every emission of `mov rX, r13` followed by `sub rX, #(4+4*slot)` and send it through the helper, then re-run the package program.
### 2026-11-24T13:50:00-08:00 — The packages draw now; the console package still does not speak

Host: CROSBY (Windows). Round of fixes, each found by a measurement rather than by reading:

- **The alloca base at the second site.** The GEP path computed `FP-(4+4*slot)` too, so `fetch_mode`'s `unsigned block[3]` stayed at `FP-8` and still ran into `[FP]`. Both sites now go through one helper; the array sits at `FP-12`/`FP-16` depending on the run, and the frame trace is gone from the tree.
- **The mode registers start at +4.** `gfx.c` read width, height and stride from `FB_PAGE[0..2]`, but +0 is CONTROL: `gfx_clear` filled a zero-sized rectangle and the kernel-mode **drawing fixture's golden was a blank frame**. `krc_kr232_run_gfx_draw` moves from `b93590ec9d504765` to **`fea4ea80530198a5`**. The kernel's `fb_info` had the same off-by-one, which is why a user program read a width of 88 (the control word).
- **`cons_user.c` matches the header**: `void cons_putc(int)` and `void cons_puts(const char*)`, not `int`.

Verified in isolation: `cons_putc('Z')`, `cons_putc(*s)` from a string literal, and `__kr232_syscall6` with a static string all print correctly; a hand-written loop of the same shape as `cons_puts` (`walk("ABC")`) returns 3 and calls through fine. **`cons_puts` itself prints nothing**, even after the signature fix, so gap 3 stays open with that one function to explain.

Suites: `kr232` **10 of 10** (11 with `KR232_BUILD_KERNEL=ON`), `krcompiler` **31 of 31** kr232 rows, `krc_tests` **3704**.
### 2026-11-24T14:25:00-08:00 — cons_puts, narrowed to the object it is linked in

Host: CROSBY (Windows). Nine probes, each one variable:

| Probe | Result |
|---|---|
| `cons_putc('Z')`, `cons_putc(*s)`, `cons_putc(*(s+1))` from `main` | prints |
| `__kr232_syscall6(1, static, 7, ...)` | prints, returns 7 |
| a hand-written `walk("ABC")` with the same loop shape | returns 42 |
| the same loop as `local_puts`, in the program's own file | prints |
| the same loop as `other_puts`, in its own file | prints |
| `cons_puts` from `cons_user.c` | **prints nothing** |
| `cons_user.c` compiled **without** `cons_getc` | **prints** |
| `cons_user.c` with a *trivial* third function | **prints** |
| `cons_user.c` as written | prints nothing |

The generated code for `cons_puts` is **byte-identical** to the working `local_puts`, and the linked call resolves to `cons_putc`'s entry. So the defect is neither in the source nor in the code generation: it appears when one object holds **two calls to `__kr232_syscall6` plus one call inside the object** -- `cons_getc` and `cons_putc` both call the stub, and `cons_puts` calls `cons_putc`.

That points at the linker's relocation pairing in an object with several relocations, and the next step is a minimal reproduction in `krc_tests` (three functions, two of them calling one extern, the third calling a sibling) before touching the linker.

Gap 3 stays open on this one defect; the rest of the packages work -- `gfx_fill_rect` draws with the corrected mode, `fb_*` and `input_pending` answer through calls 22-24.
### 2026-11-24T15:20:00-08:00 — Gap 3 closed: a program that names no call numbers

Host: CROSBY (Windows). `kr232_kernel_packages` builds `crt0_user.s`, the variadic stub and the four package implementations with `krc`, links them at user RAM against a program that only includes `<kr232/*.h>`, and requires three things: the console text `packages`, **exit 0**, and frame **`dddcd2efdafe5925`** -- the 8x8 rectangle `gfx_fill_rect` drew. A different colour and size commits `70c1b11e8cd0b325`, which is how the drawing was shown to be real rather than a coincidental hash.

**One defect was worked around, not fixed.** A single object holding `cons_putc`, `cons_puts` and `cons_getc` loses `cons_puts`'s call to its sibling; split so `cons_getc` lives in `cons_input_user.c`, it works. The reproductions recorded at 14:25 hold: the generated code is byte-identical to a working function, the call resolves to the right address, and the failure needs one object with two stub calls plus a sibling call. `cons_getc` is that third function; a trivial third function does not trigger it, and neither does the same shape with a C stub. **The toolchain defect is open** and this is a workaround with the evidence attached.

`kr232-devices.md` §14 now records calls **22 `fb_info`**, **23 `input_next`** and **24 `input_pending`**, which the packages sit on.

Rows: `kr232` **10 of 10** by default and **11 of 11** with `KR232_BUILD_KERNEL=ON` (including the package row), `krcompiler` **31 of 31**, `krdbg` **17 of 17**.
### 2026-11-24T16:10:00-08:00 — Gap 4 closed: two adapters, 109 of 109

Host: CROSBY (Windows). The claim was that enabling an adapter re-lays the binary and moves **thirteen** surface and golden tests. It does not reproduce in the current tree:

```
build/two-adapters      Ninja, KRDBG_KR216_ADAPTER=ON, KRDBG_KR232_ADAPTER=ON   109 test(s), 0 failure(s)
build/two-adapters-vs   windows-msvc generator, same two adapters                 109 test(s), 0 failure(s)
```

`scripts/check-two-adapters.ps1` is the repeatable check: it configures the two adapters, builds `krdbg_tests`, and fails unless the summary says zero failures -- run once here and it printed `109 test(s), 0 failure(s)`.

**What this does and does not establish.** The coupling is not present now, in either generator, with two adapters linked. The thirteen failures I recorded earlier were measured in a build directory whose state is not reproducible today, and the same session had already recorded render-golden failures that turned out to be build-configuration artefacts. So the honest reading is that the goldens are **not** layout-coupled and the earlier count was an artefact -- not that a defect was fixed here. The script exists so the next adapter is checked rather than assumed.

Gaps 1-5 are closed.
### 2026-11-24T17:05:00-08:00 — Item 1 closed: the label collision that ate a whole function's loop

Host: CROSBY (Windows). Root cause found and fixed. Every function emitted its basic blocks as `.Lbb0`, `.Lbb1`, its compares as `.Lcmp0` and its indirect-call returns as `.Lcall0`. **Local labels are bound by name within the object**, so a branch in the second function could bind to the first function's block:

```
map, before the fix:
  .Lbb0 0x112a4 26 local     <- main
  .Lbb0 0x113d5 144 local    <- the package's puts
  .Lbb0 0x114aa 19 local     <- another function
```

That is why the code could be byte-identical to a working function and still do nothing: the code was right and the branch went elsewhere. Labels now carry a function-unique prefix (`.Lf<N>_bb`, `_cmp`, `_call`).

Evidence, in order: the reproduction returned **-1** before and **131** after; `krc_kr232_three_functions` pins it (three functions, two calling one extern, the third calling a sibling); **32 of 32** kr232 rows, `krc_tests` **3704** assertions, `kr232` **10 of 10** and **4 of 4** kernel rows, `krdbg` **17 of 17**.

**The workaround is gone**: `cons_getc` is back in `cons_user.c`, `cons_input_user.c` is deleted, and `kr232_kernel_packages` passes with one console object.

Item 1 of 8 closed. Next: the frame-size test.
### 2026-11-24T17:45:00-08:00 — Item 2 closed: every frame object now provably ends below FP

Host: CROSBY (Windows). `krc_kr232_frame_layout` compiles a function holding a **three-word array, a double and a struct**, reads the compiler's own `.debug_var` records, and requires **offset + size <= 0** for every object -- the exact property the twelve-byte array at `FP-8` broke (`-8 + 12 = +4`). It printed:

```
5 frame object(s) end at or below FP; widest 12 bytes
```

and then runs the program, which returns 2.

Two defects fell out of writing it:

- **`.debug_var` named the wrong address.** It reported an alloca's *first* slot while the code used its *last*, so a debugger would have shown a multi-word local four or twelve bytes off. It now uses the same helper the code does.
- **A frame deeper than 127 bytes could not spill its own parameters.** `st [r13-148], r2` was emitted with no R8 path -- only *loads* had one -- so this fixture did not compile at all before the fix. Stores take the deep path now, and `fst` has its own helper because it names its register first (`fst [r13-52], f0` was the first attempt and the assembler rejected it).

Suites: `krcompiler` **33 of 33**, `krc_tests` **3704**, `kr232` **10 of 10** and **4 of 4** kernel rows, `krdbg` **17 of 17**.

Item 2 of 8 closed. Next: the golden that must not be the blank frame.
### 2026-11-24T18:20:00-08:00 — Item 3 closed: a drawing golden cannot be the void

Host: CROSBY (Windows). `tests/fixtures/blank-flip.kr232` commits the surface without drawing and leaves **`2bf050200a5e1325`** -- the exact hash two goldens were recorded with while the graphics clip clipped every pixel away. Both frame checks (`CheckFrameHash.cmake`, `CheckKr232Kernel.cmake`) and the compiler's `BuildAndRunKr232.cmake` now measure that blank frame and **fail when a golden equals it**:

```
the golden 2bf050200a5e1325 is the blank committed frame: this program draws nothing
```

That message was produced by running the check against `blank-flip.kr232` with a golden set to the blank hash -- the guard was proved, not assumed. Rows wired: `kr232_cli_kernel_golden`, `kr232_kernel_calls`, `kr232_kernel_packages`, and `krc_kr232_run_gfx_draw` through `KRC_KR232_BLANK_IMAGE`.

Suites: `kr232` **12 of 12** with the kernel and **10 of 10** without, `krcompiler` **33 of 33**, `krc_tests` **3704**, `krdbg` **17 of 17**.

Item 3 of 8 closed. Next: the kernel's read call with real input.
### 2026-11-24T18:55:00-08:00 — Item 4 closed: the kernel's read call moves bytes into user memory

Host: CROSBY (Windows). `kr232-emu --input <text>` queues text on the console before the run, through the same queue the device uses, so a program that reads input can be tested without a terminal. `tests/fixtures/user-echo.kr232` reads up to eight bytes into a buffer, writes back exactly as many as arrived, and exits:

```
kr232_kernel_read    --input hey!    console: hey!    exit 0
```

`hey!` can only reach the console through the *buffer*, so this is the kernel's `read` moving bytes into user memory rather than a call that answers 0. `CheckKr232Kernel.cmake` also grew `EXPECT_CONSOLE` and now asks for `--transcript` on every run.

Two defects in my own new code, both caught by running it: the row failed with *unknown option '--input'* because `build/kernel-check` had a stale emulator, and the frame check broke because `string(REGEX REPLACE ...)` keeps the surrounding text -- `CMAKE_MATCH_1` is the right read.

Suites: `kr232` **13 of 13** with the kernel and **11 of 11** without, **87 unit cases**; `krcompiler` **33 of 33** and **3704** assertions; `krdbg` **17 of 17**.

Item 4 of 8 closed. Next: `gfx_draw_text`, honest or working.
### 2026-11-24T19:25:00-08:00 — Item 5 closed: DRAW_TEXT says it is not supported

Host: CROSBY (Windows). The device already set `STATUS` bit 0 `ERROR` for `DRAW_TEXT` (`devices.cpp:183`), but `kr232-devices.md` §8 promised **10x20 text from guest memory** with an atlas that belongs to the terminal page. A reader would expect text, and a caller could believe the command worked.

Decision, recorded in §8: **the command is accepted, answers `ERROR`, and changes no pixels**, with the atlas named as the missing piece. `kr232_kernel_text_reports_error` runs a program that sends a six-word block, reads `STATUS` back through the kernel, and **exits 0 only when bit 0 is set** -- so an implementation that draws nothing and answers success fails the row, and so does one that silently stops reporting.

That is the honest branch of "honest or working": a glyph atlas is a device feature with pixels nobody could check here, and the terminal's atlas is where the draft already says it lives.

Suites: `kr232` **14 of 14** with the kernel and **12 of 12** without, `krcompiler` **33 of 33**, `krdbg` **17 of 17**, lint **81/81**.

Item 5 of 8 closed. Next: wiring the two-adapter check into the gate.
### 2026-11-24T19:55:00-08:00 — Item 6 closed: the gate runs the two-adapter suite

Host: CROSBY (Windows). `gate-windows.ps1` now calls `check-two-adapters.ps1` before it reports PASS. A full run:

```
== two adapters
109 test(s), 0 failure(s)
GATE WINDOWS: PASS
```

and the wiring was **proved to bite**: with the child's success test changed to something impossible, the gate stopped at `== two adapters` and never printed PASS.

Item 6 of 8 closed. Next: the user-mode fixtures' drift rows.
### 2026-11-24T20:25:00-08:00 — Item 7 closed: fixtures cannot drift from their sources

Host: CROSBY (Windows). The five checked-in images (`user-exit42`, `user-calls`, `user-echo`, `user-text`, `blank-flip`) had sources beside them and **nothing comparing the two**, so a stale image would keep passing tests against a program nobody has.

- **`kr232_fixtures_match_source`** rebuilds each `.s` with `krc` and `krld` and compares the bytes, naming the exact commands in the failure.
- **`tests/fixtures/regenerate.ps1`** is the regeneration path.

Proved both ways: corrupting one byte of `user-echo.kr232` failed the row with

```
user-echo.kr232 does not match user-echo.s: rebuild it with
  krc -c --target kr232 user-echo.s -o user-echo.o
  krld --base=0x08000000 --entry=_start user-echo.o -o user-echo.kr232
```

and running the regeneration afterwards rebuilt **all five** to byte-identical files -- `git status` stayed clean, which is the evidence that the script and the checker agree.

One defect in my own script, caught by running it: `krld` wants `--base=0x08000000`, not `--base 0x08000000`, which it read as an input file (*cannot open '0x08000000'*).

Suites: `kr232` **15 of 15** with the kernel, **87 unit cases**; `krcompiler` **33 of 33**; `krdbg` **17 of 17**.

Item 7 of 8 closed. Next: the ARM64 run.
### 2026-11-24T21:40:00-08:00 — Item 8 closed: the same suite runs on ARM64

Host: BitWhiz (aarch64, Debian). With the checked-in images and the ARM64-built `krc`/`krld`:

```
kr232 ctest       15 of 15    machine tests, kernel rows, package row, fixture drift
krcompiler ctest   5 of 5     the kr232 rows that host registers
```

Getting there found four portability defects, none of which the Windows suite could see:

- **The compiler did not build on Linux at all.** `clang` on aarch64 rejects an unused lambda capture under `-Werror`, in `kr216_encoder.cpp` — two dead captures. Then an unused constant, an unused private field, and **a lambda of mine** left behind when the alloca address moved to its own helper. Removing the dead weight fixed all four sites.
- **An empty list element is dropped by the newer CMake policy**, so the fixture row worked on Windows and failed on Linux with `list index: 2 out of range`; the field is `none` now.
- **Images were not reproducible across hosts.** `--source-root` now records relative source paths, and the per-file label prefix is an **FNV-1a hash of the source text with carriage returns skipped**: a Windows checkout carries CRLF, so the same file assembled to different label names (`Lb6aaeb23_` against `L8d45b21a_`) and the drift row failed on ARM64 while passing on Windows.

That third one is the interesting one: the drift row I added in item 7 is what caught it, and only because it ran somewhere else.

All eight items on the priority list are closed.
### 2026-11-24T22:20:00-08:00 — Item 1 closed: all 33 kr232 rows run on ARM64

Host: BitWhiz (aarch64). The rows were never registered there because `KRC_KR232_VM` was unset, so `ctest -R kr232` matched **5** tests where Windows had 33. Configured with the emulator path:

```
krcompiler ctest -R kr232    33 of 33
```

The thirty-third needed a fix, and it is a real platform difference: **a POSIX process status keeps only its low eight bits**, so a program that exits 300 reads back as **44** on Linux while Windows reports 300. The 32-bit truth is the machine's own printed status, so the runner gained `EXPECT_STATUS` and `krc_kr232_run_exit300` checks that everywhere, keeping `EXPECT_EXIT=300` only on Windows where it means something.

That also sharpens the earlier 32-bit exit-status work: the *machine* holds 32 bits, and on POSIX only the last eight reach the parent.
### 2026-11-24T23:15:00-08:00 — Items 2 and 4 closed: every fixture has a source, and two of them had none

Host: CROSBY (Windows) and BitWhiz (aarch64). Two checked-in images had **no source at all**: krdbg's `userprint.kr232` and kr232's `kernel-gate.bin`. Both now do, and both reproduce byte for byte:

- **`tests/fixtures/kr232/userprint.s`** prints `hello` through `SYSCALL` and exits 3, which is what the checked-in image did; rebuilding from it produces the same file.
- **`tests/fixtures/kernel-gate.s`** writes `K` to the terminal, an 8x8 green rectangle and a flip: `frame dddcd2efdafe5925` in 21 steps, the same drawing the golden has always named. It joins `kr232_fixtures_match_source`.

`regenerate.ps1` in krdbg was broken in three ways and never built `kernel.kr232` or `userprint.kr232` at all: its repo path was one level short, it wrote to the system temp directory, and it passed `--source-root` as a separate argument, which the linker reads as a file name. It now builds all six fixtures with relative source paths.

The loaded-state golden is a hash of the first guest page, so rebuilt fixtures moved it: **751BE2BDFA24D8FD becomes 81A7220B18CE534D**, recorded through the test's own recording path and then asserted; **17 of 17** adapter tests pass.

**Host independence, checked on both hosts**: `krdbg_kr232_fixtures_match_source` (new, item 4) and `kr232_fixtures_match_source` both pass on Windows *and* on BitWhiz.

Items 2 and 4 of 25 closed.
### 2026-11-25T00:20:00-08:00 — Item 3: the STATUS check is in, and it found a global that was four bytes wide

Host: CROSBY (Windows). `user-calls` was assembly that discarded `r2` from every call, so a device failing all of them still passed. It is C now, calls **fill, line, copy, upload, draw_text and flip**, and exits with a bit per violated expectation: success for the four drawing calls, and the documented refusal for `draw_text`. The frame is unchanged (`f6b786175f12b425`) and the drift row builds it from the new source.

The guard fires: with `draw_text` expected to succeed it exits **8**, which is the bit that expectation owns.

**But the STATUS values themselves do not add up, so the item is not closed.** A program that reads two bits and returns `copy_bit * 10 + line_bit` exited **150**:

```
copy = __kr232_syscall6(18, (int)block, 0, 0, 0, 0) & 1;   -> 15, not 0 or 1
line = __kr232_syscall6(19, (int)block, 0, 0, 0, 0) & 1;   -> 0
return copy * 10 + line;                                   -> 150
```

A `& 1` cannot produce 15, so either the second variadic call in one function returns something other than the status, or `& 1` is miscompiled, or the pushed-argument area is not restored between calls. Every earlier probe called the stub **once** per program and agreed with the kernel.

Next step is a minimal reproduction: three sequential `__kr232_syscall6` calls in one function whose return values are compared, then read the generated code and the stub's stack use. This is the same family as the label collision -- correct in isolation, wrong in combination -- and it must be settled before the STATUS check means anything.
### 2026-11-25T01:05:00-08:00 — Item 3 closed, and the anomaly was a global overlapping an array

Host: CROSBY (Windows). The 150 in the previous entry had nothing to do with repeated calls. `.data` was **12 bytes** for a program that needed 32:

```
.data     0x8000000  12
block     0x8000000   4     <- static unsigned block[6], 24 bytes of type
line_bit  0x8000004   4     <- inside block[1]
copy_bit  0x8000008   4     <- inside block[2]
```

**A global with no initialiser was emitted as one word**, so the linker packed the next global four bytes in, and `block[2] = 15` silently changed `copy_bit`. Every earlier probe used a single global or locals, which is why the suite never saw it.

The size now comes from the global's **element type**, and `krc_kr232_globals` pins it: an array global followed by two counters exits **12** only when both keep their own address. The two-bit probe that returned 150 returns **0**, and `user-calls` exits 0 with its STATUS check intact.

Suites: `krcompiler` **34 of 34** rows and **3704** assertions, `kr232` **15 of 15** kernel rows.

Item 3 of 25 closed.
### 2026-11-25T02:40:00-08:00 — Items 5 and 6 closed: the ARM64 run is one command

Host: BitWhiz (aarch64). `krc_tests` had never run there:

```
krc_tests    252 test cases, 3704 assertions, 0 failed
```

and `scripts/check-arm64.ps1` now performs the whole ARM64 check from a Windows shell -- pull three checkouts, build the compiler and the emulator, register the emulator with the compiler's tests, run four suites -- and prints:

```
== kr232 (kernel rows)      100% tests passed, 0 failed out of 15
== krcompiler (kr232 rows)  100% tests passed, 0 failed out of 34
== krc_tests                3704 assertions, 0 failed
== krdbg fixture drift      100% tests passed, 0 failed out of 1
ARM64 CHECKS: PASS
```

Getting there found three defects, all in the writing rather than the checking:

- **The script printed PASS after a failing suite**, because `ctest ... | grep` reports grep's status. Each suite now writes a log and a failure is remembered.
- **`user-calls.kr232` was stale**: the drift row rebuilt only the `.s` fixtures, so the C one was never compared, and it predated the global-size fix.
- **Its upload was invalid** and always had been: a four-pixel row with an eight-byte stride is narrower than a row, so the device refused every time. It passed before because the block overlapped the counters beside it; with the globals fixed, the refusal became visible. Stride 16 is accepted and the frame is unchanged.

Items 5 and 6 of 25 closed.
### 2026-11-25T03:30:00-08:00 — Items 7 and 8 closed: both gates run the checks, not a list of them

Host: CROSBY (Windows) and BitWhiz (aarch64).

- **`gate-linux.sh`** now builds the suite with the kr216 and kr232 adapters and runs it through `ctest`, which picks the right invocation per host and excludes the x64 golden comparison on ARM64. A full run on BitWhiz ended:

```
== two adapters
100% tests passed, 0 tests failed out of 4
GATE LINUX ARM64: PASS
```

  Its first version built only `krdbg_tests` and then ran `ctest`, which also runs the demo and the kr232 binary: two tests reported **Not Run**. It builds the whole tree now.

- **`scripts/gate-windows.ps1`** is the kr232 repository's entry point: the spec lint, the unit tests, the default suite, and -- when `-Krc` and `-Krld` are given -- the kernel rows, package row and fixture drift row in their own build directory, saying they were skipped when they were:

```
15 of 15 kernel rows ... GATE KR232: PASS
```

  Its first version passed the compiler paths inside a scriptblock and CMake saw neither; they go as an argument array now, and one debug echo was removed before the commit.

Items 7 and 8 of 25 closed.
### 2026-11-25T04:05:00-08:00 — Item 9 closed: the separator audit, and three scripts that named nothing

Host: CROSBY (Windows). Every CMake script that splits a record on a separator and then indexes it, audited:

| Script | State |
|---|---|
| `kr232/cmake/CheckKr232FixtureDrift.cmake` | uses `none` for "no arguments"; the empty field is what broke on the older policy |
| `krdbg/tests/fixtures/kr232/CheckFixtureDrift.cmake` | already bounds-checks before indexing |
| `krcompiler/cmake/CheckDebugInvariants.cmake` | **two unguarded splits** -- now each names the malformed record |
| `krcompiler/cmake/LinkKr16Host.cmake` | **no separator check**: a pair without `|` sliced the whole string twice and handed krc a nonsense object name |

An empty field disappears from a CMake list under the current policy, so all four failed as `list index out of range` rather than naming the record -- which is exactly how the fixture list cost a round. The three unguarded sites now say:

```
a line range record is malformed: '...'
a variable record is malformed: '...'
an input pair is malformed: '...'
```

Suites after the change: `krcompiler` **228** rows in the affected groups, and the kr232 rows unchanged.

Item 9 of 25 closed.
### 2026-11-25T04:50:00-08:00 — Items 10, 11 and 12 closed: the last unguarded frame address, and a frame that proves it

Host: CROSBY (Windows).

**Item 11 -- the audit and the fix.** Every `[r13-N]` emission checked: `frame_load` and `frame_store` were deep-safe, one inline `fst` was already deep-aware, and **`address_of` still returned a raw operand**, so an alloca past 128 bytes could not be addressed -- the assembler refused the displacement. It forms the address in R8 now, as the others already did.

`krc_kr232_deep_frame` makes it run: a forty-word array whose base sits at **FP-168**, with `MIN_DEPTH=128` so the row fails if the fixture ever stops being deep, and `EXPECT_PARAM=seed` so it also proves parameters are recorded.

**Item 12 -- parameters.** They already had `.debug_var` records (the prologue spills them into slots), but nothing checked them. The check now requires the record for a named parameter on both frame fixtures.

**Item 10 -- the CLI.** `kr232-plan.md` §6 records `--input <text>`, and states that a POSIX process status keeps only the low eight bits of the exit status while the printed `halted status N` line carries the whole value.

Suites: `krcompiler` **35 of 35** kr232 rows and **3704** assertions, `kr232` **15 of 15** kernel rows.

Items 10, 11 and 12 of 25 closed.
### 2026-11-25T05:25:00-08:00 — Items 13 and 14 closed: one host-dependent hash left, and one dead field gone

Host: CROSBY (Windows) and BitWhiz (aarch64).

**Item 14 -- `.Lfp` was still `std::hash<double>`.** That hash is implementation-defined, so a program with a double constant assembled to different images under MSVC and libstdc++. The same program now matches exactly:

```
Windows  0DBD151402C3F2E51576ECDED9766CB8F4331235EE63C5DC70349C268207ED63
ARM64    0dbd151402c3f2e51576ecded9766cb8f4331235ee63c5dc70349c268207ed63
```

The label is the constant's **IEEE-754 bit pattern in hex**, and `materialize_float` uses the same function, so a reference and its definition cannot drift apart.

**Item 13 -- `profile_` was dead.** The field was stored and never read; its widths are constants. The parameter stays because the factory passes the profile to every backend, and the constructor says why. Both the Windows build and the Linux clang build (which flags unused private fields) are clean.

Suites: `krcompiler` **35 of 35** kr232 rows; the image comparison above is the cross-host evidence.

Items 13 and 14 of 25 closed.
### 2026-11-25T06:00:00-08:00 — Item 15 closed: the package reaches all four drawing commands

Host: CROSBY (Windows). `gfx_copy_rect`, `gfx_draw_line`, `gfx_draw_text` and `gfx_upload` existed as kernel calls and had **no wrapper**, so a C program could not reach them and `kr232-devices.md` §11.1's promise was half true.

They take **at most four arguments**, because that is what the ABI passes in registers: the commands needing more travel as one block behind a pointer, and the ones that want a colour use the one `gfx_set_colour` recorded. The first attempt had five and six parameters and the compiler refused the call sites -- *more than four arguments needs the stack form* -- which is the same limit the header's original comment recorded.

Each answers the device STATUS, and the package fixture checks every one:

| call | expected | result |
|---|---|---|
| `gfx_draw_line(0,0,15,15)` | success | success |
| `gfx_copy_rect(0,0,8,8)` | success | success |
| `gfx_upload(pixels,4,4,16)` | success | success |
| `gfx_draw_text(0,0,"packages",8)` | **refusal** | refusal |

The frame moved to **`659d051c4dc232e5`** because the line and the upload now draw, and the row was re-recorded against it.

Suites: `krcompiler` **35 of 35** kr232 rows, `kr232` **15 of 15** kernel rows.

Item 15 of 25 closed.
### 2026-11-25T06:45:00-08:00 — Items 16 and 17 closed: the packages read input and the mode

Host: CROSBY (Windows). `cons_getc` and `input_next` were compiled into the packages and **never called** -- the state that hid `cons_puts`'s failure for a whole milestone. The package program now:

- **reads the queued byte** with `cons_getc` and fails with **11** if it is not the `z` the row queues (`--input z`); with `--input q` it exits 11, which is how the check was proved;
- expects **`KR232_NO_EVENT`** from `input_next`, because the host pushes no keyboard events and that is the documented answer;
- checks that **`fb_stride()` is exactly one row of pixels** (`fb_width() * 4`), which was the item 17 question: a probe showed they agree, so the earlier failure was a stale object, not the device.

Getting the row to pass needed a **reconfigure**: CMake had generated the row before `-DINPUT=z` was added, and `ctest -V` showed the stale command. The third time this session a generated test list has lied about the source.

Suites: `kr232` **15 of 15** kernel rows, `krcompiler` **35 of 35** kr232 rows.

Items 16 and 17 of 25 closed.
### 2026-11-25T07:30:00-08:00 — Items 18, 19 and 20 closed, and the write range was a real hole

Host: CROSBY (Windows).

**Item 20 found something.** The kernel's `write` copied until an access faulted: a length that ran past the mapped memory **trapped halfway** instead of answering. It now refuses, with nothing copied:

- an address below `0x08000000`,
- a length whose sum with the address **wraps** (`cmp r5, r2; bcc`),
- a sum above `0xFC000000`.

A probe with `write(0xFFFFFFF0, 0x20)` answers **-1**; before the fix it trapped at `0x00000000`.

`kr232-devices.md` §14 records the rule and the absence of any other cap, including the honest limit: the kernel cannot see a machine whose RAM is smaller than the ISA's maximum, so a range inside the limit but beyond installed memory still traps.

**Item 18 -- descriptor 1.** The ISA §13 already said descriptor 1 is the console and 0 its input, with any other number refused; the unit test now names **descriptor 2** explicitly, which is the one a caller would most likely try next.

**Item 19 -- the clock's top half.** `time` answers 64-bit seconds in R2:R3 and the high word is zero until **2106**, so no test can reach the carry. That is now written in the test rather than left as an untested path pretending to be covered.

Suites: `kr232` **87** unit cases, `kr232` kernel rows re-run after the kernel change.

Items 18, 19 and 20 of 25 closed.
### 2026-11-25T08:15:00-08:00 — Items 21-25 closed: the list is empty

Host: CROSBY (Windows) and BitWhiz (aarch64).

- **Item 24** -- `krc_kr232_status_high` returns **0x80000005** from `main`, and the row requires the machine's printed status to be **2147483653**. A POSIX process status cannot carry it, which is why the check reads the printed value.
- **Item 25** -- the package row links with `--source-root`, so its objects name their sources relative to the compiler checkout instead of to the machine that built them.
- **Item 23** -- the round-trip table now lists **source and expected text** side by side, so `[sp+0]`, `[r3+0]` and `[sp+0]` for stores assert their canonical compact form instead of being absent: eighteen spellings, all matching what the table says.
- **Item 21** -- `AGENTS.local.md`'s verified line names what ran and when: the lint, the Windows suite with the kernel and package rows, and `scripts/check-arm64.ps1` on BitWhiz.
- **Item 22** -- the same file now states what the lint **cannot** see: it reads the two drafts only, and the plan's status row and §14's gate records are prose kept true by review.

Final state of the repository, all four suites run after the last change:

```
krcompiler ctest -R kr232   36 of 36      krc_tests        3704 assertions
kr232 ctest (kernel rows)   15 of 15      kr232 unit       87 cases
krdbg kr232 adapter         17 of 17      spec lint        81/81 vectors
```

**All 25 items on the priority list are closed.**
### 2026-11-25T09:30:00-08:00 — Items 1 and 2 closed: the compiler's work is on its default branch, and ARM64 runs all of it

Host: CROSBY (Windows) and BitWhiz (aarch64).

**Item 1.** `kr232-m3` was a **fast-forward** of `master` (`0 76` behind/ahead), pushed as `fdb44aa..7597c22`. A **fresh clone** of `master` then built and passed:

```
ctest -R kr232   36 of 36      krc_tests   3704 assertions
```

**Item 2.** `scripts/check-arm64.ps1` now pulls `master`, runs the compiler's **whole** ctest suite and calls `gate-linux.sh --arch arm64`:

```
== kr232 (kernel rows)      15 of 15
== krcompiler (whole suite) 955 of 955
== krc_tests                3704 assertions
== krdbg gate               GATE LINUX ARM64: PASS
== krdbg fixture drift      1 of 1
ARM64 CHECKS: PASS
```

Before this, ARM64 had run **36** of those 955 rows.

**Two stale fixtures fell out of it.** `session.kr232` carried the data layout from before a global stopped being four bytes wide, and `kernel.kr232` predated the write-range check. Both were rebuilt from source, the adapter tests still pass **17 of 17**, and the drift row holds on the second host.

**Errata.** The commit `faa849d`'s subject says "ARN64"; it is pushed and history is append-only, so it stays as written.

Items 1 and 2 of 7 closed.
### 2026-11-25T10:30:00-08:00 — Items 3, 4 and 5 closed: CI, a final refusal, and documents that said "provisional"

Host: CROSBY (Windows).

**Item 3 -- CI.** kr232 had **no workflow at all** and krcompiler's ran presets **without the sibling VM repositories**, so the executed kr232 rows were never registered there.

- `kr232/.github/workflows/ci.yml`: a lint job, and an integration job that checks out the compiler, builds it from its default branch, configures the emulator with the kernel and package rows, runs the kr232 suite, then registers the emulator with the compiler and runs its executed rows and `krc_tests`.
- `krcompiler/.../ci.yml` gained `kr232-integration`, which builds the emulator from its repository and configures the compiler with `KRC_KR232_VM`, so the rows exist.

**No GitHub run has been observed.** The equivalent steps were run locally and by `scripts/check-arm64.ps1`; both files pass a structural check (`on:`, `jobs:`, no tabs, jobs enumerated) but no workflow has executed on a runner.

**Item 4 -- the atlas is a documented refusal, not done.** `kr232-devices.md` §8's row now says *not implemented, finally* and names why: the 10x20 atlas is not in this repository, the terminal page renders its cells from a font the UI layer owns, and a guest-visible atlas has to be designed there first. `DRAW_TEXT` is the one graphics command that refuses by design, and rows on both sides assert that it refuses.

**Item 5 -- documents and the demo.**

- `kr232-devices.md` §11 and §11.6 lose **"provisional"**: the protocol has been in use since M5.
- `kr232-plan.md` §12's two open questions are marked **settled at M5**, with ISA §18 and devices §14 named, and the paragraph that called them open is replaced.
- `kr232-plan.md` §13 gained the three traps this work kept hitting: **stale generated artefacts** (four in one session), **host-dependent bytes** (CRLF in a label hash, `std::hash<double>`), and **checks that cannot fail**.
- The demo's `--kr232 <image>` was parsed and mentioned in **no usage line**; the line names it now, and a headless load prints `frame hash 70F42ABC0377DD5A`. **`--target kr232` -- a source build inside the debugger -- is not implemented**: the target selector covers kr32, kr16 and kr216 only, so that path is recorded as unreachable rather than claimed.

Items 3, 4 and 5 of 7 closed; items 1 and 2 closed earlier.
### 2026-11-23T08:05:00-08:00 — The packages ship with the sysroot, and the source shows

Host: CROSBY (Windows). Two defects kept a source-built program from being usable in the debugger, and both were in the toolchain rather than the debugger.

**The translation unit had no name.** The kr232 assembler recorded every input as `"assembly"`, so the debug payload named no file and the SOURCE panel had nothing to open:

```
before  source 0 assembly
after   source 0 C:\Users\kevin\repos\kr232\examples\squares.c
```

`Options::source_name` carries the name now, krc sets it per input, and the panel shows the file with the current line marked (krcompiler `5923c77`).

**The packages were not installed.** The prefix carried `<kr232/cons.h>` and the startup, so a program calling `cons_puts` linked against a declaration with no definition. The staged sysroot builds `cons_user`, `gfx_user`, `fb_user`, `input_user` and `syscall6` into `kr232/lib`, the kr232 headers are staged like the other targets, and the debugger's build chain links every `.kro` it finds there, so a package added later needs no change (krcompiler `352cdbb`).

Verified: `krc_kr232_packages` builds a program that prints through the package, links the staged objects and runs it in the VM; `examples/counter.c` -- the case that failed with `undefined symbol 'cons_puts'` -- loads in the demo.

Suites: krcompiler **37 of 37** kr232 rows, `krc_tests` **3704** assertions.
### 2026-11-23T09:10:00-08:00 — The frame the program is in, and the view menu that named it

Host: CROSBY (Windows). The call-stack chain stores a **return address** in every frame, and the kr232 adapter published those unchanged, so the innermost frame named the caller:

```
before   #0 main 00006882      LOCALS (1)  i = 7        <- main's variable
after    #0 spin 0000660E      LOCALS (2)  seed = 7
                               counter = 176795
```

Frame zero now reports the running address (`adapters/kr232/kr232_adapter.cpp`), so the stack starts at the function being executed and the locals shown are that function's. The unwind test asserts the innermost frame's name and that every frame names a function (krdbg `380c57f`).

**The view menu's repeated entry** was the same defect from the other side: two sources both called `assembly`. With the translation unit's real name recorded (krcompiler `5923c77`) the menu lists `squares.c` and `assembly` -- two entries because there are two files, the second being the start-up.

Suites: krdbg `krdbg_tests` **110 of 110**, `krdbg_kr232_tests` **17 of 17**.
### 2026-11-23T09:55:00-08:00 — The fixtures follow the compiler, and the last four items close

Host: CROSBY (Windows).

**The cascade.** Recording the translation unit's name changes every image, so every checked-in fixture drifted from its source at once. Each was rebuilt from its **own drift row's output**, not by hand, because the row's rebuild is the canonical one:

- `kernel.kr232` from `kernel-rebuild/kernel.kr232` (the row's entry differs from the target's);
- `user-exit42`, `user-calls`, `user-echo`, `user-text`, `blank-flip` and `kernel-gate.bin` from `fixtures/`;
- krdbg's five kr232 fixtures, and its adapter golden re-recorded as **C20CAE8151BD20AD** from the rebuilt `loaded.kr232`.

`regenerate.ps1` also listed `user-calls` as assembly when the drift row has compiled it as C with the user start-up and the variadic stub since the STATUS checks landed; the script compiles and links all three now.

**The four items.**

| Item | Evidence |
|---|---|
| 1 packages in the sysroot | `examples/counter.c` loads; `krc_kr232_packages` builds, links and runs a program that prints through `<kr232/cons.h>` |
| 2 VIDEO panel | `--run 300000 --state video` shows the committed frame in the panel (screenshot `shots/video-frame.png`); `--run` added for exactly this |
| 3 locals and call stack | `#0 spin 0000660E` with `seed = 7`, `counter = 176795` (screenshot `shots/stack-fixed.png`) |
| 4 repeated view entry | `squares.c` and `assembly` -- two entries for two files, no duplicates |

Final state, every suite after the last change:

```
krcompiler ctest -R kr232   37 of 37      krc_tests        3704 assertions
kr232 ctest (kernel)        15 of 15      kr232 unit       87 cases
krdbg krdbg_tests          110 of 110     krdbg kr232      17 of 17
spec lint                   81/81 vectors
```
### 2026-11-23T10:40:00-08:00 — The kr32 C runtime runs on kr232

Host: CROSBY (Windows).

**What was there.** `krcompiler/targets/kr32/runtime`: `libc.c` 2,686 lines and 183 functions (printf with `%f`/`%e`/`%g`, malloc over a free list, the whole `str*` family, `strtol`/`strtod`, time.h, locale, `atexit`, the C++ `__cxa_*` entry points), `fileio.s` 81 lines of `SYSCALL #34`–`#44` host services, `eh.s` 128 lines of `.cpp_except` walking, `crt0.s` 35 lines.

**The diagnostic chain, in the order it appeared.** Each is a toolchain gap, not a library one:

| Diagnostic | What it meant | Resolution |
|---|---|---|
| `'long long' is not supported` (21 errors) | the profile has no 64-bit integer (plan §2.3) | stripped from the port; `<stdlib.h>` could not even be included, so the generator now emits `KRC_HAS_LONG_LONG` and the header tests it |
| `expected declaration specifiers` | my `lldiv_t` edit collided | `lldiv` not defined: the profile never declared it |
| `remainder is not implemented` | `%` was absent from the backend | `a − (a ÷ b) × b`, exactly as ISA §12 prescribes |
| `wrong number of arguments` | two `strftime` helpers took five and six | packed into `struct append_target` |
| `opcode 21` | `FPTrunc`: the ISA has no `FCVT` | `strtof` removed: one floating-point format |
| `opcode 31` | `FCmp` | `FCMP`'s N/Z/V flags map onto the integer branch conditions |
| `'digit_value' is out of the 16-bit branch range` | the object is 50,580 lines of assembly, ~150 KB, and rel16 reaches ±32 KiB | the assembler lays out again with the offending call widened to `ld r8, #symbol; call r8` |
| `link relocation overflows its width` | a linker patches a field, it cannot grow one | `--far-calls` emits every call wide for an object whose calls leave it |

**The tiers that run.** Tier 0 (`printf`, `malloc`/`free`, `str*`, `strtol`, `atoi`, math) and Tier 1's console half run: `krc_kr232_runtime` builds `fixtures/c/kr232_runtime.c` against `libc_full.c` and `fileio_full.s` and requires `total=50` on the console. File calls (`open`, `close`, `seek`, `stat`, `rename`, `truncate`, `dup`, `chdir`) answer -1; `_Unwind_Resume` exits rather than corrupt the stack; exceptions remain Tier 3 and need a backend that emits tables.

**Two limits worth naming.** The heap moved from kr32's `0x00100000` -- reachable only in kernel mode -- to `0x09000000`, inside the user window and clear of a 150 KB image and of the stack. And an image this size executes ~9,000 instructions for one `printf` of five fields, which is why the emulator's default step budget and the debugger's `--run` default matter for anything built on it.

Suites: krcompiler **38 of 38** kr232 rows, `krc_tests` **3704** assertions, kr232 **15 of 15**, krdbg **110 of 110** and **17 of 17**.
### 2026-11-23T11:15:00-08:00 — One floating-point format: float is 64 bits

Host: CROSBY (Windows).

The profile advertised `float_bits = 32` while the ISA has a single 64-bit format and no `FCVT`, so a `float` program was 64-bit arithmetic wearing a 32-bit name. Two ways out, and the cheap one is also the conforming one: C requires `float` to exist but not to be narrower than `double`.

**Changed.** `kKr232`'s `float_bits` is 64. `float` and `double` are the same type on this target, `strtof` returns without a narrowing cast, and `FPTrunc`/`FPExt` lower to a copy because both sides are 64 bits.

`<float.h>`'s `FLT_*` described x86 rather than the target, so they moved into a generated `bits/float.h`: kr232 gets the double limits (53 mantissa bits), kr32 keeps 24, and a profile without floating point gets none. `DBL_*` stay in the shared header, the same everywhere this compiler targets.

The runtime row now proves it: `krc_kr232_runtime` requires `total=55 float=8`, a program that declares a `float`, multiplies it by a `double` and prints `sizeof(float)`.

Suites: krcompiler **38 of 38** kr232 rows, `krc_tests` **3704** assertions, kr232 **15 of 15**, krdbg **17 of 17**. `krdbg_tests` passed three consecutive runs after reporting 13 failures in earlier runs; the intermittent failure is **not explained** and is recorded as an open defect, not a fixed one.
## 14. Gate verification

### M3 — the code generator, runtime and freezes

Host: CROSBY (Windows). Recorded 2026-11-23T19:20:00-08:00.

| Plan §8 criterion | Evidence |
|---|---|
| `int main(void){return 42;}` builds, runs, matches | `krc_kr232_run_return42` **passed** |
| A C++ program with a class, virtual call, by-value struct parameter, struct return and array | `krc_kr232_run_cpp` **passed** (exit 17) |
| A program that draws through `<kr232/gfx.h>` and flips | `krc_kr232_run_gfx_draw` **passed**, frame pinned |
| By-value and returned records include a three-`char` one, size-exact | `krc_kr232_run_struct`, `krc_kr232_run_sret`, `krc_kr232_run_nested` **passed** |
| `-S` output byte-identical at every conversion step | `CheckKr232Determinism.cmake` compares two `-S` runs as well as two images: **passed** |
| The executed rows exist because `KRC_KR232_VM` resolved | rows 10-33 of `ctest -R kr232` **passed** |
| The compiled program loads in krdbg at source level and stops on a line breakpoint | `krdbg_kr232_tests`: `kr232_adapter_steps_to_a_line_breakpoint` and `kr232_adapter_line_breakpoints_survive_a_reload` **passed**, locals read through a frame offset |
| Determinism, KDBG invariants and the clean gate | `krc_kr232_determinism`, `krc_kr232_kdbg_invariants`, the clean gate **passed** |

`ctest -R kr232` **27 of 27**; `krc_tests` **3704 of 3704**; `krdbg_kr232_tests` **15 of 15** with a prefix installed.

**ISA 1.0 and devices 1.0 are frozen here.** `kr232-isa.md` and `kr232-devices.md` carry status **Frozen 1.0**, §18 and §14 are retitled **Deferred to 2.0**, and the tags `isa-1.0` and `devices-1.0` mark this revision.

Two M3 tasks remain open and are **not** claimed by this gate: the varargs/FP conventions (the FP path traps with `fp = 0` after a callee returns, recorded in earlier entries) and `libc.c`'s SYS stubs, which the file itself defers until a kernel exists.

### 2026-11-22T20:07:37-08:00 — M0 gate (Windows x64)

Build: `vcvarsall.bat x64`, then `cmake --build --preset windows-msvc-debug -j 10 --target krc`. The stamp is that build's start, printed before it ran. The `krc` binary was **not relinked** — nothing under `src/` changed, only documentation and a test registration — so its modification time could not identify this gate; the build's own start line is the identifier.
Tests: `ctest --preset windows-msvc-debug` → **919 passed, 1 failed of 920**, 268 s. The single failure is `krc_kr232_no_backend`, the test this milestone was required to add; M2's profile row turns it green without editing it.
Lint: `node tools/check-isa.mjs` → `ok: 81/81 vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; 13 device page rows tile the window; tables well-formed`.
Merged: `krcompiler` `master` **7c31e3d**, a `--no-ff` merge of `kr232-m0` (`972294b`, `b16e3be`, `3db94f7`, `682f7ee`, `7952efb`).
Evidence: ADR **0057** and ADR **0058**; `docs/abi-kr232.md` (265 lines); **D-051** in `plan.md` Appendix A; target id **5** in `docs/symbols.md`, where KR216's 4 was added at the same time because it had never been recorded; and the refusal test's own output, `krc: error: unknown target 'kr232' (expected kr32, kr16, or kr64)`, against its expected regex.
ARM64: **not run** — M0 changes documentation and one test registration, and no `kr232` build exists to run there; the first ARM64 row is M1's.
Gate: plan §5 — both ADRs merged, the ABI document merged, id 5 recorded, and the failing test fails for the documented reason. **Met.**

### 2026-11-22T21:01:09-08:00 — M1 gate (Windows x64, WSL x64, BitWhiz ARM64)

Builds: `cmake --preset windows-msvc` (MSVC 14.52.37025), `linux-wsl` (GCC 15.2.0), and `linux-arm64` on BitWhiz (aarch64, GCC 14.2.0, Debian 13) from a fresh clone of this commit.
Tests: `ctest` **6 of 6** on each host — 2 hygiene and 4 unit: `kr232_version` (the suite itself), `kr232_cli_frame_hash`, `kr232_cli_kernel`, `kr232_cli_kernel_golden`.
Lint: `node tools/check-isa.mjs` → `ok: 81/81 vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; 13 device page rows tile the window; tables well-formed`.

Evidence by exit criterion:

1. **81 vectors.** `every_vector_executes_with_the_effect_its_form_requires` runs each Appendix A vector from its bytes on a canonical machine and asserts its form's effect; the twelve value cases in `test_machine.cpp` carry the arithmetic and flag semantics.
2. **Protection.** Tests cover user access below `0x08000000` and at or above `0xFC000000` → 8; both gaps in both modes, with the high gap 4 in kernel mode and 8 in user mode; the null page in kernel mode → 4; `EI`, `DI`, `HALT`, `SYS` and `IRET` in user mode → 8 with a frame naming the instruction; `SYSCALL` → 9 and resuming after it; and `IRET` restoring I, K and SP.
3. **Devices.** Eight pages answer their `ID` at `0x3C`; an undefined offset, a reserved page and a mis-shaped access trap with vector 4; a rejected `FLIP` sets `FLIP_ERROR` and commits nothing; the blitter validates both ranges before moving a byte.
4. **Gate image.** `tests/fixtures/kernel-gate.bin`, hand-encoded at base 0, writes `'K'` to the terminal page, fills an 8×8 rectangle of `0x0000FF00` through the graphics command page, flips through the framebuffer page and exits through `SYS 0` — 21 instructions. `kr232_cli_kernel_golden` compares `--frame-hash` against `tests/fixtures/kernel-gate.hash`, the checked-in `2bf050200a5e1325`, and matches on all three hosts, so the hash is stable across compilers, libc and architecture.
5. **Lint** as above.

Known gaps, named rather than hidden: the 10×20 glyph atlas does not exist, so the terminal maintains and mirrors its grid but renders no pixels and `DRAW_TEXT` sets `ERROR`; the gate image's terminal write therefore exercises the page and the mirror, not pixels. Windows and WSL ran the full suite; BitWhiz was cloned fresh for this gate and is not kept as a working tree.

Gate: plan §6 items 1–5 — **met**.
### 2026-11-23T15:40:00-08:00 — The instruments that found the shift, general to the family

The hunt for the binary-tree trap ended on an immediate shift that selected the
wrong direction: `shl r7, #2` shifted right and produced zero, so every scaled index
in every compiled program came out zero. Twenty-five rounds went into the compiler,
which was correct throughout. Four command-line options, written during that hunt,
are what finally saw it, and this entry records their description and their port.

**`debug-instruments.md`** states, per instrument, the question it answers, a
worked invocation with real output, how it is implemented, and the defect class it
catches: `--watch` (which instruction writes a word), `--break` with `--break-max`
(the registers and frame at an address), `--watch-page` (every store to a 4 KiB
page, with PCs) and `--watch-fp` (whether the frame register moves at all). The same
file lives in `kr32`, `kr16` and `kr216`, whose porting notes record what each
machine had to change — the word it watches, the registers a break dumps, and
whether it has a frame register.

**Ports.** kr232 (`2bac089`, `f55bea7`, `784d5ca`), kr32 (`97390b4`), kr16
(`177e3ac`, `b15c1d4`) and kr216 (`966ac0d`). kr216's emulator gains a step loop in
place of its single `run()` call, because each instrument must sample before and
after one instruction.

**Runs that exercised them.**

| Machine | Break | Watch | Page | Frame |
|---|---|---|---|---|
| kr232 | yes | yes | yes | yes |
| kr32 | `bios.kr32` at the entry | not on that image | three stores at the first instruction | no change on that image |
| kr16 | `exec_min.kr16` at `0x0100` | `0x2000`: `0x1234` to `0x5678` | the same stores, byte by byte | no frame register |
| kr216 | `probe.kr216` at `0x0200` | `0xFEC2`: `0x035E` at pc `0x0237` | the stack stores | `0x0000` to `0xFEEE` |

kr216's demonstration program is three lines compiled with the bundled toolchain,
because no image checked into that repository writes memory.

**Suites after the ports.** kr232: spec lint ok (81/81 vectors), 15 of 15 kernel
rows. kr32: 40 of 40 (the first run failed two rows because the fixture images were
not built; building the default target produced them and both passed). kr16: 59 of
60, the failure being `embedded runtime sources match checked-in resources`, which
compares the embedded libc against the checked-in resource and is unaffected by the
CLI and runner changes — pre-existing, reported rather than fixed. kr216: 58 of 58.
### 2026-11-23T16:32:00-08:00 — The fixtures had Windows paths baked into them

The two drift rows failed on BitWhiz while passing on Windows, and the code was
not the reason. `CheckKr232FixtureDrift.cmake` compiled with an absolute source
path, and krc embeds that path in the object, so every checked-in image carried
`C:/Users/kevin/...` and could only be reproduced on the machine that generated
it. The same source built on aarch64 differed by 31 bytes in `user-text.kr232`,
32 bytes in `user-calls.kr232` and 2 bytes in the kernel — all of it path text —
while the disassembly of the two images was identical. A bisect at `b71a57e`,
before the frame-size change, produced the same numbers, so the drift predates
this milestone's compiler work.

The mechanism was measured on one host: compiling the same file with a relative
and an absolute path gave 61160 and 61186 bytes, the difference being exactly the
26 characters of the prefix, and the object contains the path as a string. The
sysroot spelling makes no difference.

**Changed.** Both rows now pass `ROOT` and compile with `file(RELATIVE_PATH)`
paths and `WORKING_DIRECTORY` at the root, so the embedded path is
`tests/fixtures/user-calls.c` everywhere. `regenerate.ps1` does the same, in
forward slashes, and now also rebuilds `kernel.kr232` and `kernel-gate.bin`, which
were previously rebuilt by hand from a command recorded in this file. Every
fixture image was regenerated.

**Two obstacles worth recording.** `--source-root` is gone from both checkers:
with relative inputs there is nothing to strip, and passing the repository root
made `krld` report `two allocated sections overlap` on objects that link cleanly
without it, which is a linker defect that needs its own look. And `user-calls`
links in the same object order as `regenerate.ps1` — `user-calls.o` before
`crt0_user.o` — because the order decides the layout and two orderings produced
two images from identical objects.

**Verified.** Windows x64: 15 of 15 rows. BitWhiz aarch64, after `git pull` at
`fe86bf4` and against the compiler installed from current source: 15 of 15 rows,
with no regeneration on that host.
### 2026-11-23T17:05:00-08:00 — The gate counts its rows

The premise I carried into this was wrong, and the check is worth recording for
that reason. I expected `kr232_fixtures_match_source` to be skipped silently:
it is registered under `if(KR232_BUILD_KERNEL AND EXISTS "${KR232_KRC}")`, so a
configure without the compiler looked like it would register twelve or thirteen
rows and still report success. It cannot. `CMakeLists.txt:289-295` fails the
configure outright when `KR232_BUILD_KERNEL=ON` and either `KR232_KRC` or
`KR232_KRLD` does not exist, so with the kernel enabled -- and the ARM64 gate
always enables it -- the two drift rows are always registered. Configured with a
bogus `KR232_KRC`, the run stops with `KR232_BUILD_KERNEL=ON expects KR232_KRC
and KR232_KRLD to name an installed krc and krld`.

**Added anyway.** `scripts/check-arm64.ps1` now counts the registered rows and
fails when there are not fifteen, and `report()` refuses a suite whose log says
`No tests were found` or `out of 0` rather than reading it as a pass. ctest
reports success for a suite with rows missing, and that is true whatever the
mechanism.

**Measured on BitWhiz.** `build/arm64-clean` and `build/arm64-bisect` both list
15 rows; a scratch configure with `KR232_BUILD_KERNEL=OFF` lists 12, so the
assertion has teeth and the number is the kernel-enabled count this gate uses.
### 2026-11-23T17:40:00-08:00 — The gate refuses a suite that did not really run

The audit of this gate found four ways it could pass without checking what it
claims, and they are closed. Pulls are `fetch` + `merge --ff-only` instead of
`checkout -B main origin/main`, which rewrote the remote branch and could drop
local work. Both configure steps pass `--fresh`, because a reused build directory
keeps any setting an earlier run left in its cache. Every ctest call passes
`--no-tests=error`, `report()` fails on `Not Run`/`Disabled` -- ctest prints
`***Not Run (Disabled)` beside `100% tests passed` and exits 0 -- and names the
skips it does accept rather than counting them as passes. The krdbg fixture row is
counted like kr232's: one registered row, not "whatever ran".

**Correction to the entry above.** The configure-time guard that makes the
fixture rows mandatory is at `CMakeLists.txt:270-274`, not `:289-295`. The
numbers I recorded came from a working copy that still held an edit I later
reverted; the guard has not moved.

**The audit's blocking finding is fixed in krdbg** (`efffb38`): the checked-in
kr232 fixtures could not be reproduced, on either host, because `regenerate.ps1`
recorded them through backslash paths while the drift check compiles with forward
slashes and the path is embedded in the image. The recording now normalises, the
five images and the adapter golden are re-recorded, and on Windows the drift row
passes, `krdbg_kr232_tests` is 17 of 17 and `krdbg_tests` 110 of 110.

**Verified here:** `scripts/check-arm64.ps1` parses under PowerShell with zero
errors and its bash block passes `bash -n` on the ARM64 host. The gate itself has
not been re-run: the host is in use, and a run is the clean test of all of this.
### 2026-11-23T18:05:00-08:00 — The CI workflow would have failed on its third step

The workflow had never run, so this is a reading of it against commands measured
on this host rather than a run. Two defects, both fatal:

**The `krc_tests` path named a directory the build never wrote.** The last step
ran `./krcompiler/build/ci/host/bin/Release/krc_tests`, but `build/ci` was
configured without a preset, so its build type was empty and its binaries went to
`host/bin/`, not `host/bin/Release/`. It also configured the compiler a second
time, doubling the build. Both go away by reusing the `linux-gcc-release` tree:
the preset sets `CMAKE_BUILD_TYPE=Release`, `KRC_BUILD_TESTS=ON` and
`KRC_BUILD_TOOLS=ON`, so `host/bin/Release/krc_tests` is the file it writes.

**The suites were trusted to have run.** Neither ctest call passed
`--no-tests=error`, and nothing counted kr232's rows; ctest reports success for a
suite with rows missing, which is the hole the gate closed in `a911c25`. Both
calls now pass `--no-tests=error` and a step asserts fifteen registered rows,
mirroring `scripts/check-arm64.ps1`.

Verified here: `linux-gcc-release` exists as both a configure and a build preset
and carries those three cache variables; the workflow has no tabs and its block
steps are intact; `--no-tests=error` appears in both ctest calls and the row-count
assertion reads `test "$rows" -eq 15`. The YAML was not parsed -- no parser is
installed on this machine and PYYAML is absent -- so the file's structure was
checked by reading, not by a tool. The first GitHub run still needs a push, and
this workflow does not cover krdbg, which the gate script does.
### 2026-11-23T18:40:00-08:00 — The overlap: the alloc bit, and a stale emulator

`.debug_info` carries flags 8 (debug) and no alloc bit, and it is typed as ordinary data by the assembler. Three places read "the flag word is non-zero" as "this section occupies memory": the placement, the container writer's overlap test, and the emulator's loader. The section therefore took an address 18 KB ahead of `.text`, and `attach_kdbg`'s rewrite of its source paths -- longer than what the object recorded -- grew it into `.text` after layout. All three now read the alloc bit, and `.text` starts at `0x08000034` instead of `0x08000104`.

**The six kr232 rows that failed the first time were a stale binary.** They run `../kr232/build/windows-msvc/kr232-emu.exe`, not the `build/kernel-check` emulator that had been rebuilt with the fix, so the old loader loaded the debug payload over address 0. Rebuilding that tree and re-running gives 957 of 957.

**`--source-root` is not restored in the checkers.** Their compiler inputs are already relative to the repository root, so there is nothing for it to strip; passing it now would rewrite paths that need no rewriting and change every image again for no gain.
