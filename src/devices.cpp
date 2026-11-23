#include <kr232/devices.h>

#include <kr232/memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace kr232 {
namespace {

struct PageId {
    unsigned page;
    std::uint32_t id;
};

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
        case 6:  // DRAW_TEXT: the glyph set arrives with the terminal page
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
