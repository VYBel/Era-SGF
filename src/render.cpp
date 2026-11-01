#include "erasgf/render.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace erasgf {
namespace {
std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        const std::size_t n = b < 128 ? 1 : b >= 0xC2 && b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        if (i + n > text.size()) { result.push_back("?"); break; }
        result.push_back(text.substr(i, n)); i += n;
    }
    return result;
}
std::string shortened(const std::string& text, std::size_t columns) {
    auto chars = characters(text);
    std::string out;
    const auto count = std::min(chars.size(), columns);
    for (std::size_t i = 0; i < count; ++i) out += chars[i] == "\n" || chars[i] == "\r" || chars[i] == "\t" ? " " : chars[i];
    if (chars.size() > columns && columns) { out.clear(); for (std::size_t i = 0; i + 1 < columns; ++i) out += chars[i]; out += "…"; }
    return out;
}
void text(Scene& scene, const Rect& r, int font, const std::string& value, int color = 0, bool centered = false) {
    const auto cols = static_cast<std::size_t>(std::max(1, r.w * 10 / std::max(1, font * 6)));
    scene.drawing.push_back({DrawKind::text, r, color, -1, font, shortened(value, cols), centered});
}
void rectangle(Scene& scene, const Rect& r, int color = 0, int fill = -1) { scene.drawing.push_back({DrawKind::rectangle, r, color, fill, 0, {}}); }
void line(Scene& scene, int x, int y, int x2, int y2, int color = 0) { scene.drawing.push_back({DrawKind::line, {x, y, x2 - x, y2 - y}, color, -1, 0, {}}); }
void circle(Scene& scene, int x, int y, int radius, int color = 0, int fill = -1) { scene.drawing.push_back({DrawKind::circle, {x - radius, y - radius, 2 * radius, 2 * radius}, color, fill, 0, {}}); }
void button(Scene& scene, const Rect& bounds, Action action, const std::string& label, int font, bool enabled = true) {
    rectangle(scene, bounds, enabled ? 0 : 160, 255);
    text(scene, {bounds.x + font / 3, bounds.y, bounds.w - 2 * font / 3, bounds.h}, font, label, enabled ? 0 : 160, true);
    scene.controls.push_back({bounds, action});
}
std::string xml(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c >= 32 || c == '\n' || c == '\t' || c == '\r') out += static_cast<char>(c);
    }
    return out;
}
std::string color(int value) {
    std::ostringstream out; out << "rgb(" << value << ',' << value << ',' << value << ')'; return out.str();
}
}

std::vector<std::string> wrap_text(const std::string& value, std::size_t columns) {
    columns = std::max<std::size_t>(1, columns);
    std::vector<std::string> result;
    std::vector<std::string> word;
    std::string current;
    std::size_t length = 0;
    auto flush = [&] { result.push_back(current); current.clear(); length = 0; };
    auto put_word = [&] {
        if (word.empty()) return;
        if (length && length + 1 + word.size() > columns) flush();
        if (length) { current += ' '; ++length; }
        for (const auto& c : word) { if (length == columns) flush(); current += c; ++length; }
        word.clear();
    };
    const auto units = characters(value);
    for (std::size_t i = 0; i < units.size(); ++i) {
        const auto& c = units[i];
        if (c == "\r" || c == "\n") {
            put_word(); flush();
            if (i + 1 < units.size() && ((c == "\r" && units[i + 1] == "\n") || (c == "\n" && units[i + 1] == "\r"))) ++i;
        } else if (c == " " || c == "\t" || c == "\v" || c == "\f") put_word();
        else word.push_back(c);
    }
    put_word();
    if (!current.empty() || result.empty()) result.push_back(current);
    return result;
}

