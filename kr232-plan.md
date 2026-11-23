# kr232 — Plan

| | |
|---|---|
| Status | **Approved 0.18** — thirteen scope decisions accepted (eight in §2, five in §12), every section reviewed, and the cross-document check applied. Approved on 2026-11-22 as the implementation basis for M0–M3, with M4 and M5 approved from the same plan; all six milestones are complete, recorded in `kr232-impl.md` §12–§14 |
| Date | 2026-11-22T19:50:32-08:00 |
| Approved against | `kr232-isa.md` 0.4 and `kr232-devices.md` 0.4, 2026-11-22 |
| Basis | `kr232-isa.md` 0.4, `kr232-devices.md` 0.4, `..\krcompiler\new-target-guide.md` (243 lines, revision `73611af`), `..\krdbg\krdbg-newtarget.md` (622 lines) |
| Authoritative for | milestone scope, order, and exit criteria for kr232 1.x |
| Not authoritative for | instruction encodings (`kr232-isa.md`), device registers (`kr232-devices.md`), the ABI (`..\krcompiler\docs\abi-kr232.md`), debugger behaviour (`..\krdbg`) |
| Open | §2 records the accepted scope decisions; §12 carries the two questions deferred to 2.0; no section is unreviewed |

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-11-22 | First draft: seven scope decisions, five milestones across three repositories, gates and open-question schedule. |
| 0.2 | 2026-11-22 | The seven scope decisions in §2 accepted by the owner; `char_unsigned` added to the profile row, M5 fixed as post-1.x, the by-value struct path added to the M3 gate, and a size-exact copy risk recorded. |
| 0.3 | 2026-11-22 | §12 resolved: five decisions recorded in ISA §15.55–§15.59 and nine questions closed on the drafted behaviour, leaving the two M5 syscall-table questions. |
| 0.4 | 2026-11-22 | §3 reviewed: the identifier rows corrected to one KRO/KDBG counter — KR216 holds 4, so kr232 takes **5** in both — with §2.6, the §4 and §5 rows, the M0 bullets and the §13 risk following. |
| 0.5 | 2026-11-22 | §4 reviewed: M0–M4 marked as 1.x; M4 sequenced after M2, since its fixtures and golden are container images; M0's failing test and M3's ISA/device freeze restored to the gate column; the assembler's `.kdbg` output named as M4's dependency. |
| 0.6 | 2026-11-22 | §5 reviewed: `sret_register = 6` recorded for memory-class returns, the ABI document's missing sections named (data model, frame layout, floating-point convention), and the M0 failing test restated so it fails on `unknown target` until M2's profile row lands. |
| 0.7 | 2026-11-22 | §6 reviewed: CLI aligned with the family runner (`vm`, `--max-steps`, plus `--frame-hash`), M1's flat image format named, the unbacked-gap traps added to the protection gate, and the out-of-scope list narrowed to 64-bit integer arithmetic. |
| 0.8 | 2026-11-22 | §7 reviewed: the container writer, the runner CMake file and the `KRC_KR232_VM` cache moved to krcompiler, the `.k232` loader named as kr232's M2 work, a second M0 ADR fixing the container layout, the `.kdbg` decode added to the M2 gate, and `has_matrix` added to the profile row. |
| 0.9 | 2026-11-22 | §8 reviewed: virtual dispatch accepted with RTTI off (§2.8), the reserved typeinfo word added to the ABI and the risks, `crt0`'s stack setup corrected against the ISA's reset SP, the `_o1` twins made conditional on §2.7's measurement, the three-`char` record added to the M3 gate, and the guide's step 3 sysroot named. |
| 0.10 | 2026-11-22 | §9 reviewed: `K` and `I` made non-writable in the flag panel, the variable-length layout fields named, the two unbacked gaps given regions, the demo and `load_any` wiring added as deliverables, and the gate extended to the disassembly step and the host load paths. |
| 0.11 | 2026-11-22 | §10 reviewed: the user program delivered as a second image on the CLI (`--user`), the krdbg OS-pair wiring named, an M5 exit gate added, and the syscall table's numbers identified as the kernel's own convention. |
| 0.12 | 2026-11-22 | §11 reviewed: the layer table extended to the six checks the gates already require (contracts, protection and traps, loader rejection, assembler round-trip, `-S` byte-identity, host gate), and its last column changed to repository names. |
| 0.13 | 2026-11-22 | §12 reviewed: the closed count corrected to fourteen with the debug item split, and a sentence recording that the two M5 questions do not hold the freeze. |
| 0.14 | 2026-11-22 | §13 reviewed: the first risk renamed to the `vm` entry point, and a risk recording that `has_rtti` is documentation — a `dynamic_cast` fails at link, not at compile time. |
| 0.15 | 2026-11-22 | §14 reviewed: the freeze now retitles each draft's open-questions section as deferred to 2.0, a drawing program joins M3's gate so §14's frame promise has a producer, the compiled program is checked in krdbg at M3, and the gate reference points at §4. |
| 0.16 | 2026-11-22 | Cross-document check: the four guest packages §11 commits to join M3, M5's user image is loaded by the emulator and entered at `0x08000000` with no entry word, and §3's device-map row names the ISA sections it means. |
| 0.17 | 2026-11-22 | Approved as the implementation basis for M0–M3, against `kr232-isa.md` 0.4 and `kr232-devices.md` 0.4; the Basis and Open rows follow. |
| 0.18 | 2026-11-22 | M0's identifier split corrected: ADR 0057 and `docs/symbols.md` record id 5, and the `kROTargetId` case moves to M2 with the profile row, because the constant cannot precede the enumerator. |

