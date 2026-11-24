# Platform matrix

What each row proves, where it runs, and what it costs. One copy for the family; the
repositories link here rather than restating it.

Durations are measured wall-clock on CROSBY (Windows 11, x64, 16 cores) and on BitWhiz
(Raspberry Pi, aarch64) with the family gate or the named gate, not estimated. A row
marked *not measured* has been run but its duration was not recorded, and saying so is
the point of the column.

## Rows

| # | Row | Command | Proves | Duration |
|---|---|---|---|---|
| 1 | Windows x64 debug | `../kr232/scripts/check-family.ps1` | every tree configures, builds and passes its suite on the host the change was written on; row counts match | 380 s for six repositories: compiler 291 s, kr16 41 s, kr216 33 s, kr232 5 s, krdbg 4 s, kr32 1 s |
| 2 | Windows x64 debug, single tree | `<repo>/scripts/gate-windows.ps1` | one tree in isolation, for the inner loop | 31 s kr216, 39 s kr16, 10 s kr232, 12 s kr32 |
| 3 | Windows x64 release | `cmake --preset windows-msvc-release` and `ctest` | optimised code paths, and that the compiler build type does not change results | not measured |
| 4 | Linux x64 debug | `bash <repo>/scripts/gate-linux.sh` from `~/mirrors/<repo>` | a second compiler (GCC), a second libc, and case-sensitive paths | 15 s kr232, 13 s kr16, 40 s kr32, 3 s krdbg headless |
| 5 | Linux x64 release | `cmake --preset linux-gcc-release` and `ctest` | the release configuration on a non-MSVC compiler | not measured |
| 6 | Linux ARM64 | `ssh bitwhiz` and the same Linux gate, or `check-family.ps1 -Arm64` | a different word size and byte order do not change the bytes an image is made of | kr232 15/15 rows, compiler build about 6 min on four cores |
| 7 | Sanitizers | `linux-sanitize` preset in WSL | no undefined behaviour or leak in the code a test exercises | not measured |
| 8 | Determinism across hosts | build the same image on Windows and BitWhiz, compare `sha256` | the artefact is a function of its sources, not of the host that built it | one kernel build per host, about 1 min each |

## Why these rows

Row 1 is the inner loop and the most informative single command: it runs every
repository's own gate in dependency order, so a break in a shared contract shows up in
the tree that consumes it rather than at the next milestone. Row 2 exists for the case
where the whole family is too slow to iterate on.

Rows 4 and 6 are not duplicates of row 1. The mount-versus-mirror rule, case sensitivity,
and the host path in an image's debug payload were all found on row 4 and none of them
can fail on Windows.

Row 8 is the only row that compares two machines rather than checking one. It is what
E3 of `../krcompiler/docs/build-cleanup-plan.md` required, and it is cheap: one image.

## What no row covers

- BitWhiz is not a hosted runner. A row that only passes because BitWhiz is reachable is
  untested when it is not, and the gate says which revision it ran.
- The sanitizer row has not been timed, so treating it as free is unmeasured optimism.
- Row 3 and row 5 have not been run since the rename; the release presets are in the
  table because they exist, not because a recent run is recorded.