Scene make_scene(const ViewModel& model, int width, int height) {
    if (width < 320 || height < 320) throw std::runtime_error("Screen too small");
    Scene scene;
    const int small = std::min(width, height), margin = std::max(8, small / 60), font = std::max(16, small / 32);
    const int line_height = font * 3 / 2, header = font * 3 + margin * 2, controls_y = height - margin - font * 3;
    text(scene, {margin, margin, width - margin * 2, line_height}, font + font / 6, model.title);
    text(scene, {margin, margin + line_height, width - margin * 2, line_height}, font, model.subtitle);
    line(scene, margin, header - margin, width - margin, header - margin);
    const bool landscape = width > height * 115 / 100;
    if (model.full_comment) scene.comments = {margin, header, width - 2 * margin, controls_y - header - margin};
    else {
        const int side = landscape ? std::min(controls_y - header - margin, (width - 3 * margin) * 3 / 5)
                                   : std::min(width - 2 * margin, controls_y - header - font * 7 - 3 * margin);
        scene.board = {landscape ? margin : (width - side) / 2, header, side, side};
        scene.comments = landscape ? Rect{margin * 2 + side, header, width - side - margin * 3, controls_y - header - margin}
                                  : Rect{margin, header + side + margin, width - 2 * margin, controls_y - header - side - 2 * margin};
        const auto& board = scene.board;
        const int pad = font, grid = side - 2 * pad, n = model.position.size;
        const double step = static_cast<double>(grid) / (n - 1);
        auto x = [&](int col) { return board.x + pad + static_cast<int>(std::lround(col * step)); };
        auto y = [&](int row) { return board.y + pad + static_cast<int>(std::lround(row * step)); };
        for (int i = 0; i < n; ++i) {
            line(scene, x(0), y(i), x(n - 1), y(i)); line(scene, x(i), y(0), x(i), y(n - 1));
            const std::string col(1, static_cast<char>('A' + i + (i >= 8 ? 1 : 0)));
            text(scene, {x(i) - font / 3, board.y - font / 4, font, font}, font * 2 / 3, col);
            text(scene, {board.x, y(i) - font / 2, font, font}, font * 2 / 3, std::to_string(n - i));
        }
        const int edge = n == 9 ? 2 : 3, center = n / 2;
        for (int row : {edge, center, n - 1 - edge}) for (int col : {edge, center, n - 1 - edge}) circle(scene, x(col), y(row), std::max(2, font / 10), 0, 0);
        const int radius = std::max(3, static_cast<int>(step * .45));
        for (int row = 0; row < n; ++row) for (int col = 0; col < n; ++col) {
            const auto stone = model.position.at(col, row);
            if (stone != Stone::empty) circle(scene, x(col), y(row), radius, 0, stone == Stone::black ? 0 : 255);
        }
        if (model.position.last_move) {
            const auto p = *model.position.last_move;
            circle(scene, x(p.x), y(p.y), std::max(2, radius / 4), model.position.at(p.x, p.y) == Stone::black ? 255 : 0);
        }
        for (const auto& mark : model.position.marks) {
            const int cx = x(mark.point.x), cy = y(mark.point.y), r = std::max(3, radius / 2);
            const int ink = model.position.at(mark.point.x, mark.point.y) == Stone::black ? 255 : 0;
            if (mark.kind == MarkKind::circle) circle(scene, cx, cy, r, ink);
            else if (mark.kind == MarkKind::square) rectangle(scene, {cx - r, cy - r, 2 * r, 2 * r}, ink);
            else if (mark.kind == MarkKind::cross) { line(scene, cx - r, cy - r, cx + r, cy + r, ink); line(scene, cx + r, cy - r, cx - r, cy + r, ink); }
            else if (mark.kind == MarkKind::triangle) { line(scene, cx, cy - r, cx - r, cy + r, ink); line(scene, cx - r, cy + r, cx + r, cy + r, ink); line(scene, cx + r, cy + r, cx, cy - r, ink); }
            else {
                const int label_font = std::min(font, static_cast<int>(step * .8));
                if (model.position.at(mark.point.x, mark.point.y) == Stone::empty) rectangle(scene, {cx - radius, cy - radius, 2 * radius, 2 * radius}, 255, 255);
                text(scene, {cx - radius + 2, cy - radius, 2 * radius - 4, 2 * radius}, label_font, mark.label, ink, true);
            }
        }
        for (const auto& branch : model.branches) if (branch.point) {
            const int cx = x(branch.point->x), cy = y(branch.point->y);
            rectangle(scene, {cx - radius, cy - radius, radius * 2, radius * 2}, 0, 255);
            text(scene, {cx - radius + 2, cy - radius, radius * 2 - 4, radius * 2}, std::min(font, radius * 3 / 2), branch.label, 0, true);
        }
    }
    const auto& area = scene.comments;
    const int comment_font = font;
    const int text_y = area.y + line_height + margin / 2;
    const auto lines = wrap_text(model.comment.empty() ? "К этому ходу нет комментария." : model.comment, static_cast<std::size_t>(std::max(1, area.w * 10 / (comment_font * 6))));
    const std::size_t rows = static_cast<std::size_t>(std::max(1, (area.y + area.h - text_y) / line_height));
    scene.comment_pages = std::max<std::size_t>(1, (lines.size() + rows - 1) / rows);
    scene.comment_page = std::min(model.comment_page, scene.comment_pages - 1);
    const std::string caption = "Комментарий " + std::to_string(scene.comment_page + 1) + '/' + std::to_string(scene.comment_pages) + (model.full_comment ? " [доска]" : " [+]");
    text(scene, {area.x, area.y, area.w - font * 4, line_height}, font * 9 / 10, caption);
    scene.controls.push_back({{area.x, area.y, area.w - font * 4, line_height}, Action::toggle_comment});
    button(scene, {area.x + area.w - font * 4, area.y, font * 2, line_height}, Action::comment_previous, "<", font, scene.comment_page > 0);
    button(scene, {area.x + area.w - font * 2, area.y, font * 2, line_height}, Action::comment_next, ">", font, scene.comment_page + 1 < scene.comment_pages);
    for (std::size_t row = 0; row < rows && scene.comment_page * rows + row < lines.size(); ++row)
        text(scene, {area.x, text_y + static_cast<int>(row) * line_height, area.w, line_height}, comment_font, lines[scene.comment_page * rows + row]);
    const int gap = margin / 2, button_width = (width - margin * 2 - gap * 3) / 4;
    for (int i = 0; i < 4; ++i) {
        const Rect r{margin + i * (button_width + gap), controls_y, button_width, font * 3};
        if (i == 0) button(scene, r, model.full_comment ? Action::comment_previous : Action::previous, "Назад", font, model.full_comment ? scene.comment_page > 0 : model.has_previous);
        if (i == 1) button(scene, r, model.full_comment ? Action::comment_next : Action::next, "Вперёд", font, model.full_comment ? scene.comment_page + 1 < scene.comment_pages : model.has_next);
        if (i == 2) button(scene, r, model.full_comment ? Action::toggle_comment : Action::variations, model.full_comment ? "Доска" : "Ветки " + std::to_string(model.branches.size()), font);
        if (i == 3) button(scene, r, Action::menu, "Меню", font);
    }
    return scene;
}

