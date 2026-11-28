#include <kr232/devices.h>

#include <kr232/memory.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>

namespace kr232 {
namespace {

struct PageId {
    unsigned page;
    std::uint32_t id;
};

// The fixed 16-entry palette of kr232-devices.md §7.3, as the XRGB8888 colours the
// shared display stores. The terminal page still speaks palette indices.
constexpr std::uint32_t kPaletteRgb[16] = {
    0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu, 0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
    0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu, 0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu,
};

// The shared display stores XRGB8888 with the alpha byte unused; the page draws
// opaque text, so the high byte is all ones.
constexpr std::uint32_t kOpaque = 0xFF000000u;

std::uint32_t palette_color(unsigned index) noexcept {
    return kOpaque | kPaletteRgb[index & 0x0Fu];
}

unsigned palette_index(std::uint32_t color, unsigned fallback) noexcept {
    for (unsigned index = 0; index < 16; ++index) {
        if ((color & 0x00FFFFFFu) == kPaletteRgb[index]) {
            return index;
        }
    }
    return fallback;
}

// The eight populated pages and their `ID` words (kr232-devices.md §3–§10).
constexpr PageId kPageIds[] = {
    {0, 0x544D5231u},   // TMR1
    {2, 0x434F4E31u},   // CON1
    {4, 0x46425231u},   // FBR1
    {6, 0x4B424431u},   // KBD1
    {8, 0x5445524Du},   // TERM
    {9, 0x47504831u},   // GPH1
    {10, 0x53595331u},  // SYS1
    {11, 0x424C5431u},  // BLT1
};

bool is_populated(unsigned page) noexcept {
    for (const PageId& entry : kPageIds) {
        if (entry.page == page) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::uint32_t Devices::id_of(unsigned page) const noexcept {
    for (const PageId& entry : kPageIds) {
        if (entry.page == page) {
            return entry.id;
        }
    }
    return 0;
}

DeviceResult Devices::decode(std::uint32_t address, std::uint32_t width, unsigned& page,
                             std::uint32_t& offset) const noexcept {
    if (width != 4 || (address & 3u) != 0) {
        return DeviceResult::MisShaped;
    }
    page = static_cast<unsigned>((address - kBase) / kPageSize);
    offset = (address - kBase) % kPageSize;
    if (page >= 240) {
        return DeviceResult::Reserved;
    }
    if (!is_populated(page)) {
        return DeviceResult::Reserved;
    }
    return DeviceResult::Ok;
}

Devices::Devices() noexcept {
    reset_clip();
    terminal_sync_geometry();
}

void Devices::reset_clip() noexcept {
    const Surface surface = target_surface();
    clip_x_ = 0;
    clip_y_ = 0;
    clip_w_ = surface.width;
    clip_h_ = surface.height;
}

Devices::Surface Devices::target_surface() const noexcept {
    if (explicit_surface_) {
        return target_;
    }
    return Surface{fb_back_, fb_width_, fb_height_, fb_stride_};
}

void Devices::run_graphics(const Memory& ram) {
    graphics_pending_ = false;
    const Surface surface = target_surface();
    std::vector<std::uint8_t>& memory = vram();

    const auto put = [&](std::uint32_t x, std::uint32_t y, std::uint32_t colour) {
        if (x < clip_x_ || y < clip_y_ || x >= clip_x_ + clip_w_ || y >= clip_y_ + clip_h_) {
            return;  // clipped: a drawing call never rejects, it just draws less
        }
        const std::uint64_t at = static_cast<std::uint64_t>(surface.offset) +
                                 static_cast<std::uint64_t>(y) * surface.stride + x * 4u;
        if (at + 4 > kApertureSize) {
            return;
        }
        for (unsigned byte = 0; byte < 4; ++byte) {
            memory[static_cast<std::size_t>(at) + byte] =
                static_cast<std::uint8_t>((colour >> (8 * byte)) & 0xFFu);
        }
    };
    const auto get = [&](std::uint32_t x, std::uint32_t y) {
        const std::uint64_t at = static_cast<std::uint64_t>(surface.offset) +
                                 static_cast<std::uint64_t>(y) * surface.stride + x * 4u;
        std::uint32_t colour = 0;
        if (at + 4 <= kApertureSize) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                colour |= static_cast<std::uint32_t>(memory[static_cast<std::size_t>(at) + byte])
                          << (8 * byte);
            }
        }
        return colour;
    };

    switch (g_command_) {
        case 1: {  // SET_CLIP: intersect with the target
            const std::uint32_t x = std::max(g_args_[0], clip_x_);
            const std::uint32_t y = std::max(g_args_[1], clip_y_);
            const std::uint32_t right = std::min(g_args_[0] + g_args_[2], clip_x_ + clip_w_);
            const std::uint32_t bottom = std::min(g_args_[1] + g_args_[3], clip_y_ + clip_h_);
            clip_x_ = x;
            clip_y_ = y;
            clip_w_ = right > x ? right - x : 0;
            clip_h_ = bottom > y ? bottom - y : 0;
            return;
        }
        case 3: {  // FILL_RECT
            const std::uint32_t x0 = std::max(g_args_[0], clip_x_);
            const std::uint32_t y0 = std::max(g_args_[1], clip_y_);
            const std::uint32_t x1 = std::min(g_args_[0] + g_args_[2], clip_x_ + clip_w_);
            const std::uint32_t y1 = std::min(g_args_[1] + g_args_[3], clip_y_ + clip_h_);
            for (std::uint32_t y = y0; y < y1; ++y) {
                for (std::uint32_t x = x0; x < x1; ++x) {
                    put(x, y, g_args_[4]);
                }
            }
            return;
        }
        case 4: {  // COPY_RECT within the target
            const std::uint32_t sx = g_args_[1];
            const std::uint32_t sy = g_args_[2];
            const std::uint32_t w = g_args_[3];
            const std::uint32_t h = g_args_[4];
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h, 0);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    pixels[static_cast<std::size_t>(y) * w + x] = get(sx + x, sy + y);
                }
            }
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    put(g_args_[1] + x, g_args_[2] + y, pixels[static_cast<std::size_t>(y) * w + x]);
                }
            }
            return;
        }
        case 5: {  // DRAW_LINE, both endpoints included
            std::int64_t x0 = g_args_[0];
            std::int64_t y0 = g_args_[1];
            const std::int64_t x1 = g_args_[2];
            const std::int64_t y1 = g_args_[3];
            const std::int64_t dx = std::llabs(x1 - x0);
            const std::int64_t dy = -std::llabs(y1 - y0);
            const std::int64_t sx = x0 < x1 ? 1 : -1;
            const std::int64_t sy = y0 < y1 ? 1 : -1;
            std::int64_t error = dx + dy;
            for (;;) {
                put(static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), g_args_[4]);
                if (x0 == x1 && y0 == y1) {
                    break;
                }
                const std::int64_t twice = 2 * error;
                if (twice >= dy) {
                    error += dy;
                    x0 += sx;
                }
                if (twice <= dx) {
                    error += dx;
                    y0 += sy;
                }
            }
            return;
        }
        case 6:  // DRAW_TEXT: no guest-visible atlas; the command refuses by design
            g_error_ = true;
            return;
        case 7:  // FLIP
            flip();
            return;
        case 8: {  // SET_SURFACE
            const std::uint32_t offset = g_args_[0];
            const std::uint32_t width = g_args_[1];
            const std::uint32_t height = g_args_[2];
            const std::uint32_t stride = g_args_[3];
            const bool ok = width >= 1 && width <= 1920 && height >= 1 && height <= 1080 &&
                            stride % 4u == 0 && stride >= width * 4u && stride <= 7680u &&
                            offset % 4u == 0 &&
                            static_cast<std::uint64_t>(offset) +
                                    static_cast<std::uint64_t>(height) * stride <=
                                kApertureSize;
            if (!ok) {
                g_error_ = true;
                return;
            }
            explicit_surface_ = true;
            target_ = Surface{offset, width, height, stride};
            clip_x_ = 0;
            clip_y_ = 0;
            clip_w_ = width;
            clip_h_ = height;
            return;
        }
        case 9: {  // COPY_RECT_STRIDED from another surface in the aperture
            const std::uint32_t source = g_args_[0];
            const std::uint32_t source_stride = g_args_[1];
            const std::uint32_t w = g_args_[4];
            const std::uint32_t h = g_args_[5];
            if (source_stride % 4u != 0 || source_stride < w * 4u ||
                static_cast<std::uint64_t>(source) + static_cast<std::uint64_t>(h) * source_stride >
                    kApertureSize) {
                g_error_ = true;
                return;
            }
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h, 0);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::uint64_t at = static_cast<std::uint64_t>(source) +
                                             static_cast<std::uint64_t>(y) * source_stride + x * 4u;
                    std::uint32_t colour = 0;
                    for (unsigned byte = 0; byte < 4; ++byte) {
                        colour |= static_cast<std::uint32_t>(
                                      memory[static_cast<std::size_t>(at) + byte])
                                  << (8 * byte);
                    }
                    pixels[static_cast<std::size_t>(y) * w + x] = colour;
                }
            }
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    put(g_args_[2] + x, g_args_[3] + y, pixels[static_cast<std::size_t>(y) * w + x]);
                }
            }
            return;
        }
        case 10: {  // UPLOAD from guest RAM
            const std::uint32_t address = g_args_[0];
            const std::uint32_t destination = g_args_[1];
            const std::uint32_t w = g_args_[2];
            const std::uint32_t h = g_args_[3];
            const std::uint32_t source_stride = g_args_[4];
            if (source_stride % 4u != 0 || source_stride < w * 4u ||
                static_cast<std::uint64_t>(destination) + static_cast<std::uint64_t>(h) * surface.stride >
                    kApertureSize) {
                g_error_ = true;
                return;
            }
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    std::uint32_t colour = 0;
                    const std::uint32_t from = address + y * source_stride + x * 4u;
                    if (ram.read32(from, colour) != Access::Ok) {
                        g_error_ = true;  // stop at the first row that leaves RAM
                        return;
                    }
                    const std::uint64_t at = static_cast<std::uint64_t>(destination) +
                                             static_cast<std::uint64_t>(y) * surface.stride + x * 4u;
                    for (unsigned byte = 0; byte < 4; ++byte) {
                        memory[static_cast<std::size_t>(at) + byte] =
                            static_cast<std::uint8_t>((colour >> (8 * byte)) & 0xFFu);
                    }
                }
            }
            return;
        }
        default:  // reserved: an error, and nothing drawn
            g_error_ = true;
            return;
    }
}

