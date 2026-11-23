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
| M0 | complete — gate recorded 2026-11-22T20:07:37-08:00 | M0 |
| M1 | in progress — 5 of 11 tasks | — |
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

### 2026-11-22T20:06:19-08:00 — the BitWhiz alias now matches both spellings

Host: CROSBY and BitWhiz
Changed: `C:\Users\kevin\.ssh\config` line 1, `Host BitWhiz` → `Host bitwhiz BitWhiz`; nothing else in the file. It is outside every repository and outside git, so this log is its record.
Ran: `ssh bitwhiz "hostname; whoami; uname -m"` → `BitWhiz`, `kevinro`, `aarch64`; `ssh -G bitwhiz` → `user kevinro`, `hostname 192.168.10.170`.
Found: the earlier failure was pattern case-sensitivity, not a missing key — the key was always right, the block simply did not match the lowercase name.
Next: the M0 gate's merge clause, which is the owner's action.

## 14. Gate verification

### 2026-11-22T20:07:37-08:00 — M0 gate (Windows x64)

Build: `vcvarsall.bat x64`, then `cmake --build --preset windows-msvc-debug -j 10 --target krc`. The stamp is that build's start, printed before it ran. The `krc` binary was **not relinked** — nothing under `src/` changed, only documentation and a test registration — so its modification time could not identify this gate; the build's own start line is the identifier.
Tests: `ctest --preset windows-msvc-debug` → **919 passed, 1 failed of 920**, 268 s. The single failure is `krc_kr232_no_backend`, the test this milestone was required to add; M2's profile row turns it green without editing it.
Lint: `node tools/check-isa.mjs` → `ok: 81/81 vectors; Appendix B covers 0x00-0xFF; Appendix C tiles 4 GiB; 13 device page rows tile the window; tables well-formed`.
Merged: `krcompiler` `master` **7c31e3d**, a `--no-ff` merge of `kr232-m0` (`972294b`, `b16e3be`, `3db94f7`, `682f7ee`, `7952efb`).
Evidence: ADR **0057** and ADR **0058**; `docs/abi-kr232.md` (265 lines); **D-051** in `plan.md` Appendix A; target id **5** in `docs/symbols.md`, where KR216's 4 was added at the same time because it had never been recorded; and the refusal test's own output, `krc: error: unknown target 'kr232' (expected kr32, kr16, or kr64)`, against its expected regex.
ARM64: **not run** — M0 changes documentation and one test registration, and no `kr232` build exists to run there; the first ARM64 row is M1's.
Gate: plan §5 — both ADRs merged, the ABI document merged, id 5 recorded, and the failing test fails for the documented reason. **Met.**

*(M1's gate block follows when that milestone closes.)*
