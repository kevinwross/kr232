#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace kr232 {

// The eight rows of ISA §2.3. Classification is by address alone and does not
// depend on how much RAM this build happens to allocate, because the executor
// raises its traps from the address, not from the buffer size.
enum class Region : std::uint8_t {
    NullPage,
    KernelRam,
    UserRam,
    Unbacked,
    Aperture,
    Devices,
    Reserved,
};

struct RegionSpan {
    Region region;
    std::uint32_t base;
    std::uint32_t size;

    // 64-bit because the last row ends at 0x100000000, which is not a 32-bit value.
    [[nodiscard]] std::uint64_t end() const { return static_cast<std::uint64_t>(base) + size; }
};

[[nodiscard]] RegionSpan region_of(std::uint32_t address) noexcept;
[[nodiscard]] const char* to_string(Region region) noexcept;

enum class Access : std::uint8_t {
    Ok,
    Unmapped,
};

// RAM plus the address-space map. The aperture and the device window are
// classified here but not backed: their accesses belong to the device model,
// and an access that this class cannot serve is reported as Unmapped so the
// executor decides which vector that is.
//
// The null page and every unbacked row are Unmapped even though they are
// inside the RAM buffer's address range, because ISA §2.3 makes an access to
// either a fault. The buffer holds 0x00000000-0x0FFFFFFF contiguously; the null
// page is simply never served.
class Memory {
public:
    static constexpr std::uint32_t kRamSize = 0x10000000;  // 256 MiB
    static constexpr std::uint32_t kFirstRamByte = 0x00000100;

    // `ram_bytes` smaller than the full 256 MiB builds a short machine for
    // tests; larger is a configuration error and throws.
    explicit Memory(std::uint32_t ram_bytes = kRamSize);

    // True when [address, address + width) is backed RAM this build serves.
    [[nodiscard]] bool contains(std::uint32_t address, std::uint32_t width) const noexcept;

    [[nodiscard]] Access read8(std::uint32_t address, std::uint8_t& out) const noexcept;
    [[nodiscard]] Access read16(std::uint32_t address, std::uint16_t& out) const noexcept;
    [[nodiscard]] Access read32(std::uint32_t address, std::uint32_t& out) const noexcept;

    [[nodiscard]] Access write8(std::uint32_t address, std::uint8_t value) noexcept;
    [[nodiscard]] Access write16(std::uint32_t address, std::uint16_t value) noexcept;
    [[nodiscard]] Access write32(std::uint32_t address, std::uint32_t value) noexcept;

    // Bytes this build owns, for a debugger dump and for sizing a golden.
    [[nodiscard]] std::uint32_t ram_bytes() const noexcept {
        return static_cast<std::uint32_t>(ram_.size());
    }

    // A loader write: the host may fill the null page, which only a guest access
    // traps on (ISA §2.3). False when the range leaves RAM.
    [[nodiscard]] bool load(std::uint32_t address, std::span<const std::uint8_t> bytes) noexcept;

    // Fetches up to `count` instruction bytes from backed RAM. The executor uses
    // this instead of eight read8 calls per instruction: the range is checked
    // once and the bytes are copied directly. Returns the number available, 0
    // when the first byte is not backed RAM, and stops at the end of RAM.
    [[nodiscard]] unsigned fetch(std::uint32_t address, std::uint8_t* out,
                                 unsigned count) const noexcept {
        if (address < kFirstRamByte ||
            static_cast<std::uint64_t>(address) >= ram_.size()) {
            return 0;
        }
        const auto available = static_cast<unsigned>(
            std::min<std::size_t>(count, ram_.size() - address));
        std::memcpy(out, ram_.data() + address, available);
        return available;
    }

private:
    std::vector<std::uint8_t> ram_;
};

}  // namespace kr232