void Devices::terminal_sync_geometry() noexcept {
    const std::uint32_t cols = committed_.width / 10u;
    const std::uint32_t rows = committed_.height / 20u;
    const auto width = static_cast<std::uint16_t>(cols > 0 ? cols : 1);
    const auto height = static_cast<std::uint16_t>(rows > 0 ? rows : 1);
    if (display_ && width == term_cols_ && height == term_rows_) {
        return;
    }
    term_cols_ = width;
    term_rows_ = height;
    // The shared display fixes its geometry at construction, so a committed mode
    // change installs a new one. Its cells start in the colours the page holds.
    display_.emplace(height, width, palette_color(term_color_ & 0x0Fu),
                     palette_color((term_color_ >> 4) & 0x0Fu));
    display_->set_cursor_visible((term_control_ & 1u) != 0);
}

void Devices::terminal_clear() noexcept {
    terminal_sync_geometry();
    display_->clear();
    display_->set_cursor(0, 0);
}

void Devices::terminal_apply_color() noexcept {
    terminal_sync_geometry();
    display_->set_color(palette_color(term_color_ & 0x0Fu),
                        palette_color((term_color_ >> 4) & 0x0Fu));
}

void Devices::terminal_put(std::uint8_t ch) noexcept {
    terminal_sync_geometry();
    switch (ch) {
        case 0x0C:  // form feed: clear the grid and home the cursor
            terminal_clear();
            break;
        default:
            // Other control bytes and DEL are not cells: the page ignores them, and
            // they are not reported to an interactive host sink either.
            if ((ch < 0x20 && ch != 0x08 && ch != 0x09 && ch != 0x0A && ch != 0x0D) ||
                ch == 0x7F) {
                return;
            }
            // The shared display owns newline, carriage return, backspace, tab,
            // wrapping and scrolling (kr232-devices.md §7.2).
            display_->put(ch);
            break;
    }
    if (terminal_sink_) {
        terminal_sink_(ch);
    }
}

