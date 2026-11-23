# Example programs

Small kr232 programs to load in the debugger. Each is self-contained C: the
build chain compiles it with the kr232 toolchain and links only the runtime
start-up, so the source appears in the SOURCE panel after loading.

- `squares.c` -- sums the first ten squares; two functions, a loop, an integer return.
- `letters.c` -- fills a character array and measures it; a pointer parameter and a `while` loop.
- `counter.c` -- **does not load yet**: it includes `<kr232/cons.h>`, and the installed
  sysroot ships the package headers without a package library, so linking fails on
  `cons_puts`. Programs that use the packages need that library; see the note below.

Load one with:

```
krdbg_demo.exe --load examples\squares.c --target kr232 ^
               --toolchain C:\Users\kevin\repos\krcompiler\dist
```
