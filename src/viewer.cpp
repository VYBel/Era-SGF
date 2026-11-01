#include "erasgf/viewer.hpp"
#include <algorithm>
#include <array>

namespace erasgf {
Stone Position::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= size || y >= size) throw std::out_of_range("board coordinate");
    return cells.at(static_cast<std::size_t>(y * size + x));
}

namespace {
int integer(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    if (s.size() > 5 || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) throw std::runtime_error("Invalid SGF integer: " + s);
    return std::stoi(s);
}
Point point(std::string_view s, int size) {
    if (s.size() != 2 || s[0] < 'a' || s[1] < 'a' || s[0] >= 'a' + size || s[1] >= 'a' + size) throw std::runtime_error("Point outside the board: " + std::string(s));
    return {s[0] - 'a', s[1] - 'a'};
}
std::vector<Point> points(std::string_view s, int size) {
    if (s.size() == 2) return {point(s, size)};
    if (s.size() != 5 || s[2] != ':') throw std::runtime_error("Invalid SGF point list");
    Point a = point(s.substr(0, 2), size), b = point(s.substr(3, 2), size);
    if (a.x > b.x || a.y > b.y) throw std::runtime_error("Reversed setup rectangle");
    std::vector<Point> result;
    for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) result.push_back({x, y});
    return result;
}
std::vector<int> neighbors(int index, int size) {
    std::vector<int> result;
    const int x = index % size, y = index / size;
    if (x) result.push_back(index - 1);
    if (x + 1 < size) result.push_back(index + 1);
    if (y) result.push_back(index - size);
    if (y + 1 < size) result.push_back(index + size);
    return result;
}
std::pair<std::vector<int>, bool> group(const Position& p, int start) {
    std::array<bool, 361> seen{};
    std::vector<int> stones{start}; seen[start] = true;
    bool liberty = false;
    for (std::size_t i = 0; i < stones.size(); ++i) {
        for (int n : neighbors(stones[i], p.size)) {
            if (p.cells[n] == Stone::empty) liberty = true;
            else if (p.cells[n] == p.cells[start] && !seen[n]) { seen[n] = true; stones.push_back(n); }
        }
    }
    return {stones, liberty};
}
void move(Position& p, std::string_view s, Stone color) {
    ++p.move_number; p.last_move.reset();
    if (s.empty() || s == "tt") return;
    const Point pt = point(s, p.size);
    const int index = pt.y * p.size + pt.x;
    if (p.cells[index] != Stone::empty) throw std::runtime_error("Move on an occupied intersection");
    p.cells[index] = color; p.last_move = pt;
    const Stone opponent = color == Stone::black ? Stone::white : Stone::black;
    for (int n : neighbors(index, p.size)) if (p.cells[n] == opponent) {
        const auto g = group(p, n);
        if (!g.second) {
            for (int sidx : g.first) p.cells[sidx] = Stone::empty;
            (color == Stone::black ? p.black_captures : p.white_captures) += static_cast<int>(g.first.size());
        }
    }
    const auto own = group(p, index);
    if (!own.second) {
        for (int sidx : own.first) p.cells[sidx] = Stone::empty;
        (color == Stone::black ? p.white_captures : p.black_captures) += static_cast<int>(own.first.size());
        p.last_move.reset();
    }
}
}

Position replay(const Game& game, std::size_t node) {
    if (game.nodes.empty() || node >= game.nodes.size()) throw std::out_of_range("SGF node");
    const Node& root = game.nodes[0];
    if (integer(root.value("GM"), 1) != 1) throw std::runtime_error("Only Go records (GM[1]) are supported");
    std::string sz = root.value("SZ");
    const auto colon = sz.find(':');
    if (colon != std::string::npos) {
        if (sz.substr(0, colon) != sz.substr(colon + 1)) throw std::runtime_error("Rectangular boards are not supported");
        sz.resize(colon);
    }
    Position p; p.size = integer(sz, 19);
    if (p.size != 9 && p.size != 13 && p.size != 19) throw std::runtime_error("Supported board sizes: 9, 13, 19");
    p.cells.assign(static_cast<std::size_t>(p.size * p.size), Stone::empty);
    std::vector<std::size_t> path;
    for (auto n = node; n != no_node; n = game.nodes.at(n).parent) {
        if (path.size() >= game.nodes.size()) throw std::runtime_error("Cycle in SGF tree");
        path.push_back(n);
    }
    std::reverse(path.begin(), path.end());
    for (auto n : path) {
        const auto& current = game.nodes[n];
        for (const auto& name : {"AE", "AB", "AW"}) if (const auto* prop = current.find(name)) {
            for (const auto& value : prop->values) for (Point q : points(value, p.size))
                p.cells[q.y * p.size + q.x] = name[1] == 'E' ? Stone::empty : name[1] == 'B' ? Stone::black : Stone::white;
        }
        if (current.find("B") && current.find("W")) throw std::runtime_error("Both move colors in one SGF node");
        for (const auto& name : {"B", "W"}) if (const auto* prop = current.find(name)) {
            if (prop->values.size() != 1) throw std::runtime_error("A move must have one value");
            move(p, prop->values[0], name[0] == 'B' ? Stone::black : Stone::white);
        }
    }
    const auto& selected = game.nodes[node];
    const std::pair<const char*, MarkKind> kinds[] = {{"TR", MarkKind::triangle}, {"SQ", MarkKind::square}, {"CR", MarkKind::circle}, {"MA", MarkKind::cross}};
    for (const auto& kind : kinds) if (const auto* prop = selected.find(kind.first))
        for (const auto& value : prop->values) for (Point q : points(value, p.size)) p.marks.push_back({q, kind.second, {}});
    if (const auto* labels = selected.find("LB")) for (const auto& value : labels->values) {
        if (value.size() < 3 || value[2] != ':') throw std::runtime_error("Invalid board label");
        p.marks.push_back({point(std::string_view(value).substr(0, 2), p.size), MarkKind::label, value.substr(3)});
    }
    return p;
}

Viewer::Viewer(Collection c) : collection_(std::move(c)) { select_game(0); }
void Viewer::select_game(std::size_t index) {
    const auto& g = collection_.games.at(index);
    (void)replay(g, 0);
    game_ = index; node_ = 0; chosen_.assign(g.nodes.size(), 0);
}
void Viewer::select_node(std::size_t index) {
    (void)replay(game(), index); // Validate before mutating visible state.
    auto child = index;
    while (game().nodes[child].parent != no_node) {
        const auto parent = game().nodes[child].parent;
        const auto& children = game().nodes[parent].children;
        chosen_[parent] = static_cast<std::size_t>(std::find(children.begin(), children.end(), child) - children.begin());
        child = parent;
    }
    node_ = index;
}
bool Viewer::next(std::size_t variation) {
    if (node().children.empty()) return false;
    if (variation == no_node) variation = chosen_[node_];
    const auto target = node().children.at(variation);
    select_node(target); return true;
}
bool Viewer::previous() {
    if (node().parent == no_node) return false;
    node_ = node().parent; return true;
}
void Viewer::begin() { node_ = 0; }
void Viewer::end() { while (next()) {} }
bool Viewer::previous_branch() {
    auto target = node().parent;
    if (target == no_node) return false;
    while (game().nodes[target].parent != no_node && game().nodes[target].children.size() < 2) target = game().nodes[target].parent;
    node_ = target; return true;
}
} // namespace erasgf
