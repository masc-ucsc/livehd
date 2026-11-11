// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string_view>

namespace livehd::placement::gui {
// The single self-contained page served at `/` (no external script or style).
std::string_view page_html();
}  // namespace livehd::placement::gui