## 1. What 1.x is

1.x is four deliverables, in this order:

1. **`kr232-emu`** — the virtual machine: decoder, executor, protection, traps, all eight device pages, image loader.
2. **Image tooling** — assembler, encoder, object writer, `kr232-dis`, and the `.k232` container.
3. **A krcompiler backend** — profile, descriptor, sysroot bits, code generator, `crt0` and a libc subset, so C and C++ programs can be built.
4. **A krdbg adapter** — `IDebugTarget` plus KDBG translation, assembly level first and source level second.

The boundary that keeps 1.x small: **no ISA extensions**. The drafted instruction set is what 1.x implements (`kr232-isa.md` §15.53); 1.x runs programs in kernel mode, with the kernel that would make user mode useful in M5, after 1.x (§2). User mode itself is implemented and tested in M1; nothing in 1.x depends on a kernel existing.

Not in 1.x: interrupts, paging, 64-bit integers, bit operations, indexed addressing, run-time type information, an operating system, a userland, storage or networking.

## 2. Scope decisions (accepted)

The owner accepted all eight on 2026-11-22. Each row states the decision and what the rejected answer would have cost.

| # | Decision | Accepted choice | Rejected alternative |
|---|---|---|---|
| 2.1 | Do 1.x programs run in kernel mode, or does 1.x ship a kernel? | Kernel mode; the kernel is M5, after 1.x | A kernel in 1.x adds a milestone and a syscall ABI before either is needed |
| 2.2 | Aggregate passing | `MemoryOnly`: every by-value aggregate is memory class | `RegistersOrMemory` adds the classification and register-marshalling path KR32 carries, and more cases to freeze in the ABI |
| 2.3 | 64-bit integer types | Absent (`long_long_bits = 0`, `intmax_bits = 32`) | Synthesised `ADDC` pairs or helper calls, as KR16 does for 64-bit; a compiler-only change, no ISA change |
| 2.4 | `char` signedness | Signed (`char_unsigned` false) | Unsigned changes every `char` load the backend picks |
| 2.5 | `long double` | 64 bits, the same as `double` | `0` removes the type; both are legal C |
| 2.6 | Container and identifiers | `.k232` container; **one** target id, `5`, carried by both KRO and KDBG | Reusing 4 collides with KR216's shipped id and writes the wrong target into every kr232 object and `.kdbg` |
| 2.7 | Optimisation level shipped | `-O0`; `-O1` is measured, not assumed (ADR 0049 precedent) | `-O1` in 1.x adds the register-allocator work KR32 carries and KR16 declined |
| 2.8 | C++ polymorphism and RTTI | Virtual dispatch in; `has_rtti` false, the typeinfo word reserved, no `__krc_dynamic_cast` | Without the reserved word, adding RTTI later moves every virtual slot; shipping `dynamic_cast` needs a runtime helper no 1.x program calls |

