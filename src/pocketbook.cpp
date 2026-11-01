#include <inkview.h>
#include "erasgf/encoding.hpp"
#include "erasgf/render.hpp"
#include "erasgf/storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>

using namespace erasgf;
namespace {
#ifndef ERA_SGF_VERSION
#define ERA_SGF_VERSION "dev"
#endif
constexpr const char* version = ERA_SGF_VERSION;
enum class Menu { main, files, variants, encodings, games };
struct Entry { std::string name; std::string path; bool directory; };
std::string basename(const std::string& path) { const auto slash = path.find_last_of('/'); return path.substr(slash == std::string::npos ? 0 : slash + 1); }
bool is_sgf(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
    return name.size() >= 4 && name.substr(name.size() - 4) == ".sgf";
}
int gray(int color) { return (color << 16) | (color << 8) | color; }
void menu_callback(int);

class Application {
public:
    std::string root, state_dir, launch_file, launch_action;
    explicit Application(std::string storage) : root(std::move(storage)), state_dir(root + "/system/config/era-sgf"), directory(root) {}
    ~Application() { for (auto& font : fonts) CloseFont(font.second); }

    int event(int type, int p1, int p2) {
        try {
            if (type == EVT_INIT) {
                SetPanelType(0);
                if (!launch_file.empty()) open_file(launch_file);
                else if (launch_action.empty()) { if (auto last = load_last_file(state_dir)) open_file(*last); }
                if (!launch_action.empty()) association(launch_action == "--install");
            } else if (type == EVT_SHOW) draw(true);
            else if (type == EVT_HIDE || type == EVT_EXIT) persist();
            else if (type == EVT_ORIENTATION) { SetOrientation(p1); draw(true); }
            else if (type == EVT_POINTERUP) {
                for (const auto& control : scene.controls) if (control.bounds.contains(p1, p2)) { act(control.action); break; }
            } else if (type == EVT_KEYPRESS || type == EVT_KEYREPEAT) {
                if (p1 == IV_KEY_NEXT || p1 == IV_KEY_NEXT2 || p1 == IV_KEY_RIGHT) act(full_comment ? Action::comment_next : Action::next);
                else if (p1 == IV_KEY_PREV || p1 == IV_KEY_PREV2 || p1 == IV_KEY_LEFT) act(full_comment ? Action::comment_previous : Action::previous);
                else if (p1 == IV_KEY_MENU || p1 == IV_KEY_OK) show_main();
                else if (p1 == IV_KEY_BACK) { if (full_comment) act(Action::toggle_comment); else { persist(); CloseApp(); } }
            }
        } catch (const std::exception& e) { error(e.what()); }
        return 0;
    }

    void select(int index) {
        try {
            if (!index) { draw(true); return; }
            if (mode == Menu::main) {
                if (index == 100) { page = 0; show_files(); return; }
                if (index == 102) { page = 0; show_games(); return; }
                if (index == 103) { page = 0; show_variants(); return; }
                if (index == 104 && viewer) { viewer->previous_branch(); changed(); }
                if (index == 105 && viewer) { viewer->begin(); changed(); }
                if (index == 106 && viewer) { viewer->end(); changed(); }
                if (index == 107) { show_encodings(); return; }
                if (index == 108 && viewer) { full_comment = true; information = true; comment_page = 0; }
                if (index == 109) { full_comment = !full_comment; information = false; comment_page = 0; }
                if (index == 110) { association(true); return; }
                if (index == 111) { association(false); return; }
                if (index == 112) { const int rotation[] = {1, 3, 0, 2}; SetOrientation(rotation[GetOrientation() & 3]); }
                if (index == 113) { persist(); CloseApp(); return; }
            } else if (mode == Menu::encodings && viewer) {
                if (index == 200) manual = "auto";
                else if (index >= 201 && static_cast<std::size_t>(index - 201) < supported_encodings().size()) manual = supported_encodings()[index - 201];
                resolve_encoding(); dirty = true;
            } else if (mode == Menu::files) {
                if (index == 1) {
                    if (directory != root) { const auto slash = directory.find_last_of('/'); directory = slash == 0 ? "/" : directory.substr(0, slash); }
                    page = 0; show_files(); return;
                }
                if (index == 2) { if (page) --page; show_files(); return; }
                if (index == 3) { ++page; show_files(); return; }
                const auto offset = page * page_size + static_cast<std::size_t>(index - 1000);
                if (index >= 1000 && offset < entries.size()) {
                    const auto selected = entries[offset];
                    if (selected.directory) { directory = selected.path; page = 0; show_files(); return; }
                    open_file(selected.path);
                }
            } else if (mode == Menu::variants && viewer) {
                if (index == 2) { if (page) --page; show_variants(); return; }
                if (index == 3) { ++page; show_variants(); return; }
                if (index >= 1000) { viewer->next(page * page_size + static_cast<std::size_t>(index - 1000)); changed(); }
            } else if (mode == Menu::games && viewer) {
                if (index == 2) { if (page) --page; show_games(); return; }
                if (index == 3) { ++page; show_games(); return; }
                if (index >= 1000) { viewer->select_game(page * page_size + static_cast<std::size_t>(index - 1000)); manual = "auto"; resolve_encoding(); changed(); }
            }
            draw(true);
        } catch (const std::exception& e) { error(e.what()); }
    }

private:
    static constexpr std::size_t page_size = 10;
    std::unique_ptr<Viewer> viewer;
    std::string file, source_id, manual = "auto", encoding_error, directory;
    EncodingChoice encoding{"UTF-8", false, ""};
    bool full_comment = false, information = false, dirty = false;
    std::size_t comment_page = 0, page = 0;
    unsigned refreshes = 0;
    Menu mode = Menu::main;
    Scene scene;
    std::vector<Entry> entries;
    std::vector<std::string> labels;
    std::vector<imenu> menu;
    std::map<int, ifont*> fonts;

