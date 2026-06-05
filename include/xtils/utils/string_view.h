#pragma once

// DEPRECATED: This header is retained for backward compatibility only.
// Use <string_view> directly for std::string_view, and include
// "xtils/utils/string_utils.h" for CaseInsensitiveEq, StartsWith, EndsWith.

#include <string_view>

#include "xtils/utils/string_utils.h"

namespace xtils {

// DEPRECATED: Use std::string_view directly.
using StringView = std::string_view;

}  // namespace xtils