std::string svg(const Scene& scene, int width, int height) {
    std::ostringstream out;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\"><title>Era-SGF desktop preview</title><rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    for (const auto& op : scene.drawing) {
        const auto& r = op.bounds;
        if (op.kind == DrawKind::line) out << "<line x1=\"" << r.x << "\" y1=\"" << r.y << "\" x2=\"" << r.x + r.w << "\" y2=\"" << r.y + r.h << "\" stroke=\"" << color(op.color) << "\"/>\n";
        else if (op.kind == DrawKind::text) out << "<text x=\"" << r.x + (op.centered ? r.w / 2 : 0) << "\" y=\"" << r.y + (r.h + op.font) / 2 - op.font / 6 << "\" text-anchor=\"" << (op.centered ? "middle" : "start") << "\" font-family=\"DejaVu Sans,sans-serif\" font-size=\"" << op.font << "\" fill=\"" << color(op.color) << "\">" << xml(op.text) << "</text>\n";
        else {
            if (op.kind == DrawKind::circle) out << "<circle cx=\"" << r.x + r.w / 2 << "\" cy=\"" << r.y + r.h / 2 << "\" r=\"" << r.w / 2 << '"';
            else out << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.w << "\" height=\"" << r.h << '"';
            out << " fill=\"" << (op.fill < 0 ? "none" : color(op.fill)) << "\" stroke=\"" << color(op.color) << "\"/>\n";
        }
    }
    return out.str() + "</svg>\n";
}
}