    void error(const std::string& message) { Message(ICON_ERROR, "Era-SGF", message.c_str(), 6000); }
    void changed() { dirty = true; comment_page = 0; }
    std::string decode(const std::string& value) const {
        if (!encoding_error.empty()) return {};
        return decode_text(value, encoding.name);
    }
    void resolve_encoding() {
        encoding_error.clear();
        if (!viewer) return;
        try { encoding = choose_encoding(game_text_sample(viewer->game()), viewer->game().nodes[0].value("CA"), manual); }
        catch (const std::exception& e) { encoding_error = e.what(); encoding = {"", true, ""}; }
    }
    void persist() {
        if (!viewer || !dirty) return;
        save_state(state_dir, FileState{file, source_id, viewer->game_index(), viewer->node_index(), manual});
        dirty = false;
    }
    void open_file(const std::string& path) {
        const auto raw = read_file(path);
        auto next = std::make_unique<Viewer>(parse_sgf(raw));
        persist();
        auto restored = load_state(state_dir, path, raw);
        if (restored) {
            try { next->select_game(restored->game); next->select_node(restored->node); }
            catch (const std::exception&) { restored = none; next->select_game(0); }
        }
        viewer = std::move(next); file = path; source_id = fingerprint(raw);
        manual = restored ? restored->encoding : "auto";
        full_comment = false; information = false; comment_page = 0; dirty = true;
        resolve_encoding();
    }
    ViewModel model() {
        ViewModel value;
        value.title = std::string("Era-SGF ") + version;
        value.position.size = 9; value.position.cells.assign(81, Stone::empty);
        value.comment = "Открой запись через Меню → Открыть SGF.\nКнопки перелистывания меняют ход; варианты и кодировка доступны в меню.";
        if (viewer) {
            value.position = viewer->position();
            const auto& initial = viewer->game().nodes[0];
            const auto black = decode(initial.value("PB")), white = decode(initial.value("PW"));
            value.title = black.empty() && white.empty() ? basename(file) : black + " — " + white;
            value.subtitle = "Ход " + std::to_string(value.position.move_number) + " · " + (encoding_error.empty() ? encoding.name + (manual != "auto" ? " вручную" : encoding.uncertain ? " ?" : "") : "выбери кодировку");
            if (viewer->collection().games.size() > 1) value.subtitle += " · Партия " + std::to_string(viewer->game_index() + 1);
            if (!encoding_error.empty()) value.comment = encoding_error + "\n\nМеню → Кодировка: выбери кодировку этого файла.";
            else if (information) {
                value.comment.clear();
                for (const auto& key : {"GN", "PB", "BR", "PW", "WR", "DT", "EV", "RO", "PC", "KM", "RU", "RE", "SO", "AN", "GC"}) {
                    const auto raw = initial.value(key);
                    if (!raw.empty()) value.comment += std::string(key) + ": " + decode(raw) + "\n";
                }
            } else {
                value.comment = decode(viewer->node().value("C"));
                if (viewer->node_index() == 0 && !initial.value("GC").empty()) value.comment = decode(initial.value("GC")) + "\n\n" + value.comment;
                if (!viewer->node().value("N").empty()) value.comment = decode(viewer->node().value("N")) + "\n\n" + value.comment;
            }
            for (auto& mark : value.position.marks) mark.label = decode(mark.label);
            value.has_previous = viewer->node().parent != no_node; value.has_next = !viewer->node().children.empty();
            for (std::size_t i = 0; i < viewer->node().children.size(); ++i) {
                const auto& node = viewer->game().nodes[viewer->node().children[i]];
                const auto move = node.find("B") ? node.value("B") : node.value("W");
                Optional<Point> pt;
                if (viewer->node().children.size() > 1 && i < 10 && move.size() == 2 && move[0] >= 'a' && move[0] < 'a' + value.position.size && move[1] >= 'a' && move[1] < 'a' + value.position.size) pt = Point{move[0] - 'a', move[1] - 'a'};
                value.branches.push_back({pt, std::to_string(i + 1)});
            }
        } else value.subtitle = "Самостоятельный просмотр SGF · Меню → Открыть";
        value.full_comment = full_comment; value.comment_page = comment_page;
        return value;
    }
    ifont* font(int size) {
        if (fonts.count(size)) return fonts[size];
        auto* selected = OpenFont("LiberationSans", size, 1);
        if (!selected) selected = OpenFont("default", size, 1);
        if (!selected) throw std::runtime_error("Cannot open a font on this device");
        fonts[size] = selected; return selected;
    }
    static bool same(const DrawOp& a, const DrawOp& b) {
        return a.kind == b.kind && a.bounds.x == b.bounds.x && a.bounds.y == b.bounds.y && a.bounds.w == b.bounds.w && a.bounds.h == b.bounds.h && a.color == b.color && a.fill == b.fill && a.font == b.font && a.text == b.text && a.centered == b.centered;
    }
    void draw(bool full = false) {
        const int width = ScreenWidth(), height = ScreenHeight();
        ViewModel presentation;
        try { presentation = model(); }
        catch (const std::exception& e) {
            // Detection uses a bounded sample. A later text value may still fail;
            // preserve an operable screen so touch-only readers can select a codec.
            presentation.title = file.empty() ? "Era-SGF" : basename(file);
            presentation.subtitle = "Ошибка текста · Меню → Кодировка";
            presentation.comment = std::string(e.what()) + "\n\nМеню → Кодировка: выбери кодировку этого файла.";
            presentation.full_comment = full_comment;
            if (viewer) {
                presentation.position = viewer->position();
                for (auto& mark : presentation.position.marks) if (mark.kind == MarkKind::label) mark.label = "?";
                presentation.has_previous = viewer->node().parent != no_node;
                presentation.has_next = !viewer->node().children.empty();
            } else { presentation.position.size = 9; presentation.position.cells.assign(81, Stone::empty); }
        }
        Scene next = make_scene(presentation, width, height);
        int left = width, top = height, right = 0, bottom = 0;
        auto changed_bounds = [&](const Scene& a, const Scene& b) {
            for (const auto& op : a.drawing) if (std::none_of(b.drawing.begin(), b.drawing.end(), [&](const DrawOp& other) { return same(op, other); })) {
                left = std::min(left, std::min(op.bounds.x, op.bounds.x + op.bounds.w) - 2);
                top = std::min(top, std::min(op.bounds.y, op.bounds.y + op.bounds.h) - 2);
                right = std::max(right, std::max(op.bounds.x, op.bounds.x + op.bounds.w) + 2);
                bottom = std::max(bottom, std::max(op.bounds.y, op.bounds.y + op.bounds.h) + 2);
            }
        };
        changed_bounds(scene, next); changed_bounds(next, scene);
        ClearScreen();
        for (const auto& op : next.drawing) {
            const auto& r = op.bounds;
            if (op.kind == DrawKind::line) DrawLine(r.x, r.y, r.x + r.w, r.y + r.h, gray(op.color));
            else if (op.kind == DrawKind::rectangle) {
                if (op.fill >= 0) FillArea(r.x, r.y, r.w, r.h, gray(op.fill));
                DrawRect(r.x, r.y, r.w, r.h, gray(op.color));
            } else if (op.kind == DrawKind::circle) {
                const int radius = r.w / 2, cx = r.x + radius, cy = r.y + radius;
                if (op.fill >= 0) for (int y = -radius; y <= radius; ++y) {
                    const int half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - y * y)));
                    FillArea(cx - half, cy + y, half * 2 + 1, 1, gray(op.fill));
                }
                DrawCircle(cx, cy, radius, gray(op.color));
            } else {
                SetFont(font(op.font), gray(op.color));
                DrawTextRect(r.x, r.y, r.w, r.h, op.text.c_str(), (op.centered ? ALIGN_CENTER : ALIGN_LEFT) | VALIGN_MIDDLE);
            }
        }
        const bool first = scene.drawing.empty();
        scene = std::move(next); comment_page = scene.comment_page;
        if (full || first || ++refreshes >= 10) { FullUpdate(); refreshes = 0; }
        else if (left < right && top < bottom) {
            left = std::max(0, left); top = std::max(0, top);
            PartialUpdate(left, top, std::min(width, right) - left, std::min(height, bottom) - top);
        }
    }
    void show(Menu selected, const std::string& title, const std::vector<std::pair<int, std::string>>& options) {
        mode = selected; labels.clear(); menu.clear(); labels.reserve(options.size() + 1); menu.reserve(options.size() + 2);
        labels.push_back(title);
        for (const auto& option : options) labels.push_back(option.second);
        menu.push_back({ITEM_HEADER, 0, &labels[0][0], nullptr});
        for (std::size_t i = 0; i < options.size(); ++i) menu.push_back({ITEM_ACTIVE, static_cast<short>(options[i].first), &labels[i + 1][0], nullptr});
        menu.push_back({0, 0, nullptr, nullptr});
        OpenMenu(menu.data(), 0, ScreenWidth() / 10, ScreenHeight() / 10, menu_callback);
    }
    void show_main() {
        std::vector<std::pair<int, std::string>> options{{100, "Открыть SGF"}};
        if (viewer) {
            options.insert(options.end(), {{102, "Выбрать партию"}, {103, "Варианты продолжения"}, {104, "К предыдущей развилке"}, {105, "В начало"}, {106, "В конец выбранной ветки"}, {107, "Кодировка: " + (manual == "auto" ? "Авто" : manual)}, {108, "Информация о партии"}, {109, full_comment ? "Вернуться к доске" : "Комментарий на весь экран"}});
        }
        options.insert(options.end(), {{110, "Связать файлы .sgf с Era-SGF"}, {111, "Удалить ассоциацию Era-SGF"}, {112, "Повернуть экран"}, {113, "Выход"}});
        show(Menu::main, std::string("Era-SGF ") + version, options);
    }
    void pagers(std::vector<std::pair<int, std::string>>& options, std::size_t count) {
        if (page) options.push_back({2, "< Предыдущая страница"});
        if ((page + 1) * page_size < count) options.push_back({3, "Следующая страница >"});
    }
    void show_files() {
        entries.clear();
        DIR* dir = ::opendir(directory.c_str());
        if (!dir) throw std::runtime_error("Cannot open directory: " + directory);
        while (const auto* item = ::readdir(dir)) {
            const std::string name = item->d_name;
            if (name.empty() || name[0] == '.') continue;
            const std::string path = directory + '/' + name;
            struct stat info{};
            if (::stat(path.c_str(), &info) != 0) continue;
            const bool folder = S_ISDIR(info.st_mode);
            if (folder || (S_ISREG(info.st_mode) && is_sgf(name))) entries.push_back({name, path, folder});
            if (entries.size() > 10000) { ::closedir(dir); throw std::runtime_error("Too many files in this directory"); }
        }
        ::closedir(dir);
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.directory != b.directory ? a.directory > b.directory : a.name < b.name; });
        std::vector<std::pair<int, std::string>> options;
        if (directory != root) options.push_back({1, "[..]"});
        page = std::min(page, entries.empty() ? std::size_t{0} : (entries.size() - 1) / page_size);
        for (std::size_t i = page * page_size; i < std::min(entries.size(), (page + 1) * page_size); ++i) options.push_back({1000 + static_cast<int>(i % page_size), entries[i].directory ? '[' + entries[i].name + ']' : entries[i].name});
        pagers(options, entries.size());
        show(Menu::files, directory, options);
    }
    void show_encodings() {
        if (!viewer) return;
        std::vector<std::pair<int, std::string>> options{{200, "Авто (CA и определение кодировки)"}};
        for (std::size_t i = 0; i < supported_encodings().size(); ++i) options.push_back({201 + static_cast<int>(i), supported_encodings()[i]});
        show(Menu::encodings, "Кодировка текущей партии", options);
    }
    void show_variants() {
        if (!viewer) return;
        const auto& children = viewer->node().children;
        std::vector<std::pair<int, std::string>> options;
        for (std::size_t i = page * page_size; i < std::min(children.size(), (page + 1) * page_size); ++i) {
            const auto& n = viewer->game().nodes[children[i]];
            std::string title = std::to_string(i + 1) + ". " + (n.find("B") ? "B[" + n.value("B") + ']' : n.find("W") ? "W[" + n.value("W") + ']' : "Комментарий/позиция");
            const auto note = !n.value("N").empty() ? n.value("N") : n.value("C");
            if (!note.empty() && encoding_error.empty()) { const auto lines = wrap_text(decode(note), 36); title += " — " + lines[0]; }
            options.push_back({1000 + static_cast<int>(i % page_size), title});
        }
        pagers(options, children.size());
        show(Menu::variants, children.empty() ? "Продолжений нет" : "Варианты продолжения", options);
    }
    void show_games() {
        if (!viewer) return;
        std::vector<std::pair<int, std::string>> options;
        for (std::size_t i = page * page_size; i < std::min(viewer->collection().games.size(), (page + 1) * page_size); ++i) {
            const auto& game = viewer->collection().games[i];
            std::string title = "Партия " + std::to_string(i + 1);
            try { const auto e = choose_encoding(game_text_sample(game), game.nodes[0].value("CA")); const auto name = game.nodes[0].value("GN"); if (!name.empty()) title += ": " + decode_text(name, e.name); } catch (const std::exception&) {}
            options.push_back({1000 + static_cast<int>(i % page_size), title});
        }
        pagers(options, viewer->collection().games.size()); show(Menu::games, "Партии в SGF", options);
    }
    void association(bool install) {
        if (install) {
            struct stat info{};
            if (::stat((root + "/applications/era-sgf.app").c_str(), &info) != 0 || !S_ISREG(info.st_mode)) throw std::runtime_error("Copy era-sgf.app to applications/ before registering SGF files");
        }
        const auto result = update_association(root + "/system/config/extensions.cfg", install);
        std::string message = result.changed ? (install ? "Ассоциация .sgf добавлена. Закрой и открой библиотеку книги." : "Обработчик Era-SGF удалён. Другие ассоциации сохранены.") : "Настройки уже соответствуют выбранному действию.";
        if (!result.backup.empty()) message += "\nРезервная копия: " + result.backup;
        Message(ICON_INFORMATION, "Era-SGF", message.c_str(), 6000);
    }
    void act(Action action) {
        if (action == Action::menu) { show_main(); return; }
        if (action == Action::variations) { page = 0; show_variants(); return; }
        if (action == Action::toggle_comment) { full_comment = !full_comment; information = false; comment_page = 0; }
        else if (action == Action::comment_previous) { if (comment_page) --comment_page; }
        else if (action == Action::comment_next) { if (comment_page + 1 < scene.comment_pages) ++comment_page; }
        else if (viewer && action == Action::next) { if (viewer->next()) changed(); }
        else if (viewer && action == Action::previous) { if (viewer->previous()) changed(); }
        draw();
    }
};

std::unique_ptr<Application> application;
void menu_callback(int index) { application->select(index); }
int handler(int type, int p1, int p2) { return application->event(type, p1, p2); }
}

int main(int argc, char** argv) {
    const char* configured_root = std::getenv("ERA_SGF_ROOT");
    application = std::make_unique<Application>(configured_root && *configured_root ? configured_root : "/mnt/ext1");
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--install" || argument == "--uninstall") application->launch_action = argument;
        else if (application->launch_file.empty()) application->launch_file = argument;
    }
    InkViewMain(handler);
    application.reset();
    return 0;
}
