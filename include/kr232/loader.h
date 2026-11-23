#pragma once

#include <kr232/machine.h>

#include <cstdint>
#include <span>
#include <string>

namespace kr232 {

struct LoadOutcome {
    bool ok = false;
    std::string error;
    std::uint32_t entry = 0;
};

// Places a flat image at `base` and points PC at its entry. A kernel image at
// base 0 supplies its own entry in the reset word at 0x0100 (kr232-isa.md §12);
// any other base is its own entry. An image that does not fit is refused whole:
// the loader writes nothing it cannot then finish writing.
LoadOutcome load_flat(Machine& machine, std::span<const std::uint8_t> image, std::uint32_t base);

}  // namespace kr232
