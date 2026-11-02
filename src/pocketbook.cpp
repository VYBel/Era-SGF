#include <inkview.h>
#include "erasgf/encoding.hpp"
#include "erasgf/library.hpp"
#include "erasgf/logos.hpp"
#include "erasgf/render.hpp"
#include "erasgf/png.hpp"
#include "erasgf/storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

using namespace erasgf;
namespace {
#ifndef ERA_SGF_VERSION
#define ERA_SGF_VERSION "dev"
#endif
constexpr const char* version = ERA_SGF_VERSION;
enum class Menu { main, files, variants, encodings, games, library, library_filters, library_years, pauses, logos, game_actions, file_actions, settings, associations, navigation, refresh_settings };
struct Entry { std::string name; std::string path; bool directory; };
std::string basename(const std::string& path) { const auto slash = path.find_last_of('/'); return path.substr(slash == std::string::npos ? 0 : slash + 1); }
bool is_sgf(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
    return name.size() >= 4 && name.substr(name.size() - 4) == ".sgf";
}
int gray(int color) { return (color << 16) | (color << 8) | color; }
void menu_callback(int);
void confirm_callback(int);
void search_callback(char*);
void move_callback(char*);
void autoplay_callback();
void startup_callback();

class Application {
public:
    std::string root, state_dir, launch_file, launch_action;
    explicit Application(std::string storage) : root(std::move(storage)), state_dir(root + "/system/config/era-sgf"), library(root, state_dir), directory(root) {}
    ~Application() { for (auto& font : fonts) CloseFont(font.second); }

    int event(int type, int p1, int p2) {
        try {
            if (type == EVT_INIT) {
                load_preferences();
                splash_visible = launch_action.empty();
                SetPanelType(0);
                if (!launch_file.empty()) open_file(launch_file);
                else if (launch_action.empty()) { if (auto last = load_last_file(state_dir)) open_file(*last); }
                if (!launch_action.empty()) association(launch_action == "--install");
            } else if (type == EVT_SHOW) { if (splash_visible) show_splash(); else draw(true); }
            else if (type == EVT_EXIT) finish(false);
            else if (type == EVT_HIDE || type == EVT_BACKGROUND) { stop_autoplay(); ClearTimer(startup_callback); splash_visible = false; persist(); }
            else if (type == EVT_ORIENTATION) { stop_autoplay(); ClearTimer(startup_callback); splash_visible = false; SetOrientation(p1); draw(true); }
            else if (type == EVT_POINTERUP) {
                if (splash_visible) { finish_startup(); return 0; }
                for (const auto& control : scene.controls) if (control.bounds.contains(p1, p2)) { act(control.action); break; }
            } else if (type == EVT_KEYPRESS || type == EVT_KEYREPEAT) {
                if (splash_visible) { finish_startup(); return 0; }
                if (p1 == IV_KEY_NEXT || p1 == IV_KEY_NEXT2 || p1 == IV_KEY_RIGHT) act(full_comment ? Action::comment_next : Action::next);
                else if (p1 == IV_KEY_PREV || p1 == IV_KEY_PREV2 || p1 == IV_KEY_LEFT) act(full_comment ? Action::comment_previous : Action::previous);
                else if (p1 == IV_KEY_MENU || p1 == IV_KEY_OK) show_main();
                else if (p1 == IV_KEY_BACK) { if (full_comment) act(Action::toggle_comment); else finish(true); }
            }
        } catch (const std::exception& e) { error(e.what()); }
        if (!autoplay) ForcingSleep();
        return 0;
    }