Devices::Cell Devices::terminal_cell(std::uint32_t x, std::uint32_t y) const noexcept {
    Cell cell;
    if (x >= term_cols_ || y >= term_rows_) {
        return cell;
    }
    const krc::display::Cell& shared =
        display().cell(static_cast<std::uint16_t>(y), static_cast<std::uint16_t>(x));
    cell.ch = shared.code == 0
                  ? static_cast<std::uint8_t>(' ')
                  : static_cast<std::uint8_t>(shared.code <= 0xFFu ? shared.code : ' ');
    cell.fg = static_cast<std::uint8_t>(palette_index(shared.foreground, 7));
    cell.bg = static_cast<std::uint8_t>(palette_index(shared.background, 0));
    return cell;
}

krc::display::TextDisplay& Devices::display() noexcept {
    assert(display_);
    return *display_;
}

const krc::display::TextDisplay& Devices::display() const noexcept {
    assert(display_);
    return *display_;
}

std::uint32_t Devices::terminal_cursor_x() const noexcept { return display().cursor_col(); }

std::uint32_t Devices::terminal_cursor_y() const noexcept { return display().cursor_row(); }

void Devices::set_terminal_input_refill(std::function<void()> refill) {
    terminal_refill_ = std::move(refill);
}

void Devices::close_terminal_input() noexcept { display().close_input(); }

