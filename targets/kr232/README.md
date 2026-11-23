# Guest code for the emulated machine lives here and is compiled by `krc`, never
# by the host toolchain (kr232-impl.md §1):
#
#   targets/kr232/kernel/    the M5 kernel: vector table, trap handler, syscalls
#   targets/kr232/asm/       assembly fixtures a test assembles with krc
#
# kr232_guard_host_sources() in CMakeLists.txt fails the configure if any host
# target lists a file under this directory. probe.cpp is the canary the
# KR232_LAYOUT_SELFTEST configure adds on purpose, to prove that guard fires.