## 3. Contracts this plan cannot move

| Contract | Owned by | How it changes |
|---|---|---|
| Instruction encodings and trap rules | `kr232-isa.md` | §15 decision plus §17 row; a new revision |
| Device registers and page map | `kr232-devices.md` | Device revision; the ISA's §2.3 and §14 follow this draft |
| Target profile, descriptor schema, object encoding, ABI | `..\krcompiler` | ADR under `docs/decisions/`, an Appendix A entry in its `plan.md` if a `D-xxx` is affected, and `docs/abi-kr232.md` — **before any code** |
| `IDebugTarget` and `DebugInfo` | `..\krdbg` | Adapter-local; no VM type and no target conditional may enter `include/krdbg`, `src/ui`, `src/core` or `src/kdbg` |
| Identifiers | `..\krcompiler` | **One** counter, not two: KDBG's `target_id` is the KRO target id, assigned by `kROTargetId` in `src/linker/kro_object.cpp` and copied into the `.kdbg` header. 1 KR32, 2 KR16, 3 KR64 and 4 KR216 are taken (ADR 0053); kr232 takes **5**, and `docs/symbols.md` lists it |

## 4. Milestones at a glance

| M | Deliverable | Repositories | Exit gate |
|---|---|---|---|
| M0 | Contracts and identifiers | kr232, krcompiler | ADRs reviewed and merged; `docs/abi-kr232.md` merged; id `5` recorded; the failing test fails for its documented reason |
| M1 | `kr232-emu` core and device model | kr232 | 81 vectors execute; protection and device suites pass; terminal frame hashes to a golden |
| M2 | Profile, descriptor, assembler, object writer, disassembler, container | krcompiler, kr232 | Hand-written assembly links and runs under M1; every vector round-trips |
| M3 | Code generator, runtime, sysroot | krcompiler | A C, a C++ and a drawing program build, run and match their status, output and frame; `-S` output is byte-identical; the compiled program stops in krdbg; determinism and KDBG invariants pass; **ISA 1.0 and devices 1.0 freeze** |
| M4 | krdbg adapter | krdbg | Adapter tests, golden, boundary check and host gate pass |
| M5 | Kernel and user mode | kr232 | A user program runs under protection and is debuggable (after 1.x) |

M0–M4 are 1.x; M5 lands after it. M1 must land before M2, because krcompiler's executed tests need a VM (`new-target-guide.md` §6). M2 before M3, because a backend without an assembler emits text nobody can run. M4 starts at M1, at assembly level, and completes after M2, before M3: its fixture, its no-debug-info case and its golden are all container images, which only M2's assembler and writer produce.

## 5. M0 — contracts and identifiers (documentation only)

The compiler guide is explicit: the first commit for a new target is documentation and a failing test, not a backend (§0).

**krcompiler**

- An ADR under `docs/decisions/` stating whether kr232 fits the frozen descriptor schema (ADR 0002) or changes it, which aggregate rules it takes, and that `sret_register` is **6** — the field schema v2 already carries, so no schema change.
- A second ADR for the `.k232` container, as ADR 0054 fixed `K216`: header fields, section records, the reset word at `0x0100`, the `.kdbg` payload's `image_base = 0` and build id, and every condition the loader rejects.
- `plan.md` Appendix A entry if any `D-xxx` decision is affected.
- `docs/abi-kr232.md`: the data model (`Ilp32` — 32-bit pointers, `size_t` and `ptrdiff_t` 32, `intmax_t` 32, no 64-bit integer type, `long double` 64, `char` signed), argument registers R2–R5, result in R2, `this` in R10, frame pointer R13, stack pointer R15, callee-saved R8–R9, the frame layout (saved caller FP at `[FP]`, the EFP chain in R12 that `unwind()` follows), 4-byte stack alignment with 8 recommended (`kr232-isa.md` §15.38, §15.51), narrow-type extension for `char`/`short` arguments, varargs, aggregate parameters and the struct return under `MemoryOnly` with the destination in **R6**, the floating-point convention — how F0–F3 carry arguments and how a binary64 result returns — function pointers and virtual calls, the vtable and typeinfo layout (the typeinfo word reserved at `[vptr - 4]`, `_ZTI` emitted, `dynamic_cast` and `__krc_dynamic_cast` absent), and FP register preservation.
- The `docs/symbols.md` line that lists the KDBG `target_id` extended to `5` kr232. The `kROTargetId` case returns **5** with the profile row in M2, because the function switches on `TargetId` and the constant cannot precede the enumerator (ADR 0057).
- A failing test that names `kr232`: it expects `krc --target=kr232 -c` to stop with "has no code generator yet", fails at M0 with `unknown target`, turns green when M2's profile row lands, and is replaced by a compile test at M3.