bool Devices::terminal_input_closed() const noexcept { return display().input_closed(); }

void Devices::set_terminal_output_sink(std::function<void(std::uint8_t)> sink) {
    terminal_sink_ = std::move(sink);
}

Devices::InputState Devices::terminal_read_byte(std::uint8_t& byte) {
    terminal_sync_geometry();
    krc::display::TextDisplay& text = *display_;
    bool refilled = false;
    for (;;) {
        if (!terminal_pending_.empty()) {
            byte = terminal_pending_.front();
            terminal_pending_.pop_front();
            return InputState::Byte;
        }
        if (text.line_ready()) {
            const std::optional<std::string> line = text.take_line();
            if (line) {
                for (const char character : *line) {
                    terminal_pending_.push_back(static_cast<std::uint8_t>(character));
                }
                terminal_pending_.push_back('\n');
                continue;
            }
        }
        const int key = text.get_key();
        if (key >= 0) {
            if (key == krc::display::kKeyEnter) {
                byte = '\n';
                return InputState::Byte;
            }
            if (key <= 0xFF) {
                byte = static_cast<std::uint8_t>(key);
                return InputState::Byte;
            }
            continue;  // an extended key is not a byte of the standard input stream
        }
        if (key == krc::display::kStatusEndOfInput) {
            return InputState::EndOfInput;
        }
        // Nothing queued yet. A host source may supply a line; after that the
        // source either has input, has closed, or is still waiting for the user.
        if (!refilled && terminal_refill_) {
            refilled = true;
            terminal_refill_();
            continue;
        }
        if (text.input_closed()) {
            return InputState::EndOfInput;
        }
        // Arm the host line editor so typing echoes at the cursor and Enter
        // completes a line; this request answers "not ready" and the guest polls.
        text.arm_line_editor(text.cols());
        return InputState::NotReady;
    }
}

void Devices::run_blitter(Memory& ram) {
    blitter_pending_ = false;
    blit_status_ = 0;  // starting clears DONE and ERROR

    const auto in_range = [](std::uint32_t address, std::uint32_t length) {
        if (length == 0) {
            return true;  // a zero-length operation succeeds wherever it points
        }
        const std::uint64_t last = static_cast<std::uint64_t>(address) + length;
        if (static_cast<std::uint64_t>(address) >= kApertureBase &&
            last <= static_cast<std::uint64_t>(kApertureBase) + kApertureSize) {
            return true;
        }
        // RAM is 0x00000100-0xFBFFFFFF: the null page is excluded and the range
        // stops at the aperture, so a descriptor crossing the gap is rejected.
        return address >= 0x00000100u && last <= 0xFC000000u;
    };

    const bool need_source = !blit_fill_;
    if (!in_range(blit_dst_, blit_len_) || (need_source && !in_range(blit_src_, blit_len_)) ||
        blit_size_ == 0) {
        blit_status_ = 2u;  // ERROR, and nothing has moved
        return;
    }

    const auto read_byte = [&](std::uint32_t address, std::uint8_t& byte) {
        if (in_aperture(address)) {
            std::uint32_t word = 0;
            if (!aperture_read(address, 1, word)) {
                return false;
            }
            byte = static_cast<std::uint8_t>(word & 0xFFu);
            return true;
        }
        return ram.read8(address, byte) == Access::Ok;
    };
    const auto write_byte = [&](std::uint32_t address, std::uint8_t byte) {
        if (in_aperture(address)) {
            return aperture_write(address, 1, byte);
        }
        return ram.write8(address, byte) == Access::Ok;
    };

    if (blit_fill_) {
        for (std::uint32_t i = 0; i < blit_len_; ++i) {
            const std::uint8_t byte =
                static_cast<std::uint8_t>((blit_value_ >> (8 * (i % blit_size_))) & 0xFFu);
            (void)write_byte(blit_dst_ + i, byte);
        }
        blit_status_ = 1u;  // DONE
        return;
    }

    // memmove semantics: copy backwards when the ranges overlap and the
    // destination is above the source, so no byte is read after being written.
    const bool backwards = blit_dst_ > blit_src_ && blit_dst_ < blit_src_ + blit_len_;
    for (std::uint32_t step = 0; step < blit_len_; ++step) {
        const std::uint32_t i = backwards ? blit_len_ - 1 - step : step;
        std::uint8_t byte = 0;
        if (!read_byte(blit_src_ + i, byte) || !write_byte(blit_dst_ + i, byte)) {
            blit_status_ = 2u;  // ERROR
            return;
        }
    }
    blit_status_ = 1u;  // DONE
}

