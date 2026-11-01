#include "erasgf/sgf.hpp"
#include <algorithm>

namespace erasgf {
const Property* Node::find(StringView name) const {
    for (const auto& p : properties) if (p.name == name) return &p;
    return nullptr;
}
std::string Node::value(StringView name) const {
    const auto* p = find(name);
    return p && !p->values.empty() ? p->values.front() : std::string{};
}
namespace {
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

class Parser {
public:
    Parser(StringView text, const ParseLimits& limits) : text_(text), limits_(limits) {}
    Collection run() {
        if (text_.size() > limits_.max_bytes) fail("file exceeds the size limit");
        if (text_.substr(0, 3) == "\xef\xbb\xbf") pos_ = 3;
        Collection result;
        skip();
        while (peek() == '(') {
            Game game;
            tree(game, no_node, 1);
            result.games.push_back(std::move(game));
            skip();
        }
        if (result.games.empty()) fail("expected an SGF game tree");
        if (pos_ != text_.size()) fail("unexpected data after the game tree");
        return result;
    }
private:
    StringView text_;
    ParseLimits limits_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    void skip() { while (pos_ < text_.size() && whitespace(text_[pos_])) ++pos_; }
    [[noreturn]] void fail(const std::string& why) const {
        throw std::runtime_error("SGF byte " + std::to_string(pos_ + 1) + ": " + why);
    }
    void expect(char c) { skip(); if (peek() != c) fail(std::string("expected '") + c + "'"); ++pos_; }
    std::string value() {
        expect('[');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == ']') return out;
            if (c == '\0') fail("NUL byte in a property value");
            if (c == '\\') {
                if (pos_ == text_.size()) fail("unfinished escape");
                c = text_[pos_++];
                if (c == '\r' || c == '\n') {
                    if (pos_ < text_.size() && ((c == '\r' && peek() == '\n') || (c == '\n' && peek() == '\r'))) ++pos_;
                    continue;
                }
                if (c == '\0') fail("NUL byte in a property value");
            }
            out += c;
        }
        fail("unterminated property value");
    }
    std::size_t node(Game& game, std::size_t parent) {
        expect(';');
        if (++nodes_ > limits_.max_nodes) fail("too many nodes");
        const auto index = game.nodes.size();
        game.nodes.emplace_back();
        game.nodes[index].parent = parent;
        if (parent != no_node) game.nodes[parent].children.push_back(index);
        skip();
        while (letter(peek())) {
            std::string name;
            while (letter(peek())) {
                char c = text_[pos_++];
                // FF[3] identifiers may contain lowercase letters, ignored by FF[4].
                if (c >= 'A' && c <= 'Z') name += c;
            }
            if (name.empty()) fail("property has no uppercase identifier");
            if (game.nodes[index].find(name)) fail("duplicate property " + name);
            Property property{name, {}};
            skip();
            if (peek() != '[') fail("property " + name + " has no value");
            while (peek() == '[') { property.values.push_back(value()); skip(); }
            game.nodes[index].properties.push_back(std::move(property));
        }
        return index;
    }
    void tree(Game& game, std::size_t parent, std::size_t depth) {
        if (depth > limits_.max_depth) fail("variations are nested too deeply");
        expect('('); skip();
        if (peek() != ';') fail("a game tree must contain a node");
        while (peek() == ';') { parent = node(game, parent); skip(); }
        while (peek() == '(') { tree(game, parent, depth + 1); skip(); }
        expect(')');
    }
};
}

Collection parse_sgf(StringView input, const ParseLimits& limits) {
    return Parser(input, limits).run();
}

std::string game_text_sample(const Game& game) {
    std::string sample;
    for (const auto& node : game.nodes) {
        for (const auto& p : node.properties) {
            if (p.name != "C" && p.name != "GC" && p.name != "PB" && p.name != "PW" && p.name != "GN" && p.name != "EV" && p.name != "PC" && p.name != "AN" && p.name != "LB") continue;
            for (const auto& v : p.values) { sample += v; sample += '\n'; }
        }
        if (sample.size() >= 65536) break; // Keep complete values: never split a UTF-8 character.
    }
    return sample;
}
} // namespace erasgf
