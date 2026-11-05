#include "inkview.h"
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
bool closed = false;
bool auto_keylock = true, auto_poweroff = true, active = true, locked = false;
int orientation = 0;
iv_menuhandler selection = nullptr;
iv_dialoghandler confirmation = nullptr;
iv_keyboardhandler keyboard = nullptr;
std::vector<std::string> frame;
struct TextHit { int x, y, w, h; std::string text; };
std::vector<TextHit> text_hits;
std::vector<unsigned char> pixels;
struct Timer { iv_timerproc callback; long long deadline; };
std::map<std::string, Timer> timers;
long long now = 0;
bool fake_fat = false, deny_chmod = false, deny_state_writes = false;
void advance(long long milliseconds) {
    const auto until = now + milliseconds;
    unsigned calls = 0;
    while (!timers.empty()) {
        auto next = timers.begin();
        for (auto it = timers.begin(); it != timers.end(); ++it) if (it->second.deadline < next->second.deadline) next = it;
        if (next->second.deadline > until) break;
        const auto timer = next->second; timers.erase(next); now = timer.deadline;
        timer.callback(); if (++calls > 10000) throw std::runtime_error("Runaway timer in UI test");
    }
    now = until;
}
void pixel(int x, int y, int color) {
    if (x >= 0 && x < ScreenWidth() && y >= 0 && y < ScreenHeight())
        pixels[static_cast<std::size_t>(y) * ScreenWidth() + x] = static_cast<unsigned char>(color & 255);
}
void emit() {
    std::cout << "FRAME\n"; for (const auto& s : frame) std::cout << s << '\n'; std::cout << "END_FRAME\n";
    if (const auto* name = std::getenv("ERA_SGF_TEST_FRAME")) {
        std::ofstream out(name, std::ios::binary);
        out << "P5\n" << ScreenWidth() << ' ' << ScreenHeight() << "\n255\n";
        out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }
}
}
void InkViewMain(iv_handler handler) {
    handler(EVT_INIT, 0, 0); handler(EVT_SHOW, 0, 0);
    const char* input = std::getenv("ERA_SGF_TEST_EVENTS");
    std::istringstream events(input ? input : ""); std::string event;
    while (!closed && std::getline(events, event, ';')) {
        if (event == "next") handler(EVT_KEYPRESS, IV_KEY_NEXT, 0);
        else if (event == "previous") handler(EVT_KEYPRESS, IV_KEY_PREV, 0);
        else if (event == "menu") handler(EVT_KEYPRESS, IV_KEY_MENU, 0);
        else if (event == "back") handler(EVT_KEYPRESS, IV_KEY_BACK, 0);
        else if (event == "rotate") { orientation = 1; handler(EVT_ORIENTATION, 1, 0); }
        else if (event.rfind("select:", 0) == 0 && selection) { auto callback = selection; selection = nullptr; callback(std::stoi(event.substr(7))); }
        else if (event.rfind("confirm:", 0) == 0 && confirmation) { auto callback = confirmation; confirmation = nullptr; callback(std::stoi(event.substr(8))); }
        else if (event.rfind("text:", 0) == 0 && keyboard) {
            auto callback = keyboard; keyboard = nullptr;
            const auto text = event.substr(5); std::vector<char> buffer(text.begin(), text.end()); buffer.push_back(0); callback(buffer.data());
        }
        else if (event == "cancel-text" && keyboard) { auto callback = keyboard; keyboard = nullptr; callback(nullptr); }
        else if (event.rfind("tap:", 0) == 0) { const auto comma = event.find(','); handler(EVT_POINTERUP, std::stoi(event.substr(4, comma - 4)), std::stoi(event.substr(comma + 1))); }
        else if (event.rfind("tap-text:", 0) == 0) {
            const auto label = event.substr(9); bool found = false;
            for (const auto& item : text_hits) if (item.text.compare(0, label.size(), label) == 0) {
                const int x = item.x + item.w / 2, y = item.y + item.h / 2;
                handler(EVT_POINTERUP, x, y); found = true; break;
            }
            if (!found) throw std::runtime_error("No rendered text to tap: " + label);
        }
        else if (event == "hide") { active = false; handler(EVT_HIDE, 0, 0); }
        else if (event == "background") { active = false; handler(EVT_BACKGROUND, 0, 0); }
        else if (event == "show") { active = true; locked = false; handler(EVT_SHOW, 0, 0); }
        else if (event == "foreground") { active = true; locked = false; handler(EVT_FOREGROUND, 0, 0); }
        // Firmware may lose an armed timer across sleep, or lock before HIDE arrives.
        else if (event == "drop-timers") timers.clear();
        else if (event == "lock") locked = true;
        else if (event == "policy") std::cout << "POWER_POLICY " << auto_keylock << ' ' << auto_poweroff << " TIMERS " << timers.size() << '\n';
        else if (event.rfind("wait:", 0) == 0) advance(std::stoll(event.substr(5)));
        else if (event == "replace") {
            const auto* path = std::getenv("ERA_SGF_TEST_REPLACE_FILE");
            const auto* bytes = std::getenv("ERA_SGF_TEST_REPLACE_BYTES");
            if (path && bytes) { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << bytes; }
        }
        else if (event == "fat-chmod-failure") { fake_fat = true; deny_chmod = true; }
        else if (event == "posix-chmod-failure") { fake_fat = false; deny_chmod = true; }
        else if (event == "state-write-failure") deny_state_writes = true;
        else if (event == "close") closed = true;
    }
    handler(EVT_EXIT, 0, 0);
    std::cout << "TIMERS_ACTIVE " << timers.size() << '\n';
    std::cout << "POWER_POLICY_EXIT " << auto_keylock << ' ' << auto_poweroff << '\n';
}
void CloseApp() { closed = true; std::cout << "APP_CLOSED\n"; }
void SetPanelType(int) {}
int ScreenWidth() { return orientation == 1 ? 1680 : 1264; }
int ScreenHeight() { return orientation == 1 ? 1264 : 1680; }
int GetOrientation() { return orientation; }
void SetOrientation(int value) { orientation = value; }
void ClearScreen() { frame.clear(); text_hits.clear(); pixels.assign(static_cast<std::size_t>(ScreenWidth()) * ScreenHeight(), 255); }
void DrawLine(int x, int y, int x2, int y2, int color) {
    const int dx = std::abs(x2 - x), dy = -std::abs(y2 - y), sx = x < x2 ? 1 : -1, sy = y < y2 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x, y, color); if (x == x2 && y == y2) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
