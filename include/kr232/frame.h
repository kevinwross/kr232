#pragma once

#include <kr232/devices.h>

#include <cstdint>
#include <string>

namespace kr232 {

// FNV-1a 64 over the committed frame's pixels in row-major order, `width * 4`
// bytes per row. The stride only decides where a row starts, so padding does not
// enter the hash: two frames with the same visible pixels hash the same whatever
// their stride (kr232-impl.md §9).
[[nodiscard]] std::uint64_t frame_hash(const Devices& devices);

// The hash as sixteen lowercase hex digits, fixed width so a comparison of two
// printed hashes is a string comparison.
[[nodiscard]] std::string frame_hash_text(std::uint64_t hash);

}  // namespace kr232