**krdbg**

- Nothing in M0. `src/kdbg/kdbg.cpp` copies `target_id` from the `.kdbg` payload and no table in krdbg names a target, so the id row lives only in krcompiler; the adapter, region map and golden arrive in M4.

**kr232**

- This plan approved, and `AGENTS.local.md` updated to point at it.

**Exit gate.** Both ADRs are reviewed and merged; the ABI document is merged; the id `5` is recorded in ADR 0057 and in `docs/symbols.md`, with the `kROTargetId` case following at M2; the failing test exists and fails for the documented reason.

## 6. M1 — `kr232-emu`: core and device model

The decided first milestone (`AGENTS.local.md` §Plan). It makes the ISA executable and turns Appendix A from a lint fixture into a test suite.

**Deliverables**

- `include/kr232/` and `src/` layout: `decoder`, `executor`, `machine`, `memory`, `images`, `devices/`.
- Every instruction class of `kr232-isa.md` §3.1, including `F0`/`F4` lengths resolved from the bytes after the prefix.
- Reset state, the reset word at `0x0100`, the sixteen vectors, the 16-byte trap frame, protection (vector 8), `SYSCALL` (vector 9), kernel-only instructions, the `K` and `I` write rules.
- The eight device pages: timer, console, framebuffer, keyboard, terminal, graphics commands, system, blitter.
- A headless CLI whose shape is fixed by the runner that will launch it: `kr232-emu vm <image> --max-steps <n>`, with `--trace`, `--screenshot <path>` and `--frame-hash` as additions, and `--input <text>`, which queues console input before the run so a program that reads can be tested without a terminal. The program's `SYS exit` status is the process status; on a POSIX host only its low eight bits survive as the process status, and the `halted status N` line it prints carries the whole value. It MUST be usable as `KRC_KR232_VM` by krcompiler's `RunKr232Image.cmake` (M2 adds that file; M1 only has to be invocable), which is why the subcommand is `vm` and the limit is `--max-steps`, as the KR32 rule passes them.
- A flat image format for M1: the file's bytes load at `0x00000000`, and the word at `0x0100` is the entry address. M2's `.k232` container supersedes it, and the loader dispatches on the container magic.

**Exit gate**

1. Each of the 81 Appendix A vectors executes from its bytes and produces the documented architectural effect.
2. A user-mode access below `0x08000000` or at or above `0xFC000000` raises vector 8 with a 16-byte frame; an access to the unbacked `0x10000000`–`0xFBFFFFFF` gap raises vector 4 in either mode, while the `0xFE000000`–`0xFFEFFFFF` gap raises vector 4 in kernel mode and vector 8 in user mode, because protection is checked first; `IRET` restores K, I and SP; `SYSCALL` resumes after the instruction; `EI`, `DI`, `HALT`, `SYS` and `IRET` trap in user mode.
3. Every populated device page answers `ID` at `0x3C`; an undefined offset and a reserved page trap with vector 4; a rejected FLIP sets `FLIP_ERROR` and commits nothing; blitter `COPY`/`FILL`/`UPLOAD` validate ranges before moving bytes.
4. A hand-encoded kernel-mode image writes to the terminal, draws through the graphics page and flips; `--frame-hash` over the committed surface matches a checked-in golden.
5. `node tools/check-isa.mjs` still passes.

**Out of scope.** Assembly text, the linker, the compiler, the debugger, an OS, interrupts, paging, 64-bit integer arithmetic.

## 7. M2 — profile, assembler, object writer, disassembler, container

