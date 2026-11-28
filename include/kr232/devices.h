#pragma once

#include <krc/display/text_display.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kr232 {

class Memory;

enum class DeviceResult : std::uint8_t {
    Ok,
    Undefined,   // an offset the device does not define: an address error
    Reserved,    // a page with no device: an address error
    MisShaped,   // not a 32-bit word at a 4-byte offset: an address error
};

// The device window of ISA §14.1: 240 pages of 4 KiB, one device per page, and
// `base + 0x3C` is the ID word on every populated page (kr232-devices.md §1).
//
// All eight populated pages are implemented: timer, console, framebuffer,
// keyboard, terminal, graphics commands, system control and blitter. A reserved
// page, an undefined offset or a mis-shaped access is an address error, never a
// silent zero.
class Devices {
public:
    static constexpr std::uint32_t kBase = 0xFFF00000u;
    static constexpr std::uint32_t kWindow = 0x000F0000u;  // 240 × 4 KiB
    static constexpr std::uint32_t kPageSize = 0x1000u;
    static constexpr std::uint32_t kIdOffset = 0x3Cu;

    // The graphics clip starts as the whole target (kr232-devices.md §8.1). A
    // zero clip draws nothing without failing, which is how commanded drawing
    // looked broken while it was only unclipped.
    Devices() noexcept;

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
    // Called when the guest reads console input and the queue is empty. The
    // callback may push a line; it must not re-enter the device.
    void set_console_input_refill(std::function<void()> refill);
    // Called for every console byte the guest writes, before it accumulates. An
    // interactive console writes it through to stdout.
    void set_console_output_sink(std::function<void(std::uint8_t)> sink);
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

    // Graphics commands (page 9) are executed when the guest rings the doorbell.
    // DRAW_TEXT and UPLOAD read guest RAM, so the machine hands it in rather than
    // the device owning a second view of memory.
    [[nodiscard]] bool graphics_pending() const noexcept { return graphics_pending_; }
    void run_graphics(const Memory& ram);

    // The blitter (page 11) performs a copy or fill before the store that starts
    // it completes; the machine runs it after that instruction retires.
    [[nodiscard]] bool blitter_pending() const noexcept { return blitter_pending_; }
    void run_blitter(Memory& ram);

    // The target surface a command draws into; exposed for tests and, at M2, for
    // the frame hash.
    struct Surface {
        std::uint32_t offset = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t stride = 0;
    };
    [[nodiscard]] Surface target_surface() const noexcept;

    // The terminal's cell as the page presents it: the character and the palette
    // indices that were current when it was written (kr232-devices.md §7.3).
    struct Cell {
        std::uint8_t ch = ' ';
        std::uint8_t fg = 7;
        std::uint8_t bg = 0;
    };
    [[nodiscard]] std::uint32_t terminal_columns() const noexcept { return term_cols_; }
    [[nodiscard]] std::uint32_t terminal_rows() const noexcept { return term_rows_; }
    [[nodiscard]] Cell terminal_cell(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::uint32_t terminal_cursor_x() const noexcept;
    [[nodiscard]] std::uint32_t terminal_cursor_y() const noexcept;
    // The console's output bytes, in the order a program wrote them. This is the
    // console page's observable behaviour, independent of any renderer; it is the
    // debug serial stream, and stdio does not write it.
    [[nodiscard]] const std::deque<std::uint8_t>& console_output() const noexcept {
        return console_output_;
    }

    // ---- shared text display ---------------------------------------------------
    //
    // The terminal page is a register front end over the shared host text display
    // from `krc::display`. The display owns the grid and the input queues; the page
    // maps its DATA/STATUS/CONTROL/COMMAND/COLOR registers onto it, and a debugger
    // renders the cells and forwards keys, text and clicks. The guest never reads a
    // cell back.
    [[nodiscard]] krc::display::TextDisplay& display() noexcept;
    [[nodiscard]] const krc::display::TextDisplay& display() const noexcept;

    // Host input for the display, served by a guest read of the standard input
    // stream (SYS read descriptor 0): the completed line editor output, then the raw
    // key queue. `NotReady` means the source is open but has queued nothing yet;
    // `EndOfInput` means the host closed it. `byte` is valid only for `Byte`.
    enum class InputState : std::uint8_t { Byte, NotReady, EndOfInput };
    [[nodiscard]] InputState terminal_read_byte(std::uint8_t& byte);
    // Called when a read finds nothing queued and the source is open; it may queue a
    // line, and must not re-enter the device. A host that reaches end of input calls
    // close_terminal_input() from the callback.
    void set_terminal_input_refill(std::function<void()> refill);
    // Offers the source a chance to queue display input before a SYS 5 READ_LINE or
    // GET_KEY runs. The callback guards itself, so this is safe to call every time.
    void poll_terminal_input() {
        if (terminal_refill_) {
            terminal_refill_();
        }
    }
    // The source is exhausted: no further key or text can arrive, and a drained
    // display input answers EndOfInput. Idempotent.
    void close_terminal_input() noexcept;
    [[nodiscard]] bool terminal_input_closed() const noexcept;
    // Called for every accepted display character, before it is drawn. An
    // interactive front end writes it through to its own output.
    void set_terminal_output_sink(std::function<void(std::uint8_t)> sink);

private:
    void flip() noexcept;
    [[nodiscard]] bool mode_ok() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t>& vram();

    // Terminal helpers. Geometry follows the committed framebuffer mode, so a
    // flip that changes the mode re-sizes the display and homes the cursor.
    void terminal_sync_geometry() noexcept;
    void terminal_clear() noexcept;
    void terminal_put(std::uint8_t ch) noexcept;
    void terminal_apply_color() noexcept;

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
    std::function<void()> console_refill_;
    std::function<void(std::uint8_t)> console_sink_;
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

    // Page 9.
    std::uint32_t g_command_ = 0;
    std::uint32_t g_args_[8] = {};
    bool g_error_ = false;
    bool graphics_pending_ = false;
    void reset_clip() noexcept;

    bool explicit_surface_ = false;
    Surface target_;
    std::uint32_t clip_x_ = 0;
    std::uint32_t clip_y_ = 0;
    std::uint32_t clip_w_ = 0;
    std::uint32_t clip_h_ = 0;

    // Page 8: the shared display. The optional is engaged whenever the device has a
    // valid geometry, which is from construction onward; a committed mode change
    // replaces it because the shared display's geometry is fixed at construction.
    std::optional<krc::display::TextDisplay> display_;
    std::uint32_t term_cols_ = 0;
    std::uint32_t term_rows_ = 0;
    // Bit 0 is cursor visible. The reset state hides nothing (kr232-devices.md §12:
    // "cursor at home and visible"), so the reset value is 1.
    std::uint32_t term_control_ = 1;
    std::uint32_t term_color_ = 0x07;
    std::deque<std::uint8_t> terminal_pending_;
    std::function<void()> terminal_refill_;
    std::function<void(std::uint8_t)> terminal_sink_;

    // Page 11.
    std::uint32_t blit_src_ = 0;
    std::uint32_t blit_dst_ = 0;
    std::uint32_t blit_len_ = 0;
    std::uint32_t blit_value_ = 0;
    std::uint32_t blit_status_ = 0;
    bool blit_fill_ = false;
    unsigned blit_size_ = 1;
    bool blitter_pending_ = false;
};

}  // namespace kr232