void DrawRect(int x, int y, int w, int h, int color) {
    DrawLine(x, y, x + w - 1, y, color); DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    DrawLine(x, y, x, y + h - 1, color); DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}
void FillArea(int x, int y, int w, int h, int color) {
    for (int row = y; row < y + h; ++row) for (int col = x; col < x + w; ++col) pixel(col, row, color);
}
void DrawCircle(int x, int y, int radius, int color) {
    // InkView B288 draws a filled disc, rather than an outline.
    for (int row = -radius; row <= radius; ++row) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - row * row)));
        FillArea(x - half, y + row, 2 * half + 1, 1, color);
    }
}
ifont* OpenFont(const char*, int size, int) { return new ifont{size}; }
void CloseFont(ifont* font) { delete font; }
void SetFont(const ifont*, int) {}
char* DrawTextRect(int x, int y, int w, int h, const char* text, int) { frame.emplace_back(text); text_hits.push_back({x,y,w,h,text}); return nullptr; }
void FullUpdate() { std::cout << "UPDATE_FULL\n"; emit(); }
void PartialUpdate(int, int, int, int) { std::cout << "UPDATE_PARTIAL\n"; emit(); }
void OpenMenu(imenu* menu, int, int, int, iv_menuhandler callback) {
    selection = callback; std::cout << "MENU\n";
    for (auto p = menu; p && p->type; ++p) std::cout << p->index << ':' << p->text << '\n';
}

