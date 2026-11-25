# Example programs

Small kr232 programs to load in the debugger. The C files are self-contained:
the debugger's build chain compiles one with the kr232 toolchain, links the
start-up and the package objects in the sysroot's `kr232/lib`, and stops at the
first line of `main`, so the source appears in the SOURCE panel.

- `squares.c` -- sums the first ten squares; two functions, a loop, an integer return.
- `letters.c` -- fills a character array and measures it; a pointer parameter and a `while` loop.
- `counter.c` -- sums 1 to 5 and calls `cons_puts` from `<kr232/cons.h>`. It links
  now that `kr232/lib` ships `cons_user.kro`, but the `syscall` behind `cons_puts`
  needs a kernel: the debugger loads it and stops at `main`, and running past that
  traps.
- `balanced-tree.cpp` -- an AVL tree in C++: virtual dispatch, recursion, allocation
  and `printf`. It needs the full C runtime, so it is built by hand and loaded as an
  image rather than through the debugger's source chain.

`squares.c` and `letters.c` load and run from source. `balanced-tree.cpp` does not
build through the debugger's chain because that chain links only the start-up and
the package objects, not `libc`. Build it against the compiler's
`targets/kr232/runtime/libc_full.c` and `fileio_full.s` (the recipe is
`cmake/BuildAndRunKr232.cmake`) and load the `.kr232` image.

## Load one

`krdbg_demo.exe` must come from a build configured with
`-DKRDBG_KR232_ADAPTER=ON`; the default configure leaves the kr232 adapter out.
`--toolchain` names the installed `krc` prefix -- the directory holding
`bin\krc.exe`, `bin\krld.exe` and `share\krc\sysroot`. From the repository root,
with the debugger on `PATH`:

```
krdbg_demo.exe --load examples\squares.c --target kr232 --toolchain ..\krcompiler\dist\krc-0.2.0-windows-x64
```

`--load` also opens an image directly (`build\krdbg-host\squares.kr232` after a
source load), `--reset` rebuilds a source-loaded program, and `--headless` runs
without a window. On Linux, use `krdbg_demo`, `/` separators, and the matching
`krc-0.2.0-<platform>` prefix.
