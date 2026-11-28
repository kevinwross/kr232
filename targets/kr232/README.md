# Guest code for the emulated machine lives here and is compiled by `krc`, never
# by the host toolchain (kr232-impl.md §1):
#
#   targets/kr232/kernel/    the M5 kernel: vector table, trap handler, syscalls
#
# Assembly fixtures live with their sources in tests/fixtures/ and are built by
# cmake/BuildKr232Fixtures.cmake. probe.cpp is the canary the
# KR232_LAYOUT_SELFTEST configure adds to a host target on purpose, to prove that
# kr232_guard_host_sources() in CMakeLists.txt fires.