    void select(int index) {
        try {
            if (!index || index == 900) { menu_back(); return; }
            if (std::none_of(menu.begin(), menu.end(), [&](const imenu& item) { return item.type == ITEM_ACTIVE && item.index == index; })) { draw(true); return; }
            if (action_menu()) {
                if (index == 130) { show_game_actions(); return; }
                if (index == 131) { show_file_actions(); return; }
                if (index == 132) { show_settings(); return; }
                if (index == 133) { show(Menu::associations, "Файлы SGF", {{110, "Связать .sgf с Era-SGF"}, {111, "Удалить ассоциацию Era-SGF"}}); return; }
                if (index == 139) { stop_autoplay(); about = true; full_comment = true; information = false; comment_page = 0; draw(true); return; }
                if (index == 138) { show_refresh(); return; }
                if (index == 134) { show_navigation(); return; }
                if (index == 135) { open_move_keyboard(); return; }
                if (index == 136) { act(Action::back_ten); return; }
                if (index == 137) { act(Action::forward_ten); return; }
                if (index == 100) { page = 0; show_files(); return; }
                if (index == 102) { page = 0; show_games(); return; }
                if (index == 103) { page = 0; show_variants(); return; }
                if (index == 104 && viewer) { full_comment = false; information = false; if (viewer->previous_branch()) changed(); }
                if (index == 105 && viewer) { act(Action::first); return; }
                if (index == 106 && viewer) { act(Action::last); return; }
                if (index == 107) { show_encodings(); return; }
                if (index == 108 && viewer) { full_comment = true; information = true; comment_page = 0; }
                if (index == 109) { full_comment = !full_comment; information = false; comment_page = 0; }
                if (index == 110) { association(true); return; }
                if (index == 111) { association(false); return; }
                if (index == 112) { const int rotation[] = {1, 3, 0, 2}; SetOrientation(rotation[GetOrientation() & 3]); }
                if (index == 113) { finish(true); return; }
                if (index == 114) { page = 0; show_library(); return; }
                if (index == 115) { toggle_autoplay(); draw(); return; }
                if (index == 116) { show_pauses(); return; }
                if (index == 117 && viewer) { screenshot(); return; }
                if (index == 118) { show_logos(); return; }
                if ((index == 120 || index == 121) && viewer) { library.set_viewed(file, index == 120); }
                if ((index == 122 || index == 123) && viewer) { request_file_action(index); return; }
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
            } else if (mode == Menu::library) {
                if (index == 10) { show_filters(); return; }
                if (index == 11) { page = 0; show_years(); return; }
                if (index == 12) {
                    std::strncpy(search_buffer, library_filter.query.c_str(), sizeof(search_buffer) - 1);
                    search_buffer[sizeof(search_buffer) - 1] = 0;
                    OpenKeyboard("Игрок, турнир, дата или имя файла", search_buffer, sizeof(search_buffer) - 1, 0, search_callback); return;
                }
                if (index == 13) { library.refresh(); page = 0; show_library(); return; }
                if (index == 14) { library_filter.query.clear(); page = 0; show_library(); return; }
                if (index == 2) { if (page) --page; show_library(); return; }
                if (index == 3) { ++page; show_library(); return; }
                const auto offset = page * page_size + static_cast<std::size_t>(index - 1000);
                if (index >= 1000 && offset < library_rows.size()) open_file(root + '/' + library.records()[library_rows[offset]].path);
            } else if (mode == Menu::library_filters) {
                if (index >= 20 && index <= 22) library_filter.status = index == 20 ? ReadFilter::all : index == 21 ? ReadFilter::new_only : ReadFilter::viewed_only;
                page = 0; show_library(); return;
            } else if (mode == Menu::library_years) {
                if (index == 1) { library_filter.year.clear(); page = 0; show_library(); return; }
                if (index == 2) { if (page) --page; show_years(); return; }
                if (index == 3) { ++page; show_years(); return; }
                const auto offset = page * page_size + static_cast<std::size_t>(index - 1000);
                if (index >= 1000 && offset < year_rows.size()) library_filter.year = year_rows[offset];
                page = 0; show_library(); return;
            } else if (mode == Menu::refresh_settings) {
                if (index >= 500 && index < 505) { refresh_interval = refresh_intervals[index - 500]; save_preferences(); }
            } else if (mode == Menu::pauses) {
                if (index >= 300 && index < 308) { pause_seconds = pauses[index - 300]; save_preferences(); }
            } else if (mode == Menu::logos) {
                if (index >= 400 && index < 407) { logo_choice = index - 399; save_preferences(); splash_visible = true; show_splash(); return; }
            }
            draw(true);
        } catch (const std::exception& e) { error(e.what()); }
    }

