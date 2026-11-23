// The canary for the layout guard. This file is guest-side by location and is
// never compiled: a configure with -DKR232_LAYOUT_SELFTEST=ON adds it to a host
// target on purpose, and kr232_guard_host_sources() must then fail with a
// message naming targets/ (kr232-impl.md §1).
int kr232_layout_probe() { return 0; }
