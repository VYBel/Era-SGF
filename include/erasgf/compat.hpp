#pragma once

// The official SDK 6.8 ships GCC/libstdc++ 6.3. Its Library Fundamentals
// implementations provide the small optional/view types without another dependency.
#include <experimental/optional>
#include <experimental/string_view>

namespace erasgf {
using StringView = std::experimental::string_view;
template<class T> using Optional = std::experimental::optional<T>;
constexpr auto none = std::experimental::nullopt;
}
