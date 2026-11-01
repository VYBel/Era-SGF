#include "inkview.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool closed = false;
int orientation = 0;
iv_menuhandler selection = nullptr;
std::vector<std::string> frame;
void emit() { std::cout << "FRAME\n"; for (const auto& s : frame) std::cout << s << '\n'; std::cout << "END_FRAME\n"; }
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
        else if (event == "hide") handler(EVT_HIDE, 0, 0);
        else if (event == "show") handler(EVT_SHOW, 0, 0);
        else if (event == "close") closed = true;
    }
    handler(EVT_EXIT, 0, 0);
}
void CloseApp() { closed = true; }
void SetPanelType(int) {}
int ScreenWidth() { return orientation == 1 ? 1680 : 1264; }
int ScreenHeight() { return orientation == 1 ? 1264 : 1680; }
int GetOrientation() { return orientation; }
void SetOrientation(int value) { orientation = value; }
void ClearScreen() { frame.clear(); }
void DrawLine(int, int, int, int, int) {}
void DrawRect(int, int, int, int, int) {}
void FillArea(int, int, int, int, int) {}
void DrawCircle(int, int, int, int) {}
ifont* OpenFont(const char*, int size, int) { return new ifont{size}; }
void CloseFont(ifont* font) { delete font; }
void SetFont(const ifont*, int) {}
char* DrawTextRect(int, int, int, int, const char* text, int) { frame.emplace_back(text); return nullptr; }
void FullUpdate() { emit(); }
void PartialUpdate(int, int, int, int) { emit(); }
void OpenMenu(imenu* menu, int, int, int, iv_menuhandler callback) {
    selection = callback; std::cout << "MENU\n";
    for (auto p = menu; p && p->type; ++p) std::cout << p->index << ':' << p->text << '\n';
}
void Message(int, const char* title, const char* text, int) { std::cout << "MESSAGE " << title << ": " << text << '\n'; }
