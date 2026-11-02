#include "erasgf/logos.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace erasgf {
const std::vector<std::string>& logo_names() {
    static const std::vector<std::string> names{"Два камня", "Лист", "Равновесие", "Печать го", "Книга", "Крыло", "Путь хода"};
    return names;
}
std::vector<DrawOp> logo_mark(int choice, const Rect& bounds) {
    if (choice < 1 || choice > 7) throw std::runtime_error("Unknown logo choice");
    std::vector<DrawOp> out;
    auto x = [&](double v) { return bounds.x + static_cast<int>(std::lround(v * bounds.w / 1000)); };
    auto y = [&](double v) { return bounds.y + static_cast<int>(std::lround(v * bounds.h / 1000)); };
    auto disc = [&](int cx, int cy, int radius, int color) {
        const int r = std::max(1, radius * std::min(bounds.w, bounds.h) / 1000);
        out.push_back({DrawKind::circle, {x(cx) - r, y(cy) - r, 2 * r, 2 * r}, color, color, 0, {}});
    };
    auto ring = [&](int cx, int cy, int radius) { disc(cx, cy, radius, 0); disc(cx, cy, radius - 24, 255); };
    auto stroke = [&](double ax, double ay, double bx, double by, int weight = 18) {
        const int x1 = x(ax), y1 = y(ay), x2 = x(bx), y2 = y(by);
        const int width = std::max(1, weight * std::min(bounds.w, bounds.h) / 1000);
        for (int delta = -width / 2; delta <= width / 2; ++delta) {
            const int dx = std::abs(y2 - y1) > std::abs(x2 - x1) ? delta : 0;
            const int dy = dx ? 0 : delta;
            out.push_back({DrawKind::line, {x1 + dx, y1 + dy, x2 - x1, y2 - y1}, 0, -1, 0, {}});
        }
    };
    auto curve = [&](double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy, int weight = 18) {
        double px = ax, py = ay;
        for (int i = 1; i <= 32; ++i) {
            const double t = i / 32., s = 1 - t;
            const double nx = s*s*s*ax + 3*s*s*t*bx + 3*s*t*t*cx + t*t*t*dx;
            const double ny = s*s*s*ay + 3*s*s*t*by + 3*s*t*t*cy + t*t*t*dy;
            stroke(px, py, nx, ny, weight); px = nx; py = ny;
        }
    };
    auto square = [&](int left, int top, int side, int weight) {
        stroke(left, top, left + side, top, weight); stroke(left + side, top, left + side, top + side, weight);
        stroke(left + side, top + side, left, top + side, weight); stroke(left, top + side, left, top, weight);
    };
    if (choice == 1) {
        for (int p : {250, 500, 750}) { stroke(p, 170, p, 830, 12); stroke(170, p, 830, p, 12); }
        disc(370, 620, 225, 0); ring(650, 350, 205);
    } else if (choice == 2) {
        curve(235, 725, 120, 390, 345, 150, 790, 205, 27);
        curve(790, 205, 825, 620, 560, 820, 235, 725, 27);
        curve(180, 815, 390, 610, 530, 430, 715, 285, 25);
        stroke(370, 620, 345, 390, 17); stroke(495, 490, 690, 490, 17);
        disc(725, 745, 128, 0);
    } else if (choice == 3) {
        stroke(280, 380, 500, 795, 60); stroke(500, 795, 720, 380, 60);
        disc(280, 245, 112, 0); ring(720, 245, 112);
    } else if (choice == 4) {
        square(160, 160, 680, 32);
        for (int p : {330, 500, 670}) { stroke(p, 205, p, 795, 13); stroke(205, p, 795, p, 13); }
        disc(330, 670, 113, 0); ring(670, 330, 113);
    } else if (choice == 5) {
        curve(155, 290, 275, 245, 390, 270, 500, 360, 28);
        curve(500, 360, 610, 270, 725, 245, 845, 290, 28);
        stroke(155, 290, 155, 725, 28); stroke(845, 290, 845, 725, 28);
        curve(155, 725, 310, 685, 405, 750, 500, 805, 28);
        curve(500, 805, 595, 750, 690, 685, 845, 725, 28);
        stroke(500, 360, 500, 805, 24);
        disc(320, 500, 102, 0); ring(680, 500, 102);
    } else if (choice == 6) {
        curve(270, 720, 300, 500, 470, 260, 765, 180, 66);
        curve(370, 760, 500, 510, 650, 390, 845, 350, 58);
        curve(480, 805, 615, 635, 760, 555, 865, 545, 50);
        disc(250, 765, 120, 0); ring(735, 760, 95);
    } else {
        curve(250, 210, 960, 180, 40, 825, 750, 790, 30);
        disc(250, 210, 95, 0); ring(500, 500, 115); disc(750, 790, 95, 0);
    }
    return out;
}
Scene launch_scene(int choice, const std::string& version, int width, int height) {
    Scene scene; const int small = std::min(width, height), side = small * 2 / 5;
    const int title_font = std::max(24, small / 16), font = std::max(16, small / 32);
    const Rect mark{(width - side) / 2, (height - side) / 2 - title_font, side, side};
    scene.drawing = logo_mark(choice, mark);
    scene.drawing.push_back({DrawKind::text, {0, mark.y + side + font, width, title_font * 3 / 2}, 0, -1, title_font, "Era-SGF", true});
    scene.drawing.push_back({DrawKind::text, {0, mark.y + side + font + title_font * 3 / 2, width, font * 2}, 0, -1, font, "Версия " + version, true});
    return scene;
}
} // namespace erasgf