This is the guide's steps 2–4: the target becomes assemblable and linkable, but not yet compilable.

**krcompiler**

- `TargetId::Kr232`, a `TargetProfile` row (`Ilp32`, little-endian, `char_bits` 8, `char_unsigned` false, `short_bits` 16, `int_bits` 32, `long_bits` 32, `long_long_bits` 0, `long_double_bits` 64, `has_fpu` true, `has_matrix`/`has_atomics`/`has_threads`/`has_tls`/`has_eh`/`has_rtti` false, `has_cpp` true, `aggregate_rules` `MemoryOnly`, `backend_available` false), `to_string`, `parse_target`, and the `kROTargetId` case returning 5.
- `Kr232Descriptor` with the register roles of M0.
- `src/asm/kr232_assembler.cpp` and its encoder, plus `make_object`, taking target id 5.
- Round-trip unit tests: assemble → bytes → disassemble for every Appendix A vector.
- The assembler emits `.kdbg` line and function records, so an assembly-only image carries debug information before M3's compiler exists — M4's source-level fixture depends on it.
- `src/linker/kr232_image.cpp`: the `.k232` writer the second ADR specifies — header, section records, the `.reset` section at `0x0100`, CRC — selected by `toolchain::link_impl` for target 5, with kr232's 4-byte heap alignment and stack bounds.
- `krc --dump-symbols` reads a `.k232`, so the gate checks the payload without krdbg.
- `cmake/RunKr232Image.cmake` and the `KRC_KR232_VM` cache variable in `CMakeLists.txt` with the sibling fallback, cloned from the KR32 rule.

**kr232**

- `kr232-dis`: the canonical decoder and text form, shared with the emulator so the two cannot drift.
- The `.k232` loader replaces M1's flat loader and dispatches on the container magic; it rejects a checksum failure, a section outside the map and a base mismatch, so a bad image fails at load rather than during execution.

**Exit gate.** `krc --list-targets` shows `kr232` with `backend=no`; a hand-written `.s` file assembles to an object, links to a `.k232`, and runs under M1 with the expected exit status and screen; the image's `.kdbg` decodes through `krc --dump-symbols` with the fixture's lines and functions and `image_base = 0`; every vector round-trips byte-exactly; two builds of one image are byte-identical.

## 8. M3 — code generator, runtime, sysroot

The guide's steps 5–9 plus the sysroot bits of step 3, and the long pole: `kr32_backend.cpp` is 2,555 lines and `kr16_backend.cpp` 1,306, and kr232 sits nearer KR16 — no 64-bit arithmetic, no matrix, no register-allocator path — plus the FPU.

**Deliverables**

- The backend in the shape the guide fixes: `Kr232Backend(module, profile, level)`, `emit`, `debug_info`, `frame_bytes`, registered at both dispatch sites in `src/toolchain/toolchain.cpp` and the six branches in `src/tools/krc/main.cpp`.
- Prologue/epilogue with `ENTER`/`LEAVE`, load/store (including `LDBU`/`LDHS` and the FP forms), ALU and branches, calls and returns — `-O0` code for the kernel-mode ABI.
- `targets/kr232/runtime/`: `crt0.s` establishing the frame and calling `main` — the reset SP is already `0x08000000` (`kr232-isa.md` §15.22), so there is no stack to set — `libc.c` with the console and the blitter-backed `memcpy`/`memset`, and the `SYS`-backed write/read/exit stubs.
- `sysroot/kr232/include/bits/` generated from the profile (`bits/types.h`, `bits/stdint.h`).
- The four guest packages `kr232-devices.md` §11 commits to: `<kr232/fb.h>`, `<kr232/gfx.h>`, `<kr232/cons.h>` and `<kr232/input.h>` under `sysroot/kr232/include/kr232/`, with their kernel-mode implementations in `targets/kr232/runtime/`. The user-mode column of §11.1's table lands with M5's kernel; the API and its arguments freeze here.
- Per-target link/run loops in `tests/CMakeLists.txt` at `-O0`, a determinism test, and a KDBG-invariants test at `-O0` modeled on the existing `krc_debug_invariants_o1`; the `_o1` twins arrive only if §2.7's measurement makes `-O1` shippable.

