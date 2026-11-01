#pragma once

#include "erasgf/sgf.hpp"
#include <optional>

namespace erasgf {
enum class Stone { empty, black, white };
struct Point { int x = 0; int y = 0; };
enum class MarkKind { triangle, square, circle, cross, label };
struct Mark { Point point; MarkKind kind; std::string label; };
struct Position {
    int size = 19;
    std::vector<Stone> cells;
    int black_captures = 0;
    int white_captures = 0;
    int move_number = 0;
    std::optional<Point> last_move;
    std::vector<Mark> marks;
    Stone at(int x, int y) const;
};

Position replay(const Game& game, std::size_t node);

class Viewer {
public:
    explicit Viewer(Collection collection);
    const Collection& collection() const { return collection_; }
    const Game& game() const { return collection_.games.at(game_); }
    const Node& node() const { return game().nodes.at(node_); }
    std::size_t game_index() const { return game_; }
    std::size_t node_index() const { return node_; }
    void select_game(std::size_t index);
    void select_node(std::size_t index);
    bool next(std::size_t variation = no_node);
    bool previous();
    void begin();
    void end();
    bool previous_branch();
    Position position() const { return replay(game(), node_); }
private:
    Collection collection_;
    std::size_t game_ = 0;
    std::size_t node_ = 0;
    std::vector<std::size_t> chosen_;
};
} // namespace erasgf
