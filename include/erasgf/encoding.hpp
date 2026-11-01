#pragma once

#include <string>
#include "erasgf/compat.hpp"
#include <vector>

namespace erasgf {
struct EncodingChoice {
    std::string name;
    bool uncertain = false;
    std::string reason;
};
std::string normalize_encoding(StringView name);
std::string decode_text(StringView bytes, StringView encoding);
EncodingChoice choose_encoding(StringView sample, StringView declared, StringView manual = "auto");
const std::vector<std::string>& supported_encodings();
} // namespace erasgf
