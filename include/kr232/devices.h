#pragma once

#include <cstdint>

namespace kr232 {

enum class DeviceResult : std::uint8_t {
    Ok,
    Undefined,   // an offset the device does not define: an address error
    Reserved,    // a page with no device: an address error
    MisShaped,   // not a 32-bit word at a 4-byte offset: an address error
};

// The device window of ISA §14.1: 240 pages of 4 KiB, one device per page, and
// `base + 0x3C` is the ID word on every populated page (kr232-devices.md §1).
//
// Only the timer (page 0) and the system page (10) are implemented; the other six
// populated pages answer their ID and trap every other offset until their own
// commit, which is a recorded intermediate state rather than a silent zero.
class Devices {
public:
    static constexpr std::uint32_t kBase = 0xFFF00000u;
    static constexpr std::uint32_t kWindow = 0x000F0000u;  // 240 × 4 KiB
    static constexpr std::uint32_t kPageSize = 0x1000u;
    static constexpr std::uint32_t kIdOffset = 0x3Cu;

    [[nodiscard]] static bool contains(std::uint32_t address) noexcept {
        return address >= kBase && address < kBase + kWindow;
    }

    // A defined access is a 32-bit word at a 4-byte offset (kr232-devices.md §2).
    [[nodiscard]] DeviceResult read(std::uint32_t address, std::uint32_t width,
                                    std::uint32_t& value) noexcept;
    [[nodiscard]] DeviceResult write(std::uint32_t address, std::uint32_t width,
                                     std::uint32_t value) noexcept;

    // One machine cycle: the timer's counter is driven from it, and the system
    // page reports the same count.
    void tick(std::uint64_t cycle) noexcept;

    [[nodiscard]] bool halt_requested() const noexcept { return halt_; }
    [[nodiscard]] bool reset_requested() const noexcept { return reset_; }
    void clear_requests() noexcept {
        halt_ = false;
        reset_ = false;
    }

    [[nodiscard]] std::uint32_t id_of(unsigned page) const noexcept;

private:
    [[nodiscard]] DeviceResult decode(std::uint32_t address, std::uint32_t width, unsigned& page,
                                      std::uint32_t& offset) const noexcept;

    std::uint32_t timer_control_ = 0;
    std::uint32_t timer_reload_ = 0;
    std::uint32_t timer_count_ = 0;
    std::uint32_t timer_status_ = 0;
    std::uint32_t timer_irq_ = 0;
    std::uint64_t cycle_ = 0;
    bool halt_ = false;
    bool reset_ = false;
};

}  // namespace kr232