    void search(char* value) {
        try { if (value) library_filter.query = value; page = 0; show_library(); }
        catch (const std::exception& e) { error(e.what()); }
    }
    void move_to(char* value) {
        if (!viewer) return;
        try {
            if (value) {
                const std::string input = value;
                const auto maximum = viewer->last_move_number();
                if (input.empty() || input.size() > 6 || input.find_first_not_of("0123456789") != std::string::npos || std::stoul(input) > static_cast<unsigned>(maximum))
                    throw std::runtime_error("Введи номер хода от 0 до " + std::to_string(maximum) + '.');
                if (viewer->seek_move(std::stoi(input))) changed();
                full_comment = false; information = false;
            }
            draw(true);
        } catch (const std::exception& e) { draw(true); error(e.what()); }
    }
    void confirm(int button) {
        const int action = pending_file_action; pending_file_action = 0;
        try {
            if (button != 1 || !viewer || file != pending_path || source_id != pending_source) { draw(true); return; }
            const auto* selected = library.find(pending_path);
            if (!selected || selected->source != pending_source) throw std::runtime_error("Файл изменён. Сначала открой его заново.");
            if (action == 122) {
                file = root + '/' + library.move_to_viewed(file);
                dirty = true; persist(); draw(true);
                Message(ICON_INFORMATION, "Era-SGF", "Партия перенесена в sgf/viewed/ и отмечена просмотренной.", 3000);
            } else if (action == 123) {
                library.erase(file); viewer.reset(); file.clear(); source_id.clear(); dirty = false;
                full_comment = false; information = false;
                write_file_atomic(state_dir + "/last", "");
                page = 0; show_library();
            }
        } catch (const std::exception& e) { draw(true); error(e.what()); }
    }
    void autoplay_tick() {
        if (!autoplay) return;
        try {
            if (!viewer || viewer->node().children.size() != 1) { stop_autoplay(); draw(); return; }
            if (viewer->next()) changed();
            if (viewer->node().children.size() != 1) stop_autoplay();
            draw();
            if (autoplay) SetHardTimer("era-sgf-autoplay", autoplay_callback, pause_seconds * 1000);
            else ForcingSleep();
        } catch (const std::exception& e) { stop_autoplay(); draw(true); error(e.what()); ForcingSleep(); }
    }
    void finish_startup() {
        if (!splash_visible) return;
        ClearTimer(startup_callback); splash_visible = false; scene = Scene{};
        draw(true); ForcingSleep();
    }

private:
    static constexpr std::size_t page_size = 10;
    Library library;
    std::unique_ptr<Viewer> viewer;
    std::string file, source_id, manual = "auto", encoding_error, directory;
    EncodingChoice encoding{"UTF-8", false, ""};
    bool full_comment = false, information = false, about = false, dirty = false;
    std::size_t comment_page = 0, page = 0;
    unsigned refreshes = 0;
    Menu mode = Menu::main;
    Scene scene;
    std::vector<Entry> entries;
    std::vector<std::string> labels;
    std::vector<imenu> menu;
    std::map<int, ifont*> fonts;
    LibraryFilter library_filter;
    std::vector<std::size_t> library_rows;
    std::vector<std::string> year_rows;
    char search_buffer[256]{};
    char move_buffer[16]{};
    int pending_file_action = 0;
    std::string pending_path, pending_source;
    bool autoplay = false;
    bool closing = false;
    bool splash_visible = true;
    int pause_seconds = 3;
    int logo_choice = 5;
    int refresh_interval = 10;
    bool menu_overlay = false;
    const std::vector<int> refresh_intervals{5, 10, 20, 50, 0};
    const std::vector<int> pauses{1, 2, 3, 5, 10, 15, 30, 60};