**Exit gate.** `int main(void){return 42;}`, a C++ program with a class, a virtual call, a by-value struct parameter, a struct return and an array, and a program that draws through `<kr232/gfx.h>` and flips, all build with `-O0`, run under `kr232-emu`, and match their status, terminal output and frame; the by-value and returned records include a three-`char` one, whose copy must be size-exact; `-S` output is byte-identical at every conversion step; the executed rows exist because `KRC_KR232_VM` is found; the compiled program loads in krdbg at source level and stops on a line breakpoint, which is the only check that pairs the compiler's `.kdbg` with the adapter; determinism, KDBG invariants and the clean gate pass. **ISA 1.0 and devices 1.0 freeze at this gate**, once real code has exercised every encoding and device the runtime touches.

## 9. M4 — krdbg adapter

Assembly level first (`krdbg-newtarget.md` §1), then source level.

**Deliverables**

- `adapters/kr232/` with the `IDebugTarget` implementation: `layout()` before load and after reset, one core context, PC/SP/FP/EFP roles, `read_value`/`write_value` by group index, flags at the ISA bit positions (Z=0, N=1, C=2, V=3, I=4, T=5, K=6) with **K and I non-writable** — `write_flags` preserves them, because only trap entry and `IRET` may change them (`kr232-isa.md` §15.29) — `disassemble` through `kr232-dis`, `unwind()` through the EFP chain, `reset()`, `step_instruction()` returning `Halted`/`Fault` truthfully.
- The layout fields the interface defaults get wrong for this ISA: `variable_instruction_length` true with `instruction_alignment` 1, since instructions are 1–8 bytes; 32 address and pointer bits; `step_unit` "instructions". Memory regions cover kernel RAM, user RAM, the aperture and the device window, plus both unbacked gaps marked neither readable nor writable, so the panel can report "not mapped".
- Host wiring (`krdbg-newtarget.md` §6.3): `--kr232` in `krdbg_demo`, the adapter include under `KRDBG_HAVE_KR232`, `load_any` dispatching on the `.k232` extension and on `--target kr232`, and `tests/test_host_build.cpp` with the `KRDBG_KRC_PREFIX` checks.
- `IComPort` over the console page (page 2) and `IVideoSource` over the committed frame; `layout.has_com_port` and `has_framebuffer` agree with the accessors.
- Source level: load a `.k232`, locate its `.kdbg`, call `kdbg::read_payload`, resolve source paths, and set `DebugInfo`. `FunctionInfo::frame` is 0 — the saved caller FP is at `[FP]` — and the adapter **negates** `Location::FrameOffset`, because locals sit below the frame base.
- `KRDBG_KR232_ADAPTER` option, `tests/test_kr232_adapter.cpp`, `tests/fixtures/kr232/`, and `goldens/kr232-loaded.hash`. The fixtures are assembled by M2's toolchain and committed with a `regenerate.ps1` that records the command, as KR32 and KR16 do, so nothing in them depends on a compiler install.

**Exit gate.** The adapter compiles with its option ON; the fixture tests cover load, layout, execution, inspection, line stepping, reload, no-debug-info, bad input, COM and framebuffer; the disassembly view advances by decoded length, so a one-byte `RET` is followed by the next instruction; `write_flags` cannot clear `K`; the demo loads the fixture by `.k232` extension and by `--kr232`; the golden matches; `krdbg_platform_boundary` reports clean; the host gate passes on the changed rows. The adapter build is reported separately, because the repository gate builds adapter-OFF.

## 10. M5 — kernel and user mode (after 1.x)

