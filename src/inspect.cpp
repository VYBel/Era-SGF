#include "erasgf/encoding.hpp"
#include "erasgf/library.hpp"
#include "erasgf/storage.hpp"
#include "erasgf/viewer.hpp"
#include <iostream>
#include <map>

using namespace erasgf;
namespace {
std::string json(const std::string& s) {
    static const char digits[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::size_t number(const std::string& text) {
    if (text.empty() || text.size() > 8 || text.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Expected a non-negative index");
    return std::stoul(text);
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--catalogue") {
            const std::string root = argv[2];
            Library library(root, root + "/system/config/era-sgf"); library.refresh();
            std::map<std::string, std::size_t> years; std::size_t viewed = 0, errors = 0;
            for (const auto& record : library.records()) { ++years[record.year]; viewed += record.viewed; errors += !record.problem.empty(); }
            std::cout << "{\"files\":" << library.records().size() << ",\"viewed\":" << viewed << ",\"errors\":" << errors << ",\"years\":{";
            bool first = true;
            for (const auto& year : years) { if (!first) std::cout << ','; first = false; std::cout << json(year.first) << ':' << year.second; }
            std::cout << "}}\n"; return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--associate") {
            const std::string action = argv[3];
            if (action != "install" && action != "remove") throw std::runtime_error("Expected install or remove");
            const auto r = update_association(argv[2], action == "install");
            std::cout << "{\"changed\":" << (r.changed ? "true" : "false") << ",\"backup\":" << json(r.backup) << "}\n";
            return 0;
        }
        if (argc < 2) throw std::runtime_error("Usage: era-sgf-inspect FILE [--game N] [--node N] [--encoding CODEC] [--state-dir DIR] [--remember]");
        std::string path = argv[1], manual = "auto", state_dir;
        std::size_t game_index = 0, node_index = 0;
        bool explicit_game = false, explicit_node = false, explicit_codec = false, remember = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--remember") { remember = true; continue; }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--game") { game_index = number(value); explicit_game = true; }
            else if (arg == "--node") { node_index = number(value); explicit_node = true; }
            else if (arg == "--encoding") { manual = normalize_encoding(value); explicit_codec = true; }
            else if (arg == "--state-dir") state_dir = value;
            else throw std::runtime_error("Unknown option: " + arg);
        }
        const auto raw = read_file(path);
        if (!state_dir.empty()) if (const auto state = load_state(state_dir, path, raw)) {
            if (!explicit_game) game_index = state->game;
            if (!explicit_node) node_index = state->node;
            if (!explicit_codec) manual = state->encoding;
        }
        Viewer viewer(parse_sgf(raw)); viewer.select_game(game_index); viewer.select_node(node_index);
        const auto p = viewer.position();
        const auto encoding = choose_encoding(game_text_sample(viewer.game()), viewer.game().nodes[0].value("CA"), manual);
        const auto comment = decode_text(viewer.node().value("C"), encoding.name);
        if (remember) {
            if (state_dir.empty()) throw std::runtime_error("--remember requires --state-dir");
            save_state(state_dir, FileState{path, fingerprint(raw), game_index, node_index, manual});
        }
        std::cout << "{\"games\":" << viewer.collection().games.size() << ",\"game\":" << game_index << ",\"node\":" << node_index
                  << ",\"size\":" << p.size << ",\"move\":" << p.move_number << ",\"encoding\":" << json(encoding.name)
                  << ",\"uncertain\":" << (encoding.uncertain ? "true" : "false") << ",\"comment\":" << json(comment) << ",\"board\":[";
        for (int y = 0; y < p.size; ++y) {
            std::string row;
            for (int x = 0; x < p.size; ++x) row += p.at(x, y) == Stone::black ? 'B' : p.at(x, y) == Stone::white ? 'W' : '.';
            if (y) std::cout << ',';
            std::cout << json(row);
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Era-SGF: " << e.what() << '\n'; return 2; }
}
