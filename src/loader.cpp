#include <kr232/loader.h>

namespace kr232 {

LoadOutcome load_flat(Machine& machine, std::span<const std::uint8_t> image, std::uint32_t base) {
    LoadOutcome outcome;
    if (image.empty()) {
        outcome.error = "the image is empty";
        return outcome;
    }
    // The host may fill the null page, so the fit check is against RAM size
    // rather than the guest's readable range.
    if (static_cast<std::uint64_t>(base) + image.size() > machine.memory().ram_bytes()) {
        outcome.error = "the image does not fit in RAM at that base";
        return outcome;
    }
    if (!machine.memory().load(base, image)) {
        outcome.error = "the image does not fit in RAM at that base";
        return outcome;
    }
    if (base == 0) {
        std::uint32_t entry = 0;
        if (machine.memory().read32(0x100, entry) != Access::Ok) {
            outcome.error = "no reset word at 0x0100";
            return outcome;
        }
        machine.set_pc(entry);
        outcome.ok = true;
        outcome.entry = entry;
        return outcome;
    }
    machine.set_pc(base);
    outcome.ok = true;
    outcome.entry = base;
    return outcome;
}

}  // namespace kr232