- A minimal kernel: vector table setup, a trap handler that resumes `SYSCALL`, the syscall table (`write`, `read`, `exit`, blitter copy/fill, graphics commands) with kernel-side validation, entry into the user image and `IRET`. The call numbers are the kernel's own convention — `SYS`'s 0–4 are the hypercall's, and `SYSCALL` leaves the number in R7 to the kernel (`kr232-isa.md` §4, §13) — so this table costs no ISA change.
- The user program travels as a second image: `kr232-emu vm <kernel.k232> --user <program.k232>`, both preloaded before reset. The emulator loads and the kernel only enters, because the ISA leaves loading undefined (`kr232-isa.md` §12): the kernel builds a frame with PC `0x08000000` — the image's link address and its first instruction — a user SP and K = 0, then executes `IRET`. A kernel started without `--user` runs as an ordinary 1.x program.
- A user-mode program built by krc that prints, draws and exits; the protection tests rerun from user mode; krdbg debugs a user program through the kernel by setting `host.kernel_supported` and implementing `load_kernel` to replace the session target with the kernel image (`krdbg-newtarget.md` §6.3).
- The two questions §12 leaves open — `SYSCALL` error values and the display call numbers — are settled here, with the syscall table.

**Exit gate.** A kernel loads a user image with `--user`, enters it through a constructed trap frame with `IRET` and K = 0, and the program writes to the console, draws and exits with a status the kernel returns through `SYS exit`; a user-mode access below `0x08000000` and a kernel-only instruction both reach the kernel's handler; the same program is stepped in krdbg at source level with its registers and stack visible; the two §12 questions are answered in the ISA and devices drafts.

## 11. Testing and gates

| Layer | What runs | Repository |
|---|---|---|
| Spec | `node tools/check-isa.mjs` after every document edit | kr232 |
| Contracts | the M0 refusal test: `krc --target=kr232 -c` stops with "has no code generator yet" | krcompiler |
| Vector execution | in `kr232-emu`, one test per Appendix A vector: bytes in, architectural state out | kr232 |
| Protection and traps | in `kr232-emu`: user-mode faults, the 16-byte frame, `IRET`'s restore, the kernel-only instructions, both unbacked gaps | kr232 |
| Devices | in `kr232-emu`: per-page `ID`, undefined offsets, FLIP bundles, blitter ranges | kr232 |
| Images | `kr232-emu` with the toolchain: assembled and compiled images run, with exit status, terminal transcript and frame hash; the loader rejects a bad checksum, a section outside the map and a base mismatch | kr232, krcompiler |
| Assembler and container | assemble → bytes → disassemble for every vector; the `.kdbg` decodes; two builds of one image are byte-identical | krcompiler |
| Compiler | the per-target link/run loops, determinism, `-S` byte-identical output at every step, KDBG invariants, clean gate | krcompiler |
| Debugger | adapter tests, golden surface hash, boundary check, host gate | krdbg |

A milestone is done when its exit gate runs, not when its code compiles. A check that was not run is reported as not run.

## 12. Open questions from the drafts

No rows remain. Both were settled at M5:

| Question | Settled in |
|---|---|
| ISA §18.1 `SYSCALL` error values | `kr232-isa.md` §18: 0 success, -1 refused argument, -2 unknown call |
| Devices §14.1 display call numbers and errors | `kr232-devices.md` §14: 16-24, with the graphics `STATUS` word carrying a device refusal |

The other fourteen are closed for 1.x:

- **Recorded as decisions** in `kr232-isa.md` §15.55–§15.59: the terminal mirrors to the console page (a development aid that retires when the terminal and the COM port separate), the kernel owns VRAM allocation, there are no escape sequences, the blitter is synchronous, and there is no pointer device.
- **Closed on the drafted behaviour**, which 1.x keeps: immediate-register commands instead of a FIFO, the 10x20 font only, one committed surface per flip, no direct user VRAM without paging, no guest font, the `T`/`RDCYCLE`/cycle debug surface, no debug port, the `F0` generic form kept, and the fixed `0x08000000` boundary and kernel trap stack.

Neither question held the freeze, and both answers are in the drafts: §18 of the ISA and §14 of the device document. A program that needs them is a program running under the kernel, which is what M5 built.

## 13. Risks and traps

