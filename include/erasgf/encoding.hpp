#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace erasgf {
struct EncodingChoice {
    std::string name;
    bool uncertain = false;
    std::string reason;
};
std::string normalize_encoding(std::string_view name);
std::string decode_text(std::string_view bytes, std::string_view encoding);
EncodingChoice choose_encoding(std::string_view sample, std::string_view declared, std::string_view manual = "auto");
const std::vector<std::string>& supported_encodings();
} // namespace erasgf
