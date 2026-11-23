#pragma once

#include <string_view>

namespace kr232 {

// The version string this build reports, "kr232 <major>.<minor>.<patch>".
// A teaching tool prints it so a reported defect names a build.
[[nodiscard]] std::string_view version() noexcept;

}  // namespace kr232
