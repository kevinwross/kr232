#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

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

    // Host side of the console (page 2) and keyboard (page 6), kr232-devices.md
    // §4 and §6. The console queue's capacity is this implementation's choice:
    // the document fixes the keyboard's 64 and only says "full" for the console.
    static constexpr std::size_t kConsoleQueue = 16;
    static constexpr std::size_t kKeyboardFifo = 64;

    // False when the queue is full; that arrival sets OVERRUN and is dropped.
    bool console_push_input(std::uint8_t byte) noexcept;
    // False when the guest has written nothing.
    bool console_pop_output(std::uint8_t& byte) noexcept;
    bool keyboard_push_event(std::uint32_t event) noexcept;

    // The aperture (kr232-devices.md §5.1) is memory-like: any width, any
    // alignment, so it is addressed in bytes and decoded little-endian.
    static constexpr std::uint32_t kApertureBase = 0xFC000000u;
    static constexpr std::uint32_t kApertureSize = 0x02000000u;  // 32 MiB

    [[nodiscard]] static bool in_aperture(std::uint32_t address) noexcept {
        return address >= kApertureBase && address - kApertureBase < kApertureSize;
    }
    [[nodiscard]] bool aperture_read(std::uint32_t address, unsigned size,
                                     std::uint32_t& value) noexcept;
    [[nodiscard]] bool aperture_write(std::uint32_t address, unsigned size,
                                      std::uint32_t value) noexcept;

    // What `FLIP` last committed: the surface the host presents.
    struct Frame {
        bool enabled = false;
        std::uint32_t offset = 0;
        std::uint32_t width = 800;
        std::uint32_t height = 600;
        std::uint32_t stride = 3200;
        std::uint64_t number = 0;
    };

    [[nodiscard]] Frame committed_frame() const noexcept { return committed_; }
    [[nodiscard]] const std::uint8_t* frame_bytes() const noexcept {
        return vram_ ? vram_->data() : nullptr;
    }

private:
    void flip() noexcept;
    [[nodiscard]] bool mode_ok() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t>& vram();

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

    std::deque<std::uint8_t> console_input_;
    std::deque<std::uint8_t> console_output_;
    bool console_overrun_ = false;
    std::deque<std::uint32_t> keyboard_events_;
    bool keyboard_overrun_ = false;

    // Page 4. VRAM is 32 MiB and is allocated on the first aperture access, so a
    // machine that never draws costs nothing.
    std::uint32_t fb_control_ = 0;
    std::uint32_t fb_width_ = 800;
    std::uint32_t fb_height_ = 600;
    std::uint32_t fb_stride_ = 3200;
    std::uint32_t fb_format_ = 1;
    std::uint32_t fb_front_ = 0;
    std::uint32_t fb_back_ = 0x001E0000;
    bool fb_flip_error_ = false;
    Frame committed_;
    std::unique_ptr<std::vector<std::uint8_t>> vram_;
};

}  // namespace kr232