DeviceResult Devices::read(std::uint32_t address, std::uint32_t width,
                           std::uint32_t& value) noexcept {
    unsigned page = 0;
    std::uint32_t offset = 0;
    const DeviceResult decoded = decode(address, width, page, offset);
    if (decoded != DeviceResult::Ok) {
        return decoded;
    }
    if (offset == kIdOffset) {
        value = id_of(page);
        return DeviceResult::Ok;
    }
    if (page == 0) {
        switch (offset) {
            case 0x00:
                value = timer_control_;
                return DeviceResult::Ok;
            case 0x04:
                value = timer_reload_;
                return DeviceResult::Ok;
            case 0x08:
                value = timer_count_;
                return DeviceResult::Ok;
            case 0x0C:
                value = timer_status_;
                return DeviceResult::Ok;
            case 0x10:
                value = timer_irq_;
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 10) {
        switch (offset) {
            case 0x00:  // HALT: write-only, reads as zero
            case 0x04:  // RESET: write-only, reads as zero
                value = 0;
                return DeviceResult::Ok;
            case 0x08:
                value = static_cast<std::uint32_t>(cycle_ & 0xFFFFFFFFu);
                return DeviceResult::Ok;
            case 0x0C:
                value = static_cast<std::uint32_t>(cycle_ >> 32);
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 2) {  // console
        switch (offset) {
            case 0x00:  // OUT is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x04: {  // IN consumes one byte; empty reads all ones
                if (console_input_.empty() && console_refill_) {
                    console_refill_();
                }
                if (console_input_.empty()) {
                    value = 0xFFFFFFFFu;
                } else {
                    value = console_input_.front();
                    console_input_.pop_front();
                }
                return DeviceResult::Ok;
            }
            case 0x08: {  // STATUS: RX_READY, TX_READY (always), OVERRUN
                std::uint32_t status = 1u << 1;
                if (!console_input_.empty()) {
                    status |= 1u;
                }
                if (console_overrun_) {
                    status |= 1u << 2;
                }
                value = status;
                return DeviceResult::Ok;
            }
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 6) {  // keyboard
        switch (offset) {
            case 0x00: {  // DATA pops one event; empty reads all ones
                if (keyboard_events_.empty()) {
                    value = 0xFFFFFFFFu;
                } else {
                    value = keyboard_events_.front();
                    keyboard_events_.pop_front();
                }
                return DeviceResult::Ok;
            }
            case 0x04: {  // STATUS: AVAILABLE, OVERRUN, count in bits 8-15
                std::uint32_t status = 0;
                if (!keyboard_events_.empty()) {
                    status |= 1u;
                }
                if (keyboard_overrun_) {
                    status |= 1u << 1;
                }
                status |= static_cast<std::uint32_t>(keyboard_events_.size() & 0xFFu) << 8;
                value = status;
                return DeviceResult::Ok;
            }
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 4) {  // framebuffer
        switch (offset) {
            case 0x00:
                value = fb_control_;
                return DeviceResult::Ok;
            case 0x04:
                value = fb_width_;
                return DeviceResult::Ok;
            case 0x08:
                value = fb_height_;
                return DeviceResult::Ok;
            case 0x0C:
                value = fb_stride_;
                return DeviceResult::Ok;
            case 0x10:
                value = fb_format_;
                return DeviceResult::Ok;
            case 0x14:
                value = fb_front_;
                return DeviceResult::Ok;
            case 0x18:
                value = fb_back_;
                return DeviceResult::Ok;
            case 0x1C: {  // committed enable, MODE_OK computed, sticky FLIP_ERROR
                std::uint32_t status = 0;
                if (committed_.enabled) {
                    status |= 1u;
                }
                if (mode_ok()) {
                    status |= 1u << 1;
                }
                if (fb_flip_error_) {
                    status |= 1u << 2;
                }
                value = status;
                return DeviceResult::Ok;
            }
            case 0x20:  // FLIP is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x24:
                value = static_cast<std::uint32_t>(committed_.number & 0xFFFFFFFFu);
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 8) {  // terminal
        terminal_sync_geometry();
        switch (offset) {
            case 0x00:  // DATA is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x04:  // READY, columns, rows
                value = 1u | (term_cols_ << 8) | (term_rows_ << 16);
                return DeviceResult::Ok;
            case 0x08:
                value = term_control_;
                return DeviceResult::Ok;
            case 0x0C:  // COMMAND is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x10:
                value = term_color_;
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 9) {  // graphics commands
        switch (offset) {
            case 0x00:
                value = g_command_;
                return DeviceResult::Ok;
            case 0x24:  // DOORBELL is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x28:
                value = g_error_ ? 1u : 0u;
                return DeviceResult::Ok;
            default:
                if (offset >= 0x04 && offset <= 0x20) {
                    value = g_args_[(offset - 0x04) / 4];
                    return DeviceResult::Ok;
                }
                return DeviceResult::Undefined;
        }
    }
    if (page == 11) {  // blitter
        switch (offset) {
            case 0x00:
                value = blit_src_;
                return DeviceResult::Ok;
            case 0x04:
                value = blit_dst_;
                return DeviceResult::Ok;
            case 0x08:
                value = blit_len_;
                return DeviceResult::Ok;
            case 0x0C:
                value = blit_value_;
                return DeviceResult::Ok;
            case 0x10:  // CONTROL is write-only
                value = 0;
                return DeviceResult::Ok;
            case 0x14:
                value = blit_status_;
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    return DeviceResult::Undefined;  // a populated page whose registers are not written yet
}

DeviceResult Devices::write(std::uint32_t address, std::uint32_t width, std::uint32_t value) noexcept {
    unsigned page = 0;
    std::uint32_t offset = 0;
    const DeviceResult decoded = decode(address, width, page, offset);
    if (decoded != DeviceResult::Ok) {
        return decoded;
    }
    if (offset == kIdOffset) {
        return DeviceResult::Ok;  // read-only: a write is ignored, not a trap
    }
    if (page == 0) {
        switch (offset) {
            case 0x00:
                timer_control_ = value;
                return DeviceResult::Ok;
            case 0x04:
                timer_reload_ = value;
                timer_count_ = value;  // a write to RELOAD also loads COUNT
                return DeviceResult::Ok;
            case 0x08:
                return DeviceResult::Ok;  // COUNT is read-only
            case 0x0C:
                if ((value & 1u) != 0) {
                    timer_status_ &= ~1u;  // write 1 clears PENDING
                }
                return DeviceResult::Ok;
            case 0x10:
                timer_irq_ = value;
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 10) {
        switch (offset) {
            case 0x00:
                halt_ = true;
                return DeviceResult::Ok;
            case 0x04:
                reset_ = true;
                return DeviceResult::Ok;
            case 0x08:
            case 0x0C:
                return DeviceResult::Ok;  // cycle counters are read-only
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 2) {  // console
        switch (offset) {
            case 0x00:  // OUT appends one byte to the host stream
                console_output_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
                if (console_sink_) {
                    console_sink_(static_cast<std::uint8_t>(value & 0xFFu));
                }
                return DeviceResult::Ok;
            case 0x04:  // IN is read-only
                return DeviceResult::Ok;
            case 0x08:  // writing 1 clears OVERRUN
                if (value != 0) {
                    console_overrun_ = false;
                }
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 6) {  // keyboard
        switch (offset) {
            case 0x00:  // DATA is read-only
                return DeviceResult::Ok;
            case 0x04:  // writing 1 clears OVERRUN
                if (value != 0) {
                    keyboard_overrun_ = false;
                }
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 4) {  // framebuffer
        switch (offset) {
            case 0x00:
                fb_control_ = value & 1u;
                return DeviceResult::Ok;
            case 0x04:
                if (value >= 1 && value <= 1920) {
                    fb_width_ = value;
                    fb_stride_ = value * 4u;  // a successful WIDTH write sets STRIDE
                }
                return DeviceResult::Ok;
            case 0x08:
                if (value >= 1 && value <= 1080) {
                    fb_height_ = value;
                    reset_clip();
                }
                return DeviceResult::Ok;
            case 0x0C:
                if (value % 4u == 0 && value >= fb_width_ * 4u && value <= 7680u) {
                    fb_stride_ = value;
                }
                return DeviceResult::Ok;
            case 0x10:
                if (value == 1) {
                    fb_format_ = 1;
                }
                return DeviceResult::Ok;
            case 0x14:
                if (value % 4u == 0) {
                    fb_front_ = value;
                }
                return DeviceResult::Ok;
            case 0x18:
                if (value % 4u == 0) {
                    fb_back_ = value;
                }
                return DeviceResult::Ok;
            case 0x1C:
                if ((value & 4u) != 0) {
                    fb_flip_error_ = false;  // write 1 clears the sticky error
                }
                return DeviceResult::Ok;
            case 0x20:
                if ((value & 1u) != 0) {
                    flip();
                }
                return DeviceResult::Ok;
            case 0x24:
                return DeviceResult::Ok;  // FRAME is read-only
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 8) {  // terminal
        switch (offset) {
            case 0x00:
                terminal_put(static_cast<std::uint8_t>(value & 0xFFu));
                return DeviceResult::Ok;
            case 0x04:  // STATUS is read-only
                return DeviceResult::Ok;
            case 0x08:
                term_control_ = value;
                terminal_sync_geometry();
                display_->set_cursor_visible((value & 1u) != 0);
                return DeviceResult::Ok;
            case 0x0C:
                if (value == 1) {
                    terminal_clear();
                } else if (value == 2) {  // full reset
                    term_control_ = 1;    // cursor visible (§12)
                    term_color_ = 0x07;
                    terminal_apply_color();
                    terminal_clear();
                    display_->set_cursor_visible(true);
                }
                return DeviceResult::Ok;
            case 0x10:
                term_color_ = value & 0xFFu;
                terminal_apply_color();
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    if (page == 9) {  // graphics commands
        switch (offset) {
            case 0x00:
                g_command_ = value;
                return DeviceResult::Ok;
            case 0x24:
                graphics_pending_ = true;
                return DeviceResult::Ok;
            case 0x28:
                if ((value & 1u) != 0) {
                    g_error_ = false;  // write 1 clears the sticky error
                }
                return DeviceResult::Ok;
            default:
                if (offset >= 0x04 && offset <= 0x20) {
                    g_args_[(offset - 0x04) / 4] = value;
                    return DeviceResult::Ok;
                }
                return DeviceResult::Undefined;
        }
    }
    if (page == 11) {  // blitter
        switch (offset) {
            case 0x00:
                blit_src_ = value;
                return DeviceResult::Ok;
            case 0x04:
                blit_dst_ = value;
                return DeviceResult::Ok;
            case 0x08:
                blit_len_ = value;
                return DeviceResult::Ok;
            case 0x0C:
                blit_value_ = value;
                return DeviceResult::Ok;
            case 0x10: {  // start, mode, size
                blit_fill_ = (value & 2u) != 0;
                switch ((value >> 2) & 3u) {
                    case 0:
                        blit_size_ = 1;
                        break;
                    case 1:
                        blit_size_ = 2;
                        break;
                    case 2:
                        blit_size_ = 4;
                        break;
                    default:
                        blit_size_ = 0;  // reserved: the start is rejected
                        break;
                }
                if ((value & 1u) != 0) {
                    blitter_pending_ = true;
                }
                return DeviceResult::Ok;
            }
            case 0x14:
                blit_status_ &= ~(value & 3u);  // write 1 clears DONE and ERROR
                return DeviceResult::Ok;
            default:
                return DeviceResult::Undefined;
        }
    }
    return DeviceResult::Undefined;
}

bool Devices::console_push_input(std::uint8_t byte) noexcept {
    if (console_input_.size() >= kConsoleQueue) {
        console_overrun_ = true;
        return false;
    }
    console_input_.push_back(byte);
    return true;
}

bool Devices::console_pop_output(std::uint8_t& byte) noexcept {
    if (console_output_.empty()) {
        return false;
    }
    byte = console_output_.front();
    console_output_.pop_front();
    return true;
}

void Devices::set_console_input_refill(std::function<void()> refill) {
    console_refill_ = std::move(refill);
}

void Devices::set_console_output_sink(std::function<void(std::uint8_t)> sink) {
    console_sink_ = std::move(sink);
}

bool Devices::keyboard_push_event(std::uint32_t event) noexcept {
    if (keyboard_events_.size() >= kKeyboardFifo) {
        keyboard_overrun_ = true;
        return false;
    }
    keyboard_events_.push_back(event);
    return true;
}

bool Devices::mode_ok() const noexcept {
    // kr232-devices.md §5.3, the five validation rules.
    if (fb_width_ < 1 || fb_width_ > 1920 || fb_height_ < 1 || fb_height_ > 1080) {
        return false;
    }
    if (fb_stride_ % 4u != 0 || fb_stride_ < fb_width_ * 4u || fb_stride_ > 7680u) {
        return false;
    }
    if (fb_format_ != 1) {
        return false;
    }
    if (fb_front_ % 4u != 0 || fb_back_ % 4u != 0) {
        return false;
    }
    const auto extent = static_cast<std::uint64_t>(fb_height_) * fb_stride_;
    return static_cast<std::uint64_t>(fb_front_) + extent <= kApertureSize &&
           static_cast<std::uint64_t>(fb_back_) + extent <= kApertureSize;
}

void Devices::flip() noexcept {
    if (!mode_ok()) {
        fb_flip_error_ = true;
        return;
    }
    // The committed frame is the surface just drawn, which is BACK; then the two
    // swap so the next frame draws into what was presented.
    committed_.enabled = (fb_control_ & 1u) != 0;
    committed_.offset = fb_back_;
    committed_.width = fb_width_;
    committed_.height = fb_height_;
    committed_.stride = fb_stride_;
    std::swap(fb_front_, fb_back_);
    ++committed_.number;
    (void)vram();               // the frame hash must read a committed frame
    terminal_sync_geometry();   // the committed mode may have changed the grid
}

std::vector<std::uint8_t>& Devices::vram() {
    if (!vram_) {
        vram_ = std::make_unique<std::vector<std::uint8_t>>(static_cast<std::size_t>(kApertureSize),
                                                            std::uint8_t{0});
    }
    return *vram_;
}

bool Devices::aperture_read(std::uint32_t address, unsigned size, std::uint32_t& value) noexcept {
    if (size == 0 || size > 4) {
        return false;
    }
    const std::uint32_t offset = address - kApertureBase;
    if (static_cast<std::uint64_t>(offset) + size > kApertureSize) {
        return false;
    }
    std::vector<std::uint8_t>& memory = vram();
    std::uint32_t word = 0;
    for (unsigned i = 0; i < size; ++i) {
        word |= static_cast<std::uint32_t>(memory[offset + i]) << (8 * i);
    }
    value = word;
    return true;
}

bool Devices::aperture_write(std::uint32_t address, unsigned size, std::uint32_t value) noexcept {
    if (size == 0 || size > 4) {
        return false;
    }
    const std::uint32_t offset = address - kApertureBase;
    if (static_cast<std::uint64_t>(offset) + size > kApertureSize) {
        return false;
    }
    std::vector<std::uint8_t>& memory = vram();
    for (unsigned i = 0; i < size; ++i) {
        memory[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
    return true;
}

void Devices::tick(std::uint64_t cycle) noexcept {
    cycle_ = cycle;
    if ((timer_control_ & 1u) == 0 || timer_count_ == 0) {
        return;
    }
    --timer_count_;
    if (timer_count_ != 0) {
        return;
    }
    timer_status_ |= 1u;
    if ((timer_control_ & 2u) != 0) {
        timer_count_ = timer_reload_;
    } else {
        timer_control_ &= ~1u;
    }
}

}  // namespace kr232
