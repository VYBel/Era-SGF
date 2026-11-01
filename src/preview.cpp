#include "erasgf/encoding.hpp"
#include "erasgf/render.hpp"
#include "erasgf/storage.hpp"
#include <iostream>

using namespace erasgf;
#ifndef ERA_SGF_VERSION
#define ERA_SGF_VERSION "0.1.0"
#endif
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Usage: era-sgf-preview FILE [WIDTH HEIGHT NODE ENCODING PAGE FULL_COMMENT]");
        const int width = argc > 2 ? std::stoi(argv[2]) : 1264, height = argc > 3 ? std::stoi(argv[3]) : 1680;
        Viewer viewer(parse_sgf(read_file(argv[1])));
        viewer.select_node(argc > 4 ? std::stoul(argv[4]) : 0);
        const auto e = choose_encoding(game_text_sample(viewer.game()), viewer.game().nodes[0].value("CA"), argc > 5 ? argv[5] : "auto");
        ViewModel model; model.position = viewer.position();
        for (auto& mark : model.position.marks) mark.label = decode_text(mark.label, e.name);
        const auto& root = viewer.game().nodes[0];
        model.title = decode_text(root.value("PB"), e.name) + " — " + decode_text(root.value("PW"), e.name);
        model.subtitle = "Ход " + std::to_string(model.position.move_number) + " · " + e.name + " · Era-SGF " + ERA_SGF_VERSION;
        model.comment = decode_text(viewer.node().value("C"), e.name);
        model.has_previous = viewer.node().parent != no_node; model.has_next = !viewer.node().children.empty();
        model.comment_page = argc > 6 ? std::stoul(argv[6]) : 0; model.full_comment = argc > 7 && std::string(argv[7]) == "1";
        for (std::size_t i = 0; i < viewer.node().children.size(); ++i) {
            const auto& next = viewer.game().nodes[viewer.node().children[i]];
            const auto move = next.find("B") ? next.value("B") : next.value("W");
            Optional<Point> pt;
            if (move.size() == 2 && move[0] >= 'a' && move[0] < 'a' + model.position.size && move[1] >= 'a' && move[1] < 'a' + model.position.size) pt = Point{move[0] - 'a', move[1] - 'a'};
            model.branches.push_back({pt, std::to_string(i + 1)});
        }
        std::cout << svg(make_scene(model, width, height), width, height);
        return 0;
    } catch (const std::exception& e) { std::cerr << "Era-SGF: " << e.what() << '\n'; return 2; }
}