- **The VM gates the compiler tests.** Without `KRC_KR232_VM`, krcompiler's executed rows silently do not register (`new-target-guide.md` §6). M1 ships the `vm` entry point before M2 names it.
- **Memory-class copies must be size-exact.** ADR 0046 records that the existing copy of a memory-class argument is one whole-word load and store, which is wrong for a record whose size is not a multiple of the word; `MemoryOnly` makes that the central path. A three-`char` record passed by value is the test that pins it.
- **The sret register is loaded last.** R6 carries the aggregate-return destination, so the caller must load it after argument evaluation — an argument that itself returns an aggregate overwrites it. A nested call in an argument is the test that pins the order.
- **The typeinfo word is reserved while RTTI is off.** The vtable keeps its `[vptr - 4]` slot from the start; emitting vtables without it would move every virtual slot the day `dynamic_cast` arrives, invalidating existing images and their goldens.
- **`has_rtti` is documentation.** Nothing in the compiler reads the flag, so a `dynamic_cast` in a kr232 program compiles and then fails at link with an undefined `__krc_dynamic_cast` — not with a diagnostic. The M3 fixture must not use it; rejecting it earlier is a krcompiler front-end change, not a profile edit.
- **Backend size.** Expect 1,300–2,600 lines and rounds, not hours. A byte-identical `-S` diff at every step is the only honest guard; the two failed optimisation attempts in `..\krcompiler\implementation.md` show what happens without one.
- **One id counter.** KRO and KDBG share a single target id: `kROTargetId` assigns it and the `.kdbg` header copies it. KR216 holds 4, so kr232 is 5 in both; an id that reuses 4 makes every kr232 object and payload claim to be a KR216 image.
- **The container crosses two repositories.** The writer lands in krcompiler and the loader in kr232-emu, so the M0 ADR is the only thing keeping the two readings equal; a field added on one side without the ADR is a load-time failure in the other.
- **ISA freeze timing.** Freezing at M3 exit rather than M2 leaves room for a gap the backend finds; any change before the freeze costs a §15 item and a §17 row.
- **Adapter frame offsets.** KDBG's offset is non-negative and `Location::FrameOffset` is added to `frame_base`; kr232 locals are below FP, so the adapter negates. Reading the wrong slot shows a local as zero or as another local's value.
- **KDBG translation limits.** The current path does not apply `Location` records, so register-held variables will not expand; frame offsets are the only locations until the decoder is re-vendored.
- **Stale debug information.** Reload must call `replace_target(std::move(target), info)`; the one-argument form keeps the previous image's lines.
- **Boundary check.** No VM header may reach `include/krdbg`, `src/ui`, `src/core` or `src/kdbg`; a target conditional in the UI means the layout is missing a field.
- **Narrow-integer literal defect.** The compiler's known `int`-narrower-than-32 literal bug (`new-target-guide.md` §1) does **not** apply to kr232, whose `int` is 32 bits — but the reasoning is recorded so a later `int_bits = 16` variant does not rediscover it.
- **Golden images are x64-only.** The ARM64 run excludes golden tests; a green ARM64 row says nothing about the surface hash.

- **Stale generated artefacts.** A build tree can report success for a command it generated before the source changed: in one session the emulator binary, a generated test list, an object file and a whole ctest command each described an older tree. `ctest -V` and a fresh configure are the cure, and a check that has just started failing is the moment to look.
- **Host-dependent bytes.** Images were not reproducible: the per-file label hash covered the source text including carriage returns, and floating-point constants were labelled with `std::hash<double>`. Both used platform-defined inputs and both are fixed -- the trap is that any new hash of a platform type reintroduces it, and only a rebuild on a second host shows it.
- **A check that cannot fail.** A golden recorded from a broken binary, a test whose condition is vacuous, a list that silently drops an empty field: the suite is green over something that does nothing. Three of this project's defects were found only after asking what would make each check fail.

## 14. What done means for 1.x

- ISA 1.0 and devices 1.0 frozen, with the spec lint green. The freeze revision retitles each draft's open-questions section — `kr232-isa.md` §18 and `kr232-devices.md` §14 — to "Deferred to 2.0", still listing the two M5 items and why a kernel settles them, so a frozen 1.0 is a closed set with two named deferrals rather than a live question list.
- A C program, a C++ program and a drawing program built by `krc --target=kr232 -O0` run under `kr232-emu` with the expected status, terminal output and frame.
- The same programs load in krdbg at source level, with line breakpoints, locals, a call stack and the framebuffer panel — including the compiler's output, which M3's gate checks rather than leaving to M4, since M4 precedes it.
- Every gate in §4's table and every milestone's exit gate has been run and reported; a skipped gate is named as skipped.
