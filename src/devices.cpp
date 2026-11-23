#include <kr232/devices.h>

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
