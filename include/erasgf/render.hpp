#pragma once

#include "erasgf/viewer.hpp"

namespace erasgf {
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
enum class DrawKind { line, rectangle, circle, text };
struct DrawOp {
    DrawKind kind;
    Rect bounds;
    int color = 0;
    int fill = -1;
    int font = 16;
    std::string text;
    bool centered = false;
};
enum class Action { previous, next, variations, menu, comment_previous, comment_next, toggle_comment };
struct Control { Rect bounds; Action action; };
struct BranchHint { Optional<Point> point; std::string label; };
struct ViewModel {
    Position position;
    std::string title = "Era-SGF";
    std::string subtitle;
    std::string comment;
    std::vector<BranchHint> branches;
    std::size_t comment_page = 0;
    bool full_comment = false;
    bool has_previous = false;
    bool has_next = false;
};
struct Scene {
    std::vector<DrawOp> drawing;
    std::vector<Control> controls;
    Rect board;
    Rect comments;
    std::size_t comment_pages = 1;
    std::size_t comment_page = 0;
};
Scene make_scene(const ViewModel& model, int width, int height);
std::vector<std::string> wrap_text(const std::string& text, std::size_t columns);
std::string svg(const Scene& scene, int width, int height);
} // namespace erasgf