void Message(int, const char* title, const char* text, int) { std::cout << "MESSAGE " << title << ": " << text << '\n'; }
void Dialog(int, const char* title, const char* text, const char* yes, const char* no, iv_dialoghandler callback) {
    confirmation = callback; std::cout << "DIALOG " << title << ':' << text << ':' << yes << ':' << no << '\n';
}
void OpenKeyboard(const char* title, char*, int, int, iv_keyboardhandler callback) {
    keyboard = callback; std::cout << "KEYBOARD " << title << '\n';
}
void SetHardTimer(const char* name, iv_timerproc callback, int milliseconds) {
    ClearTimer(callback); timers[name] = {callback, now + milliseconds};
}
void ClearTimer(iv_timerproc callback) {
    for (auto it = timers.begin(); it != timers.end();) {
        if (it->second.callback == callback) it = timers.erase(it); else ++it;
    }
}
void ForcingSleep() {}
void SetAutoKeylock(bool enabled) { auto_keylock = enabled; }
void SetAutoPowerOff(bool enabled) { auto_poweroff = enabled; }
int IsTaskActive() { return active; }
int get_keylock() { return locked; }
ibitmap* BitmapFromScreen(int x, int y, int width, int height) {
    auto* bitmap = static_cast<ibitmap*>(std::malloc(sizeof(ibitmap) + static_cast<std::size_t>(width) * height));
    if (!bitmap) return nullptr;
    bitmap->width = width; bitmap->height = height; bitmap->depth = 8; bitmap->scanline = width;
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col)
        bitmap->data[row * width + col] = pixels[(y + row) * ScreenWidth() + x + col];
    return bitmap;
}
int SaveBitmap(const char* path, const ibitmap* bitmap) {
    if (!bitmap || std::getenv("ERA_SGF_TEST_SAVE_FAIL")) return 0;
    const unsigned stride = (bitmap->width + 3U) & ~3U, offset = 54 + 256 * 4;
    std::vector<unsigned char> data(offset + stride * bitmap->height, 0);
    auto put = [&](unsigned at, unsigned value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) data[at + i] = static_cast<unsigned char>(value >> (i * 8));
    };
    data[0] = 'B'; data[1] = 'M'; put(2, data.size(), 4); put(10, offset, 4); put(14, 40, 4);
    put(18, bitmap->width, 4); put(22, bitmap->height, 4); put(26, 1, 2); put(28, 8, 2);
    put(34, stride * bitmap->height, 4); put(46, 256, 4);
    for (unsigned i = 0; i < 256; ++i) for (unsigned c = 0; c < 3; ++c) data[54 + 4 * i + c] = static_cast<unsigned char>(i);
    for (unsigned row = 0; row < bitmap->height; ++row) for (unsigned col = 0; col < bitmap->width; ++col)
        data[offset + (bitmap->height - 1 - row) * stride + col] = bitmap->data[row * bitmap->scanline + col];
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good() ? 1 : 0;
}

// Characterize the FAT capability mismatch and real write failures without
// changing production APIs or requiring a privileged loopback mount in tests.
extern "C" int fchmod(int fd, mode_t mode) noexcept {
    if (deny_chmod) { errno = EPERM; return -1; }
    return static_cast<int>(::syscall(SYS_fchmod, fd, mode));
}
extern "C" int fstatfs(int fd, struct statfs* info) noexcept {
    const auto result = static_cast<int>(::syscall(SYS_fstatfs, fd, info));
    if (!result && fake_fat) info->f_type = 0x4d44;
    return result;
}
extern "C" ssize_t write(int fd, const void* data, size_t size) {
    if (deny_state_writes) {
        char link[64], path[4096];
        std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        const auto count = ::readlink(link, path, sizeof(path) - 1);
        if (count >= 0) {
            path[count] = 0;
            if (std::strstr(path, "/system/config/era-sgf/") && std::strstr(path, ".era-sgf-")) { errno = EIO; return -1; }
        }
    }
    return static_cast<ssize_t>(::syscall(SYS_write, fd, data, size));
}
