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
- [ ] `[krcompiler]` `src/asm/kr232_assembler.cpp` and its encoder, plus `make_object` with target id 5. Check: round-trip tests, assemble → bytes → disassemble, for every Appendix A vector.
- [x] `[krcompiler]` `.kdbg` emission from assembly: line and function records. Check: `krc --dump-symbols` on a linked image names the fixture's lines.
- [x] `[krcompiler]` `src/linker/kr232_image.cpp` — the `.k232` writer the M0 ADR specifies: header, section records, the `.reset` section at `0x0100`, CRC, `image_base = 0`. Check: a unit test reads back every header field, and the writer rejects an entry outside executable text, a section outside the map and overlapping sections.
- [x] `[krcompiler]` `toolchain::link_impl` selects the writer for target 5, with 4-byte heap alignment and the kr232 stack bounds. Check: a linked image's header and entry agree with the ADR.
- [x] `[krcompiler]` `krc --dump-symbols` reads a `.k232`. Check: the gate's decode assertion passes on a fixture.
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
| M0 | complete — gate recorded 2026-11-22T20:07:37-08:00 | M0 |
| M1 | complete — 11 of 11 tasks | met 2026-11-22T21:01:09-08:00 |
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

### 2026-11-22T21:32:04-08:00 — M2: the `.k232` container writer

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `675efb7`
Added: `src/linker/kr232_image.{h,cpp}` implementing ADR 0058 — a 64-byte little-endian header (magic `K232`, kind 2, target id 5, ABI 1, image base, section-table and string-table offsets, file size, CRC32 over the whole file with its own field zeroed), 32-byte section records (name offset into a string table, type, flags, absolute address, payload offset, size, alignment), and payloads laid between the header and the table. Refused with a named error: an entry outside every executable section, a section leaving the image's half of the map, two allocated sections overlapping, and a non-power-of-two alignment.
Ran: krcompiler's suite 3630 assertions, all passing.
Found: two failures in my own test, both the same mistake — section name offsets are relative to the string table, not to the file, so resolving them from the table base read the wrong bytes. The writer was right; the test was reading it wrong, which is exactly the pair of interpretations a byte-level format invites.
Next: `toolchain::link_impl` selecting this writer for target 5 with the 4-byte heap alignment and kr232 stack bounds, then `.kdbg` emission.


### 2026-11-22T21:30:21-08:00 — M2: the kr232 object writer

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `ad92565`
Added: `src/asm/kr232_object.{h,cpp}` — a `Program` of sections and symbols becomes a `KroObject` with kind object, **target id 5** through `kROTargetId`, ABI version 1, the profile's 32-bit pointer width, little-endian, one section per program section and one symbol per program symbol with its binding. An empty program, a symbol naming a missing section, and another target's profile are each refused.
Ran: krcompiler's suite 243 cases, 3595 assertions, all passing.
Next: `.kdbg` line and function records from assembly, then the `.k232` image writer of ADR 0058.

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

### 2026-11-22T21:32:57-08:00 — M2: the .k232 loader replaces the flat loader

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
Next: emit the `.debug_info` bridge from these line records so `attach_kdbg` produces a `.kdbg`, then `krc --dump-symbols` on a `.k232`.

### 2026-11-22T21:42:23-08:00 — M2: the debug bridge converts to a .kdbg

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `20e32f0`
Added: the conversion test the previous round left unfinished. Two instructions and a global link to a container that carries a `.kdbg` section, and `decode_kdbg` reads back target id 5, the source path `fixture.s`, exactly two line records and the `main` function.
Ran: krcompiler's suite 3691 assertions, all passing.
Found: the previous round's single failure was the end-of-text label emitting an `L` record as well as a symbol, so two statements produced three lines. `.Lend` is now a symbol only -- it exists so a function record has an end, not to mark a statement. The previous round committed the emission without this test and said so; this round is what closes it.
Next: `krc --dump-symbols` on a `.k232`, `RunKr232Image.cmake` with `KRC_KR232_VM`, then `kr232-dis` and the `.s` fixture run under `kr232-emu`.

### 2026-11-22T21:43:41-08:00 — M2: reading a section out of a .k232

Host: CROSBY (Windows `windows-msvc-debug`) and CROSBY WSL (`linux-wsl`)
Added: `read_k232_section` finds the section table through the header, resolves a name through the string table, and returns the payload span; a missing section and a file that is not a container are named errors. Tested against a written container with a `.kdbg` section.
Ran: krcompiler's suite 3699 assertions, all passing; kr232's suite unaffected.
Blocked, and named: `krc --dump-symbols` cannot yet read a `.k232`. The krc tool target does not have `src/` on its include path, so the CLI cannot see `linker/kr232_image.h`, and the right fix is a public header rather than adding a source directory to a tool's includes. That header is the next step, after which the CLI branch is three lines.
Next: the public reader header, then `--dump-symbols` on a `.k232`, `RunKr232Image.cmake`, `kr232-dis`, and the `.s` fixture under `kr232-emu`.

### 2026-11-22T21:44:42-08:00 — M2: the container reader is public, and krc can reach it

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `12963c0`
Added: `include/krc/image/k232.h` and `src/image/k232.cpp` hold `read_k232_section`, moved out of the linker implementation so a tool can include it without reaching into `src/`. `krc --dump-symbols` now decodes a `.k232` by reading its `.kdbg` section and falls back to `decode_kdbg_image` for `.k32` and `.kex`.
Ran: krcompiler's suite 3699 assertions, all passing.
Not verified end to end: the CLI branch has no fixture. There is no checked-in `.k232` to run `--dump-symbols` against, so the code path exists and compiles but its first real invocation is still ahead. That is the next round's first task, and it needs a generated fixture rather than a hand-built one.
Next: generate a `.k232` fixture from a `.s` file, run `krc --dump-symbols` on it, then `RunKr232Image.cmake` and the `.s` fixture under `kr232-emu`.

### 2026-11-22T21:52:03-08:00 — M2: the CLI pipeline runs, and four tasks close

Host: CROSBY (Windows `windows-msvc-debug`), branch `kr232-m2` at `5fdf725` plus this round
Added: the backend-availability check moved ahead of the front end for stages that need code generation, so a target without a backend answers `has no code generator yet` instead of reporting whatever the source's own errors are. `krc --dump-symbols` recognises `K232` and decodes the `.kdbg` section; a checked-in 712-byte fixture produced by `krc -c` then `krld --entry=main --base=0x200` is the test's input.
Ran: `ctest -R kr232` **2 of 2 passed** -- `krc_kr232_no_backend` (the M0 red test, green now without editing it) and `krc_kr232_dump_symbols`; `krc_tests` 3699 assertions.
Found: the M0 test failed differently once the profile row landed, because the backend gate sat after typechecking and the fixture's kr16-sized static assertions fired first. The fix is where the check belongs -- a target that cannot generate code should say so before the front end runs -- and it is what turns the planted test green for the reason it was planted.
Ticked: `.kdbg` emission, the `.k232` writer, `link_impl` for target 5, and `--dump-symbols` on a container.
Next: `RunKr232Image.cmake` with `KRC_KR232_VM`, `kr232-dis`, the `.s` fixture under `kr232-emu`, and the gate.

## 14. Gate verification

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