    void error(const std::string& message) {
        stop_autoplay(); record_diagnostic(state_dir, message);
        Message(ICON_ERROR, "Era-SGF", message.c_str(), 6000);
    }
    void finish(bool requested) {
        stop_autoplay(); ClearTimer(startup_callback); splash_visible = false;
        if (!closing) {
            closing = true;
            try { persist(); }
            catch (const std::exception& e) {
                const auto message = std::string("Не удалось сохранить позицию. Приложение закроется.\n") + e.what();
                if (requested) error(message); else record_diagnostic(state_dir, message);
            }
        }
        if (requested) CloseApp();
    }
    bool action_menu() const {
        return mode == Menu::main || mode == Menu::game_actions || mode == Menu::file_actions || mode == Menu::settings || mode == Menu::associations || mode == Menu::navigation;
    }
    void open_move_keyboard() {
        if (!viewer) return;
        stop_autoplay();
        std::snprintf(move_buffer, sizeof(move_buffer), "%d", viewer->position().move_number);
        const auto title = "Номер хода: 0–" + std::to_string(viewer->last_move_number());
        OpenKeyboard(title.c_str(), move_buffer, sizeof(move_buffer) - 1, KBD_NUMERIC, move_callback);
    }
    void load_preferences() {
        try {
            const auto value = read_file(state_dir + "/preferences", 4096);
            const auto begin = value.find("pause=");
            if (begin != std::string::npos) {
                const int selected = std::stoi(value.substr(begin + 6));
                if (std::find(pauses.begin(), pauses.end(), selected) != pauses.end()) pause_seconds = selected;
            }
            const auto refresh = value.find("refresh=");
            if (refresh != std::string::npos) {
                const int selected = std::stoi(value.substr(refresh + 8));
                if (std::find(refresh_intervals.begin(), refresh_intervals.end(), selected) != refresh_intervals.end()) refresh_interval = selected;
            }
            const auto logo = value.find("logo=");
            if (logo != std::string::npos) {
                const int selected = std::stoi(value.substr(logo + 5));
                if (selected >= 1 && selected <= 7) logo_choice = selected;
            }
        } catch (const std::exception&) {}
    }
    void save_preferences() { write_file_atomic(state_dir + "/preferences", "pause=" + std::to_string(pause_seconds) + "\nlogo=" + std::to_string(logo_choice) + "\nrefresh=" + std::to_string(refresh_interval) + '\n'); }
    void show_refresh() {
        std::vector<std::pair<int, std::string>> options;
        for (std::size_t i = 0; i < refresh_intervals.size(); ++i) {
            const int interval = refresh_intervals[i];
            options.push_back({500 + static_cast<int>(i), (interval ? "Через " + std::to_string(interval) + " изменений" : "Только вручную") + std::string(interval == refresh_interval ? " ✓" : "")});
        }
        show(Menu::refresh_settings, "Полная очистка при листании", options);
    }
    void show_logos() {
        std::vector<std::pair<int, std::string>> options;
        for (std::size_t i = 0; i < logo_names().size(); ++i) options.push_back({400 + static_cast<int>(i), std::to_string(i + 1) + ". " + logo_names()[i] + (logo_choice == static_cast<int>(i + 1) ? " ✓" : "")});
        show(Menu::logos, "Логотип при запуске", options);
    }
    void show_splash() {
        stop_autoplay(); scene = launch_scene(logo_choice, version, ScreenWidth(), ScreenHeight());
        paint(scene); FullUpdate(); refreshes = 0;
        SetHardTimer("era-sgf-startup", startup_callback, 1500);
    }
    void stop_autoplay() { autoplay = false; ClearTimer(autoplay_callback); }
    void toggle_autoplay() {
        if (autoplay) { stop_autoplay(); return; }
        if (!viewer || full_comment || !encoding_error.empty() || viewer->node().children.size() != 1) {
            stop_autoplay();
            if (viewer && viewer->node().children.size() > 1) Message(ICON_INFORMATION, "Era-SGF", "Развилка: сначала выбери ветку вручную.", 3000);
            return;
        }
        autoplay = true; SetHardTimer("era-sgf-autoplay", autoplay_callback, pause_seconds * 1000);
    }
    void show_pauses() {
        std::vector<std::pair<int, std::string>> options;
        for (std::size_t i = 0; i < pauses.size(); ++i) options.push_back({300 + static_cast<int>(i), std::to_string(pauses[i]) + " с" + (pauses[i] == pause_seconds ? " ✓" : "")});
        show(Menu::pauses, "Пауза автолистания", options);
    }
    void screenshot() {
        stop_autoplay();
        const bool was_full = full_comment, was_information = information;
        const auto old_page = comment_page;
        std::string target;
        try {
            full_comment = false; information = false; comment_page = 0; draw(true);
            std::unique_ptr<ibitmap, decltype(&std::free)> bitmap(BitmapFromScreen(0, 0, ScreenWidth(), ScreenHeight()), &std::free);
            if (!bitmap) throw std::runtime_error("Не удалось получить изображение экрана.");
            const std::string folder = root + "/screenshots/Era-SGF"; make_directories(folder);
            char stamp[32]{}; const auto now = std::time(nullptr); std::tm time{};
            if (::localtime_r(&now, &time)) std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &time);
            const std::string base = std::string("position-") + stamp + "-" + source_id.substr(0, 8) + "-g" + std::to_string(viewer->game_index() + 1) + "-move" + std::to_string(viewer->position().move_number);
            for (unsigned n = 1; n <= 10000; ++n) {
                const auto candidate = folder + '/' + base + (n == 1 ? "" : "-" + std::to_string(n)) + ".png";
                const int fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (fd < 0) { if (errno == EEXIST) continue; throw std::runtime_error("Не удалось создать файл снимка."); }
                ::close(fd); target = candidate; break;
            }
            if (target.empty()) throw std::runtime_error("Не найдено свободное имя для снимка.");
            const auto png = grayscale_png(bitmap->width, bitmap->height, bitmap->depth, bitmap->scanline, bitmap->data);
            write_file_atomic(target, png);
            if (read_file(target) != png) throw std::runtime_error("Снимок записан не полностью.");
            full_comment = was_full; information = was_information; comment_page = old_page;
            if (was_full) draw(true);
            const auto message = "Снимок сохранён:\n" + target.substr(root.size() + 1);
            Message(ICON_INFORMATION, "Era-SGF", message.c_str(), 5000);
        } catch (...) {
            if (!target.empty()) ::unlink(target.c_str());
            full_comment = was_full; information = was_information; comment_page = old_page;
            draw(true); throw;
        }
    }
    void changed() {
        dirty = true; comment_page = 0;
        if (viewer && viewer->node_index() != 0 && viewer->node().children.empty() && library.find(file)) library.set_viewed(file, true);
    }
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
        stop_autoplay();
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
        library.track(path, raw);
    }
    ViewModel model() {
        ViewModel value;
        value.title = std::string("Era-SGF ") + version;
        value.position.size = 9; value.position.cells.assign(81, Stone::empty);
        value.comment = "Открой запись через Меню → Открыть SGF.\nКнопки перелистывания меняют ход; варианты и кодировка доступны в меню.";
        if (about) {
            value.title = "О программе"; value.full_comment = true; value.about = true; value.comment_page = comment_page;
            value.comment = std::string("Era-SGF ") + version + "\nПросмотр партий го для PocketBook\n\nАвтор: VYBel\ngithub.com/VYBel/Era-SGF\n\nЛицензия исходников: MIT\nPNG: zlib\nC++: GCC Runtime Library Exception 3.1\n\nInkView предоставляется прошивкой PocketBook.\nТексты лицензий: Era-SGF-LICENSES/\n\nБез гарантии. Независимый проект.";
            return value;
        }
        if (viewer) {
            value.has_game = true; value.autoplay = autoplay;
            value.position = viewer->position();
            const auto& initial = viewer->game().nodes[0];
            const auto black = decode(initial.value("PB")), white = decode(initial.value("PW"));
            const auto black_rank = decode(initial.value("BR")), white_rank = decode(initial.value("WR"));
            value.title = "B: " + (black.empty() ? std::string("не указано") : black) + (black_rank.empty() ? "" : " (" + black_rank + ')');
            value.title += " - W: " + (white.empty() ? std::string("не указано") : white) + (white_rank.empty() ? "" : " (" + white_rank + ')');
            value.move_caption = "Ход " + std::to_string(value.position.move_number) + " / " + std::to_string(viewer->last_move_number());
            if (encoding_error.empty() && viewer->at_main_line_end() && !initial.value("RE").empty())
                value.result_caption = "Результат: " + describe_result(decode(initial.value("RE")));
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
        return a.kind == b.kind && a.bounds.x == b.bounds.x && a.bounds.y == b.bounds.y && a.bounds.w == b.bounds.w && a.bounds.h == b.bounds.h && a.color == b.color && a.fill == b.fill && a.font == b.font && a.text == b.text && a.centered == b.centered && a.stroke_width == b.stroke_width;
    }
    void paint(const Scene& next) {
        ClearScreen();
        for (const auto& op : next.drawing) {
            const auto& r = op.bounds;
            if (op.kind == DrawKind::line) {
                for (int offset = -op.stroke_width / 2; offset <= (op.stroke_width - 1) / 2; ++offset) {
                    const int dx = std::abs(r.h) > std::abs(r.w) ? offset : 0;
                    const int dy = std::abs(r.h) > std::abs(r.w) ? 0 : offset;
                    DrawLine(r.x + dx, r.y + dy, r.x + r.w + dx, r.y + r.h + dy, gray(op.color));
                }
            }
            else if (op.kind == DrawKind::rectangle) {
                if (op.fill >= 0) FillArea(r.x, r.y, r.w, r.h, gray(op.fill));
                DrawRect(r.x, r.y, r.w, r.h, gray(op.color));
            } else if (op.kind == DrawKind::circle) {
                const int radius = r.w / 2, cx = r.x + radius, cy = r.y + radius;
                const int inner = std::max(0, radius - std::max(1, op.stroke_width));
                for (int y = -radius; y <= radius; ++y) {
                    const int outer_half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - y * y)));
                    const int inner_half = std::abs(y) <= inner ? static_cast<int>(std::sqrt(static_cast<double>(inner * inner - y * y))) : -1;
                    if (inner_half < 0 || op.fill == op.color) FillArea(cx - outer_half, cy + y, outer_half * 2 + 1, 1, gray(op.color));
                    else {
                        FillArea(cx - outer_half, cy + y, outer_half - inner_half, 1, gray(op.color));
                        FillArea(cx + inner_half + 1, cy + y, outer_half - inner_half, 1, gray(op.color));
                        if (op.fill >= 0) FillArea(cx - inner_half, cy + y, inner_half * 2 + 1, 1, gray(op.fill));
                    }
                }
            } else {
                SetFont(font(op.font), gray(op.color));
                DrawTextRect(r.x, r.y, r.w, r.h, op.text.c_str(), (op.centered ? ALIGN_CENTER : ALIGN_LEFT) | VALIGN_MIDDLE);
            }
        }
    }
    void draw(bool full = false) {
        full = full || menu_overlay;
        menu_overlay = false;
        const int width = ScreenWidth(), height = ScreenHeight();
        ViewModel presentation;
        try { presentation = model(); }
        catch (const std::exception& e) {
            stop_autoplay();
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
        if (!full && !scene.drawing.empty() && (left >= right || top >= bottom)) {
            scene = std::move(next); comment_page = scene.comment_page; return;
        }
        paint(next);
        const bool first = scene.drawing.empty();
        scene = std::move(next); comment_page = scene.comment_page;
        if (full || first || (refresh_interval > 0 && ++refreshes >= refresh_interval)) { FullUpdate(); refreshes = 0; }
        else if (left < right && top < bottom) {
            left = std::max(0, left); top = std::max(0, top);
            PartialUpdate(left, top, std::min(width, right) - left, std::min(height, bottom) - top);
        }
    }
    void menu_back() {
        switch (mode) {
        case Menu::main: draw(true); break;
        case Menu::navigation: case Menu::variants: case Menu::games: show_game_actions(); break;
        case Menu::refresh_settings: case Menu::encodings: case Menu::pauses: case Menu::logos: case Menu::associations: show_settings(); break;
        case Menu::library_filters: case Menu::library_years: page = 0; show_library(); break;
        case Menu::files: if (viewer) show_file_actions(); else show_main(); break;
        default: show_main(); break;
        }
    }
    void show(Menu selected, const std::string& title, std::vector<std::pair<int, std::string>> options) {
        if (selected != Menu::main) options.insert(options.begin(), {900, "< Назад"});
        stop_autoplay();
        menu_overlay = true;
        mode = selected; labels.clear(); menu.clear(); labels.reserve(options.size() + 1); menu.reserve(options.size() + 2);
        labels.push_back(title);
        for (const auto& option : options) labels.push_back(option.second);
        menu.push_back({ITEM_HEADER, 0, &labels[0][0], nullptr});
        for (std::size_t i = 0; i < options.size(); ++i) menu.push_back({ITEM_ACTIVE, static_cast<short>(options[i].first), &labels[i + 1][0], nullptr});
        menu.push_back({0, 0, nullptr, nullptr});
        OpenMenu(menu.data(), 0, ScreenWidth() / 10, ScreenHeight() / 10, menu_callback);
    }
    void show_main() {
        if (about) { about = false; full_comment = false; comment_page = 0; }
        std::vector<std::pair<int, std::string>> options{{114, "Библиотека партий"}};
        if (viewer) {
            options.insert(options.end(), {{130, "Партия…"}, {131, "Файл…"}, {117, "Снимок позиции (.png)"}});
        } else options.push_back({100, "Открыть SGF"});
        options.insert(options.end(), {{132, "Настройки…"}, {113, "Выход"}});
        show(Menu::main, std::string("Era-SGF ") + version, options);
    }
    void show_game_actions() {
        if (!viewer) return;
        std::vector<std::pair<int, std::string>> options{{134, "Навигация…"}};
        if (viewer->node().children.size() > 1) options.push_back({103, "Ветки…"});
        options.push_back({109, full_comment ? "Вернуться к доске" : "Комментарий на весь экран"});
        if (viewer->node().children.size() == 1 || autoplay) options.push_back({115, autoplay ? "Остановить автолистание" : "Начать автолистание"});
        if (viewer->collection().games.size() > 1) options.push_back({102, "Выбрать партию в SGF…"});
        show(Menu::game_actions, "Партия", options);
    }
    void show_navigation() {
        if (!viewer) return;
        show(Menu::navigation, "Навигация", {{105, "В начало"}, {136, "На 10 ходов назад"}, {135, "К номеру хода…"}, {137, "На 10 ходов вперёд"}, {106, "В конец выбранной ветки"}, {104, "К предыдущей развилке"}});
    }
    void show_file_actions() {
        std::vector<std::pair<int, std::string>> options{{100, "Открыть SGF"}};
        if (const auto* record = library.find(file)) {
            options.push_back({record->viewed ? 121 : 120, record->viewed ? "Отметить новой" : "Отметить просмотренной"});
            options.push_back({122, "Перенести в viewed"});
            options.push_back({123, "Удалить файл с книги"});
        }
        show(Menu::file_actions, "Файл", options);
    }
    void show_settings() {
        std::vector<std::pair<int, std::string>> options;
        if (viewer) options.push_back({107, "Кодировка: " + (manual == "auto" ? "Авто" : manual)});
        options.insert(options.end(), {{138, "Обновление экрана…"}, {116, "Пауза автолистания: " + std::to_string(pause_seconds) + " с"}, {118, "Логотип при запуске"}, {112, "Повернуть экран"}, {133, "Ассоциация SGF…"}, {139, "О программе"}});
        show(Menu::settings, "Настройки", options);
    }
    void pagers(std::vector<std::pair<int, std::string>>& options, std::size_t count) {
        if (page) options.push_back({2, "< Предыдущая страница"});
        if ((page + 1) * page_size < count) options.push_back({3, "Следующая страница >"});
    }
    std::string filter_name() const {
        return library_filter.status == ReadFilter::new_only ? "Новые" : library_filter.status == ReadFilter::viewed_only ? "Просмотренные" : "Все";
    }
    void show_library() {
        if (!library.loaded()) library.refresh();
        library_rows = library.query(library_filter);
        page = std::min(page, library_rows.empty() ? std::size_t{0} : (library_rows.size() - 1) / page_size);
        std::vector<std::pair<int, std::string>> options{
            {10, "Показывать: " + filter_name()}, {11, "Год: " + (library_filter.year.empty() ? "все" : library_filter.year == "unknown" ? "без даты" : library_filter.year)},
            {12, library_filter.query.empty() ? "Поиск…" : "Поиск: " + library_filter.query}, {13, "Обновить библиотеку"}};
        if (!library_filter.query.empty()) options.push_back({14, "Сбросить поиск"});
        for (std::size_t i = page * page_size; i < std::min(library_rows.size(), (page + 1) * page_size); ++i) {
            const auto& record = library.records()[library_rows[i]];
            std::string title = record.viewed ? "✓ " : "○ ";
            if (!record.date.empty()) title += record.date + " · ";
            title += record.black.empty() && record.white.empty() ? basename(record.path) : record.black + " — " + record.white;
            if (record.games > 1) title += " [" + std::to_string(record.games) + " партий]";
            if (!record.problem.empty()) title += " [ошибка SGF]";
            options.push_back({1000 + static_cast<int>(i % page_size), title});
        }
        pagers(options, library_rows.size());
        show(Menu::library, "Библиотека: " + std::to_string(library_rows.size()) + " / " + std::to_string(library.records().size()), options);
    }
    void show_filters() { show(Menu::library_filters, "Какие партии показывать", {{20, "Все"}, {21, "Новые"}, {22, "Просмотренные"}}); }
    void show_years() {
        year_rows = library.years();
        page = std::min(page, year_rows.empty() ? std::size_t{0} : (year_rows.size() - 1) / page_size);
        std::vector<std::pair<int, std::string>> options{{1, "Все годы"}};
        for (std::size_t i = page * page_size; i < std::min(year_rows.size(), (page + 1) * page_size); ++i)
            options.push_back({1000 + static_cast<int>(i % page_size), year_rows[i] == "unknown" ? "Без даты" : year_rows[i]});
        pagers(options, year_rows.size()); show(Menu::library_years, "Год партии", options);
    }
    void request_file_action(int action) {
        const auto* selected = library.find(file);
        if (!selected || selected->source != source_id) throw std::runtime_error("Файл изменён. Сначала открой его заново.");
        persist(); pending_file_action = action; pending_path = file; pending_source = source_id;
        const auto message = (action == 122 ? "Перенести эту партию в sgf/viewed/?\n" : "Удалить этот SGF-файл из памяти книги?\n") + basename(file);
        Dialog(ICON_INFORMATION, action == 122 ? "Перенос партии" : "Удаление партии", message.c_str(), action == 122 ? "Перенести" : "Удалить", "Отмена", confirm_callback);
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
        if (action == Action::autoplay) { toggle_autoplay(); draw(); return; }
        stop_autoplay();
        if (action == Action::refresh) { draw(true); return; }
        if (action == Action::information && viewer) { full_comment = true; information = true; comment_page = 0; draw(true); return; }
        if (about && action == Action::toggle_comment) { about = false; full_comment = false; comment_page = 0; show_settings(); return; }
        if (action == Action::go_to_move) { open_move_keyboard(); return; }
        if (viewer && (action == Action::first || action == Action::last || action == Action::back_ten || action == Action::forward_ten)) {
            const auto before = viewer->node_index();
            if (action == Action::first) viewer->begin();
            else if (action == Action::last) viewer->end();
            else viewer->jump_moves(action == Action::back_ten ? -10 : 10);
            if (before != viewer->node_index()) changed();
            full_comment = false; information = false; draw(); return;
        }
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
void confirm_callback(int button) { application->confirm(button); }
void search_callback(char* text) { application->search(text); }
void move_callback(char* text) { application->move_to(text); }
void autoplay_callback() { application->autoplay_tick(); }
void startup_callback() { application->finish_startup(); }
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
