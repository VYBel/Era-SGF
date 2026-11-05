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
void line(Scene& scene, int x, int y, int x2, int y2, int color = 0, int stroke = 1) { scene.drawing.push_back({DrawKind::line, {x, y, x2 - x, y2 - y}, color, -1, 0, {}, false, stroke}); }
void circle(Scene& scene, int x, int y, int radius, int color = 0, int fill = -1, int stroke = 1) { scene.drawing.push_back({DrawKind::circle, {x - radius, y - radius, 2 * radius, 2 * radius}, color, fill, 0, {}, false, stroke}); }
void button(Scene& scene, const Rect& bounds, Action action, const std::string& label, int font, bool enabled = true) {
    rectangle(scene, bounds, enabled ? 0 : 160, 255);
    text(scene, {bounds.x + font / 3, bounds.y, bounds.w - 2 * font / 3, bounds.h}, font, label, enabled ? 0 : 160, true);
    if (enabled) scene.controls.push_back({bounds, action});
}
// Draw arrows geometrically so firmware font glyphs cannot alter their shape or spacing.
void navigation_button(Scene& scene, const Rect& bounds, Action action, int font, bool enabled) {
    font = std::min(font, std::max(5, bounds.w / 3));
    button(scene, bounds, action, "", font, enabled);
    const int color = enabled ? 0 : 160, stroke = std::max(1, font / 12);
    const bool left = action == Action::first || action == Action::back_ten;
    const bool jump = action == Action::back_ten || action == Action::forward_ten;
    const int arrow = std::max(4, font * 3 / 4), space = std::max(2, font / 3);
    const int extra = jump ? font * 3 / 2 : stroke;
    const int start = bounds.x + (bounds.w - arrow - space - extra) / 2;
    const int ax = start + (left && !jump ? extra + space : !left && jump ? extra + space : 0);
    const int cy = bounds.y + bounds.h / 2, tip = left ? ax : ax + arrow;
    line(scene, ax, cy, ax + arrow, cy, color, stroke);
    line(scene, tip, cy, tip + (left ? 1 : -1) * arrow / 3, cy - arrow / 3, color, stroke);
    line(scene, tip, cy, tip + (left ? 1 : -1) * arrow / 3, cy + arrow / 3, color, stroke);
    const int ex = left && jump ? ax + arrow + space : !left && !jump ? ax + arrow + space : start;
    if (jump) text(scene, {ex, bounds.y, extra, bounds.h}, font, "10", color, true);
    else line(scene, ex, cy - arrow / 2, ex, cy + arrow / 2, color, stroke);
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
    const int small = std::min(width, height), margin = std::max(8, small / 80), font = std::max(16, small / 32);
    const int line_height = font * 3 / 2, header = line_height * (model.subtitle.empty() ? 1 : 2) + margin * 2, controls_y = height - margin - font * 3;
    const int title_x = model.has_game ? margin * 2 + line_height : margin;
    const int title_width = width - title_x - margin * 2 - line_height;
    if (model.has_game) button(scene, {margin, margin, line_height, line_height}, Action::information, "i", font);
    text(scene, {title_x, margin, title_width, line_height}, std::max(16, std::min(font + font / 6, title_width * 10 / std::max(1, static_cast<int>(characters(model.title).size()) * 6))), model.title);
    const Rect refresh_button{width - margin - line_height, margin, line_height, line_height};
    button(scene, refresh_button, Action::refresh, "", font);
    const int cx = refresh_button.x + line_height / 2, cy = margin + line_height / 2, r = std::max(5, font / 3);
    circle(scene, cx, cy, r, 0, -1, std::max(2, font / 12));
    for (int dy = -r / 2; dy <= r / 2; ++dy)
        rectangle(scene, {cx, cy - r + dy, r / 2 - std::abs(dy) + 1, 1}, 0, 0);
    if (!model.subtitle.empty()) text(scene, {margin, margin + line_height, width - margin * 2, line_height}, font, model.subtitle);
    line(scene, margin, header - margin, width - margin, header - margin);
    const bool landscape = width > height * 115 / 100;
    const int navigation_height = std::max(36, font * 2);
    const int result_height = model.result_caption.empty() ? 0 : line_height;
    if (model.full_comment) scene.comments = {margin, header, width - 2 * margin, controls_y - header - margin};
    else {
        const int comment_reserve = model.comment.empty() ? 0 : line_height * 2 + margin;
        const int side = landscape ? std::min(controls_y - header - navigation_height - result_height - margin, (width - 3 * margin) * 3 / 5)
                                   : std::min(width - 2 * margin, controls_y - header - navigation_height - result_height - comment_reserve - 2 * margin);
        scene.board = {landscape ? margin : (width - side) / 2, header, side, side};
        scene.comments = landscape ? Rect{margin * 2 + side, header, width - side - margin * 3, controls_y - header - margin}
                                  : Rect{margin, header + side + navigation_height + result_height + margin, width - 2 * margin, controls_y - header - side - navigation_height - result_height - 2 * margin};
        const int small_button = std::max(1, side * 16 / 100);
        const int jump_button = std::max(1, side * 18 / 100), gap = std::max(2, side / 60);
        const int nav_font = std::min(font, std::max(10, side / 28));
        const int number_width = side - small_button * 2 - jump_button * 2 - gap * 4;
        const int nav_y = header + side;
        int nav_x = scene.board.x;
        navigation_button(scene, {nav_x, nav_y, small_button, navigation_height}, Action::first, nav_font, model.has_game && model.has_previous); nav_x += small_button + gap;
        navigation_button(scene, {nav_x, nav_y, jump_button, navigation_height}, Action::back_ten, nav_font, model.has_game && model.has_previous); nav_x += jump_button + gap;
        const std::string move_caption = number_width < nav_font * 6 ? std::to_string(model.position.move_number)
            : model.move_caption.empty() ? "Ход " + std::to_string(model.position.move_number) : model.move_caption;
        text(scene, {nav_x, nav_y, number_width, navigation_height},
             std::max(10, std::min(nav_font, number_width * 10 / std::max(1, static_cast<int>(characters(move_caption).size()) * 6))), move_caption, 0, true);
        if (model.has_game) scene.controls.push_back({{nav_x, nav_y, number_width, navigation_height}, Action::go_to_move});
        nav_x += number_width + gap;
        navigation_button(scene, {nav_x, nav_y, jump_button, navigation_height}, Action::forward_ten, nav_font, model.has_game && model.has_next); nav_x += jump_button + gap;
        navigation_button(scene, {nav_x, nav_y, small_button, navigation_height}, Action::last, nav_font, model.has_game && model.has_next);
        if (result_height) text(scene, {scene.board.x, nav_y + navigation_height, side, result_height}, nav_font, model.result_caption, 0, true);
        const auto& board = scene.board;
        const int pad = font, grid = side - 2 * pad, n = model.position.size;
        const double step = static_cast<double>(grid) / (n - 1);
        auto x = [&](int col) { return board.x + pad + static_cast<int>(std::lround(col * step)); };
        auto y = [&](int row) { return board.y + pad + static_cast<int>(std::lround(row * step)); };
        const int grid_stroke = std::max(2, small / 420);
        for (int i = 0; i < n; ++i) {
            line(scene, x(0), y(i), x(n - 1), y(i), 0, grid_stroke); line(scene, x(i), y(0), x(i), y(n - 1), 0, grid_stroke);
            const std::string col(1, static_cast<char>('A' + i + (i >= 8 ? 1 : 0)));
            text(scene, {x(i) - font / 3, board.y - font / 4, font, font}, font * 2 / 3, col);
            text(scene, {board.x, y(i) - font / 2, font, font}, font * 2 / 3, std::to_string(n - i));
        }
        const int edge = n == 9 ? 2 : 3, center = n / 2;
        // Star points must remain distinct from the thicker high-contrast grid.
        const int hoshi_radius = std::max(grid_stroke * 2, static_cast<int>(std::lround(step * .09)));
        for (int row : {edge, center, n - 1 - edge}) for (int col : {edge, center, n - 1 - edge}) circle(scene, x(col), y(row), hoshi_radius, 0, 0);
        const int radius = std::max(3, static_cast<int>(step * .45));
        for (int row = 0; row < n; ++row) for (int col = 0; col < n; ++col) {
            const auto stone = model.position.at(col, row);
            if (stone != Stone::empty) circle(scene, x(col), y(row), radius, 0, stone == Stone::black ? 0 : 255, std::max(2, small / 300));
        }
        if (model.position.last_move) {
            const auto p = *model.position.last_move;
            circle(scene, x(p.x), y(p.y), std::max(3, radius / 3), model.position.at(p.x, p.y) == Stone::black ? 255 : 0, model.position.at(p.x, p.y) == Stone::black ? 255 : 0);
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
    const auto lines = wrap_text(model.comment, static_cast<std::size_t>(std::max(1, area.w * 10 / (comment_font * 6))));
    const bool paginated = lines.size() > static_cast<std::size_t>(std::max(1, area.h / line_height));
    const int text_y = area.y + (paginated ? line_height : 0);
    const std::size_t rows = static_cast<std::size_t>(std::max(1, (area.y + area.h - text_y) / line_height));
    scene.comment_pages = std::max<std::size_t>(1, (lines.size() + rows - 1) / rows);
    scene.comment_page = std::min(model.comment_page, scene.comment_pages - 1);
    if (!model.comment.empty()) {
        if (paginated) {
            const std::string caption = std::to_string(scene.comment_page + 1) + '/' + std::to_string(scene.comment_pages);
            text(scene, {area.x, area.y, area.w - font * 4, line_height}, font * 9 / 10, caption);
            button(scene, {area.x + area.w - font * 4, area.y, font * 2, line_height}, Action::comment_previous, "<", font, scene.comment_page > 0);
            button(scene, {area.x + area.w - font * 2, area.y, font * 2, line_height}, Action::comment_next, ">", font, scene.comment_page + 1 < scene.comment_pages);
        }
        scene.controls.push_back({{area.x, text_y, area.w, std::max(line_height, area.h - (text_y - area.y))}, Action::toggle_comment});
        for (std::size_t row = 0; row < rows && scene.comment_page * rows + row < lines.size(); ++row)
            text(scene, {area.x, text_y + static_cast<int>(row) * line_height, area.w, line_height}, comment_font, lines[scene.comment_page * rows + row]);
    }
    const int gap = margin / 2, button_width = (width - margin * 2 - gap * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        const Rect r{margin + i * (button_width + gap), controls_y, button_width, font * 3};
        if (i == 0) button(scene, r, model.full_comment ? Action::comment_previous : Action::previous, "Назад", font, model.full_comment ? scene.comment_page > 0 : model.has_previous);
        if (i == 1) button(scene, r, model.full_comment ? Action::comment_next : Action::next, "Вперёд", font, model.full_comment ? scene.comment_page + 1 < scene.comment_pages : model.has_next);
        if (i == 2) button(scene, r, model.full_comment ? Action::toggle_comment : Action::variations, model.full_comment ? (model.about ? "Закрыть" : "Доска") : "Ветки", font, model.full_comment || model.branches.size() > 1);
        if (i == 3) button(scene, r, Action::autoplay, model.autoplay ? "Стоп" : "Авто", font, model.has_game && !model.full_comment && (model.autoplay || (model.has_next && model.branches.size() == 1)));
        if (i == 4) button(scene, r, Action::menu, "Меню", font);
    }
    return scene;
}

namespace {
// Shared by every application menu and the library. Header title stays centered
// between equal-size Back and Close hit areas, independently of its text length.
void screen_header(Scene& scene, int width, int margin, int font, int size,
                   const std::string& title, Action action) {
    const Rect back{margin, margin, size, size}, close{width - margin - size, margin, size, size};
    for (const auto& item : {std::make_pair(back, 900), std::make_pair(close, 901)}) {
        rectangle(scene, item.first, 0, 255);
        scene.controls.push_back({item.first, action, item.second});
    }
    const int cy = margin + size / 2, cx = back.x + size / 2;
    const int a = font / 2, stroke = std::max(2, font / 12), ccx = close.x + size / 2;
    line(scene, cx - a, cy, cx + a, cy, 0, stroke);
    line(scene, cx - a, cy, cx, cy - a, 0, stroke);
    line(scene, cx - a, cy, cx, cy + a, 0, stroke);
    line(scene, ccx - a, cy - a, ccx + a, cy + a, 0, stroke);
    line(scene, ccx - a, cy + a, ccx + a, cy - a, 0, stroke);
    const Rect title_box{2 * margin + size, margin, width - 4 * margin - 2 * size, size};
    const int title_font = std::max(font * 3 / 4, std::min(font, title_box.w * 10 /
        std::max(1, static_cast<int>(characters(title).size()) * 6)));
    text(scene, title_box, title_font, title, 0, true);
}
struct MenuLayout {
    int margin, font, button, top, row, footer;
    MenuLayout(int width, int height) {
        if (width < 320 || height < 320) throw std::runtime_error("Screen too small");
        const int small = std::min(width, height);
        margin = std::max(8, small / 80); font = std::max(14, small / 36);
        button = font * 3; top = margin * 3 + button;
        row = font * 3 + margin; footer = height - margin - button;
    }
};
struct LibraryLayout {
    int margin, font, button, rows_top, row_height, footer;
    LibraryLayout(int width, int height) {
        if (width < 320 || height < 320) throw std::runtime_error("Screen too small");
        const int small = std::min(width, height);
        margin = std::max(8, small / 80); font = std::max(14, small / 36);
        button = font * 3; row_height = font * 3 + margin * 2;
        rows_top = margin * 4 + button * 3 + std::max(40, font * 3 / 2);
        footer = height - margin - button;
    }
};
}
std::size_t menu_page_capacity(int width, int height) {
    const MenuLayout l(width, height);
    return static_cast<std::size_t>(std::max(1, (l.footer - l.margin - l.top) / l.row));
}
Scene make_menu_scene(const MenuViewModel& model, int width, int height) {
    const MenuLayout l(width, height);
    Scene scene;
    screen_header(scene, width, l.margin, l.font, l.button, model.title, Action::menu_command);
    const auto capacity = menu_page_capacity(width, height);
    const auto pages = std::max<std::size_t>(1, (model.options.size() + capacity - 1) / capacity);
    const auto page = std::min(model.page, pages - 1), begin = page * capacity;
    for (std::size_t i = begin; i < std::min(model.options.size(), begin + capacity); ++i) {
        const Rect row{l.margin, l.top + static_cast<int>(i - begin) * l.row, width - 2 * l.margin, l.row};
        const bool focused = static_cast<int>(i) == model.focus;
        if (focused) rectangle(scene, row, 0, 0);
        text(scene, {row.x + l.font, row.y, row.w - 3 * l.font, row.h}, l.font, model.options[i].second, focused ? 255 : 0);
        if (model.options[i].first == model.checked) {
            const int x = row.x + row.w - l.font * 3 / 2, y = row.y + row.h / 2, a = l.font / 3;
            line(scene, x - a, y, x, y + a, focused ? 255 : 0, 3);
            line(scene, x, y + a, x + 2 * a, y - a, focused ? 255 : 0, 3);
        }
        line(scene, row.x, row.y + row.h - 1, row.x + row.w - 1, row.y + row.h - 1, 160);
        scene.controls.push_back({row, Action::menu_command, model.options[i].first});
    }
    if (model.options.empty()) text(scene, {l.margin, l.top, width - 2 * l.margin, l.row}, l.font, "Список пуст", 0, true);
    if (pages > 1) {
        for (int i = 0; i < 2; ++i) {
            const Rect r{i ? width - l.margin - l.button * 2 : l.margin, l.footer, l.button * 2, l.button};
            const bool enabled = i ? page + 1 < pages : page > 0;
            button(scene, r, Action::menu_command, "", l.font, false);
            if (enabled) scene.controls.push_back({r, Action::menu_command, i ? 903 : 902});
            const int x = r.x + r.w / 2, y = r.y + r.h / 2, a = l.font / 2, ink = enabled ? 0 : 160;
            line(scene, x - a, y, x + a, y, ink, 2);
            line(scene, x + (i ? a : -a), y, x, y - a, ink, 2);
            line(scene, x + (i ? a : -a), y, x, y + a, ink, 2);
        }
        text(scene, {l.margin + l.button * 2, l.footer, width - 2 * l.margin - 4 * l.button, l.button}, l.font,
             std::to_string(page + 1) + " / " + std::to_string(pages), 0, true);
    }
    return scene;
}
std::size_t library_page_capacity(int width, int height) {
    const LibraryLayout l(width, height);
    return static_cast<std::size_t>(std::max(1, (l.footer - l.margin - l.rows_top) / l.row_height));
}
Scene make_library_scene(const LibraryViewModel& model, int width, int height) {
    const LibraryLayout l(width, height);
    Scene scene;
    auto hit = [&](const Rect& bounds, int command) { scene.controls.push_back({bounds, Action::library_command, command}); };
    auto control = [&](const Rect& bounds, int command, const std::string& label, bool enabled = true, bool selected = false) {
        rectangle(scene, bounds, enabled ? 0 : 160, selected ? 0 : 255);
        text(scene, {bounds.x + l.margin / 2, bounds.y, bounds.w - l.margin, bounds.h}, l.font, label, selected ? 255 : enabled ? 0 : 160, true);
        if (enabled) hit(bounds, command);
    };
    screen_header(scene, width, l.margin, l.font, l.button, "Библиотека", Action::library_command);
    const int a = l.font / 2, stroke = std::max(2, l.font / 12);
    const int info_y = l.margin + l.button;
    text(scene, {l.margin, info_y, width - 3 * l.margin - l.button, std::max(40, l.font * 3 / 2)}, l.font * 4 / 5,
         "Найдено: " + std::to_string(model.matching) + " из " + std::to_string(model.total));
    const Rect tools{width - l.margin - l.button, info_y, l.button, std::max(40, l.font * 3 / 2)};
    control(tools, 15, "");
    for (int i = -1; i <= 1; ++i)
        circle(scene, tools.x + tools.w / 2 + i * l.font / 2, tools.y + tools.h / 2, std::max(2, l.font / 12), 0, 0);
    const int tabs_y = info_y + std::max(40, l.font * 3 / 2) + l.margin, tab_width = (width - 4 * l.margin) / 3;
    const char* tabs[] = {"Все", "Новые", "Просмотренные"};
    for (int i = 0; i < 3; ++i) control({l.margin + i * (tab_width + l.margin), tabs_y, tab_width, l.button}, 20 + i, tabs[i], true, model.status == i);
    const int search_y = tabs_y + l.button + l.margin, search_width = (width - 3 * l.margin) * 3 / 5;
    control({l.margin, search_y, search_width, l.button}, 12, model.query.empty() ? "Поиск по партиям" : "Поиск: " + model.query);
    control({l.margin * 2 + search_width, search_y, width - search_width - 3 * l.margin, l.button}, 11, "Год: " + (model.year.empty() ? "все" : model.year == "unknown" ? "без даты" : model.year));
    const auto count = std::min(model.rows.size(), library_page_capacity(width, height));
    for (std::size_t i = 0; i < count; ++i) {
        const int y = l.rows_top + static_cast<int>(i) * l.row_height;
        const Rect row{l.margin, y, width - l.margin * 2, l.row_height};
        hit(row, 1000 + static_cast<int>(i));
        text(scene, {row.x + l.margin, y, row.w - 2 * l.margin, l.font * 3 / 2}, l.font, model.rows[i].title);
        text(scene, {row.x + l.margin, y + l.font * 3 / 2, row.w - 2 * l.margin, l.font * 3 / 2}, l.font * 4 / 5, model.rows[i].detail);
        line(scene, row.x, y + row.h - 1, row.x + row.w, y + row.h - 1, 160);
    }
    if (model.rows.empty()) {
        const std::string message = model.total ? "По этим условиям партий нет" : "В библиотеке пока нет партий";
        text(scene, {l.margin, l.rows_top, width - 2 * l.margin, l.row_height}, l.font, message, 0, true);
    }
    const int nav_width = (width - 5 * l.margin) / 7;
    const Rect previous{l.margin, l.footer, nav_width, l.button};
    const Rect next{l.margin * 3 + nav_width * 3, l.footer, nav_width, l.button};
    for (const auto& item : {std::make_pair(previous, 2), std::make_pair(next, 3)}) {
        const auto& r = item.first; const bool left = item.second == 2;
        const bool enabled = left ? model.page > 0 : model.page + 1 < model.pages;
        control(r, item.second, "", enabled);
        const int x = r.x + r.w / 2, y = r.y + r.h / 2, ink = enabled ? 0 : 160;
        line(scene, x - a, y, x + a, y, ink, stroke);
        line(scene, x + (left ? -a : a), y, x, y - a, ink, stroke);
        line(scene, x + (left ? -a : a), y, x, y + a, ink, stroke);
    }
    text(scene, {l.margin * 2 + nav_width, l.footer, nav_width * 2, l.button}, l.font,
         std::to_string(model.page + 1) + " / " + std::to_string(model.pages), 0, true);
    const int random_x = next.x + next.w + l.margin;
    control({random_x, l.footer, width - random_x - l.margin, l.button}, 140, "Случайная новая");
    return scene;
}

std::string svg(const Scene& scene, int width, int height) {
    std::ostringstream out;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\"><title>Era-SGF desktop preview</title><rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    for (const auto& op : scene.drawing) {
        const auto& r = op.bounds;
        if (op.kind == DrawKind::line) out << "<line x1=\"" << r.x << "\" y1=\"" << r.y << "\" x2=\"" << r.x + r.w << "\" y2=\"" << r.y + r.h << "\" stroke=\"" << color(op.color) << "\" stroke-width=\"" << op.stroke_width << "\"/>\n";
        else if (op.kind == DrawKind::text) out << "<text x=\"" << r.x + (op.centered ? r.w / 2 : 0) << "\" y=\"" << r.y + (r.h + op.font) / 2 - op.font / 6 << "\" text-anchor=\"" << (op.centered ? "middle" : "start") << "\" font-family=\"DejaVu Sans,sans-serif\" font-size=\"" << op.font << "\" fill=\"" << color(op.color) << "\">" << xml(op.text) << "</text>\n";
        else {
            if (op.kind == DrawKind::circle) out << "<circle cx=\"" << r.x + r.w / 2 << "\" cy=\"" << r.y + r.h / 2 << "\" r=\"" << r.w / 2 << '"';
            else out << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.w << "\" height=\"" << r.h << '"';
            out << " fill=\"" << (op.fill < 0 ? "none" : color(op.fill)) << "\" stroke=\"" << color(op.color) << "\" stroke-width=\"" << op.stroke_width << "\"/>\n";
        }
    }
    return out.str() + "</svg>\n";
}
}
