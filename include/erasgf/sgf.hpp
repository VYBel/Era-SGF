#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include "erasgf/compat.hpp"
#include <vector>

namespace erasgf {
constexpr std::size_t no_node = static_cast<std::size_t>(-1);

struct Property {
    std::string name;
    std::vector<std::string> values;
};

struct Node {
    std::size_t parent = no_node;
    std::vector<std::size_t> children;
    std::vector<Property> properties;
    const Property* find(StringView name) const;
    std::string value(StringView name) const;
};

struct Game { std::vector<Node> nodes; };
struct Collection { std::vector<Game> games; };

struct ParseLimits {
    std::size_t max_bytes = 16 * 1024 * 1024;
    std::size_t max_nodes = 100000;
    std::size_t max_depth = 128;
};

Collection parse_sgf(StringView input, const ParseLimits& limits = {});
std::string game_text_sample(const Game& game);
} // namespace erasgf
