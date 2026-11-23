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

// The `.k232` container of ADR 0058: a 64-byte header, section payloads, section
// records and a string table. Everything is validated before a byte is written --
// magic, container version, kind, header size, target id, ABI version, file size,
// CRC32, each section's address and payload, and the entry. The header's entry is
// authoritative: for a kernel image it is written into the reset word at 0x0100.
LoadOutcome load_k232(Machine& machine, std::span<const std::uint8_t> image);

// Dispatches on the container magic: `K232` goes to load_k232, anything else to
// the flat loader at `flat_base`.
LoadOutcome load_image(Machine& machine, std::span<const std::uint8_t> image,
                       std::uint32_t flat_base = 0);

}  // namespace kr232
