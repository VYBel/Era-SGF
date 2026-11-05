#include "erasgf/sgf.hpp"
#include "erasgf/viewer.hpp"
#include "erasgf/encoding.hpp"
#include "erasgf/render.hpp"
#include "erasgf/png.hpp"
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace erasgf;
namespace {
int failures = 0;
int total = 0;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string("check failed: ") + #expression); } while (false)
template<class F> void throws(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } CHECK(caught); }
void test(const char* name, const std::function<void()>& f) {
    ++total;
    try { f(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
}

int main() {
    test("library layout has reachable nonoverlapping controls in both orientations", [] {
        for (const auto dimensions : {std::pair<int, int>{320,320}, {600,800}, {1264,1680}, {1680,1264}}) {
            LibraryViewModel model; model.total = model.matching = 1700; model.pages = 200; model.page = 1;
            const auto count = library_page_capacity(dimensions.first, dimensions.second);
            CHECK(count >= 1);
            for (std::size_t i = 0; i < count; ++i) model.rows.push_back({"Black player — White player", "1969-11-14 · New"});
            const auto scene = make_library_scene(model, dimensions.first, dimensions.second);
            std::size_t rows = 0;
            for (std::size_t i = 0; i < scene.controls.size(); ++i) {
                const auto& c = scene.controls[i];
                if (c.value >= 1000) ++rows;
                CHECK(c.bounds.w > 0 && c.bounds.h >= 40);
                CHECK(c.bounds.x >= 0 && c.bounds.y >= 0);
                CHECK(c.bounds.x + c.bounds.w <= dimensions.first);
                CHECK(c.bounds.y + c.bounds.h <= dimensions.second);
                for (std::size_t j = i + 1; j < scene.controls.size(); ++j) {
                    const auto& d = scene.controls[j].bounds;
                    CHECK(c.bounds.x + c.bounds.w <= d.x || d.x + d.w <= c.bounds.x || c.bounds.y + c.bounds.h <= d.y || d.y + d.h <= c.bounds.y);
                }
            }
            CHECK(rows == count);
        }
    });
    test("escaped brackets, slashes and soft newlines stay inside comments", [] {
        auto c = parse_sgf("(;SZ[9]C[hello\\] \\\\ path\\\nnext];B[aa])");
        CHECK(c.games.size() == 1);
        CHECK(c.games[0].nodes.size() == 2);
        CHECK(c.games[0].nodes[0].value("C") == "hello] \\ pathnext");
    });
    test("collection games are independent", [] {
        auto c = parse_sgf("(;SZ[9]) (;SZ[13];W[bc])");
        CHECK(c.games.size() == 2);
        CHECK(replay(c.games[1], 1).size == 13);
        CHECK(replay(c.games[1], 1).at(1, 2) == Stone::white);
    });
    test("nested variations attach to the last node of the sequence", [] {
        auto c = parse_sgf("(;SZ[9];B[aa](;W[bb];B[cc])(;W[dd]))");
        const auto& n = c.games[0].nodes;
        CHECK(n.size() == 5);
        CHECK(n[1].children == std::vector<std::size_t>({2, 4}));
        CHECK(n[4].parent == 1);
    });
    test("passes and comment-only nodes remain navigable", [] {
        Viewer v(parse_sgf("(;SZ[9];B[aa];C[explanation];W[])"));
        CHECK(v.next()); CHECK(v.next());
        CHECK(v.node().value("C") == "explanation");
        CHECK(v.position().move_number == 1);
        CHECK(v.next()); CHECK(v.position().move_number == 2);
        CHECK(!v.position().last_move);
        CHECK(!v.next()); CHECK(v.previous());
    });
    test("captures in one variation never leak into its sibling", [] {
        Viewer v(parse_sgf("(;SZ[9]AB[ab][ba][cb]AW[bb](;B[bc])(;B[ii]))"));
        CHECK(v.next(0));
        CHECK(v.position().at(1, 1) == Stone::empty);
        CHECK(v.position().black_captures == 1);
        CHECK(v.previous()); CHECK(v.next(1));
        CHECK(v.position().at(1, 1) == Stone::white);
        CHECK(v.position().black_captures == 0);
    });
    test("setup rectangles and removal are replayed", [] {
        auto c = parse_sgf("(;SZ[9]AB[aa:bb];AE[ab])");
        auto p = replay(c.games[0], 1);
        CHECK(p.at(0, 0) == Stone::black);
        CHECK(p.at(1, 1) == Stone::black);
        CHECK(p.at(0, 1) == Stone::empty);
    });
    test("node marks are local and retain raw label text", [] {
        auto c = parse_sgf("(;SZ[9]TR[aa]LB[bb:A];B[cc])");
        CHECK(replay(c.games[0], 0).marks.size() == 2);
        CHECK(replay(c.games[0], 0).marks[1].label == "A");
        CHECK(replay(c.games[0], 1).marks.empty());
    });
    test("chosen branch is retained when stepping back then forward", [] {
        Viewer v(parse_sgf("(;SZ[9];B[aa](;W[bb];B[cc])(;W[dd];B[ee]))"));
        CHECK(v.next()); CHECK(v.next(1)); CHECK(v.previous()); CHECK(v.next());
        CHECK(v.node().value("W") == "dd");
        v.end(); CHECK(v.node().value("B") == "ee");
        CHECK(v.previous_branch()); CHECK(v.node().value("B") == "aa");
        v.begin(); CHECK(v.node_index() == 0);
    });
    test("jump to end is atomic when a later node has an invalid move", [] {
        Viewer v(parse_sgf("(;SZ[9];B[aa];W[aa])"));
        throws([&] { v.end(); });
        CHECK(v.node_index() == 0);
    });
    test("invalid syntax cannot masquerade as a partial record", [] {
        throws([] { parse_sgf("(;C[unfinished)"); });
        throws([] { parse_sgf("(B[aa])"); });
        throws([] { parse_sgf("(;B[aa])garbage"); });
        throws([] { parse_sgf("(;SZ[9]SZ[13])"); });
        throws([] { parse_sgf(""); });
    });
    test("parser byte, node and nesting limits are enforced", [] {
        throws([] { parse_sgf("(;C[long text])", ParseLimits{8, 100, 10}); });
        throws([] { parse_sgf("(;;;;)", ParseLimits{100, 3, 10}); });
        throws([] { parse_sgf("(;(;(;)))", ParseLimits{100, 10, 2}); });
        CHECK(parse_sgf("(;;)", ParseLimits{100, 2, 1}).games[0].nodes.size() == 2);
    });
    test("unsupported games, sizes and invalid coordinates are rejected", [] {
        for (const std::string s : {"(;GM[2])", "(;SZ[10])", "(;SZ[9];B[zz])", "(;SZ[9]AB[aa];B[aa])"}) {
            auto c = parse_sgf(s);
            throws([&] { replay(c.games[0], c.games[0].nodes.size() - 1); });
        }
    });
    test("legacy tt pass and square SZ syntax are supported", [] {
        auto c = parse_sgf("(;SZ[19:19];B[tt])");
        auto p = replay(c.games[0], 1);
        CHECK(p.move_number == 1); CHECK(!p.last_move);
    });
    test("long move sequences do not consume recursive parser stack", [] {
        std::string s = "(";
        for (int i = 0; i < 20000; ++i) s += ";C[x]";
        s += ")";
        CHECK(parse_sgf(s).games[0].nodes.size() == 20000);
    });
    test("replay can represent a self-capture without enforcing a ruleset", [] {
        auto c = parse_sgf("(;SZ[9]AW[ab][ba];B[aa])");
        const auto p = replay(c.games[0], 1);
        CHECK(p.at(0, 0) == Stone::empty);
        CHECK(p.white_captures == 1);
    });
    test("all initial Cyrillic codecs decode independently checked bytes", [] {
        CHECK(decode_text("\xcf\xf0\xe8\xe2\xe5\xf2, \xb8\xe6!", "cp1251") == "Привет, ёж!");
        CHECK(decode_text("\xf0\xd2\xc9\xd7\xc5\xd4, \xa3\xd6!", "koi8-r") == "Привет, ёж!");
        CHECK(decode_text("\x8f\xe0\xa8\xa2\xa5\xe2, \xf1\xa6!", "cp866") == "Привет, ёж!");
        CHECK(decode_text("\xbf\xe0\xd8\xd2\xd5\xe2, \xf1\xd6!", "iso-8859-5") == "Привет, ёж!");
        CHECK(decode_text("Caf\xe9", "latin1") == "Café");
        CHECK(decode_text("\xef\xbb\xbfПривет", "utf8") == "Привет");
    });
    test("invalid Unicode and undefined legacy bytes are never silently replaced", [] {
        for (const auto& s : {"\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f\x92"}) throws([&] { decode_text(s, "UTF-8"); });
        throws([] { decode_text("\x98", "cp1251"); });
        throws([] { decode_text("text", "UTF-16"); });
    });
    test("manual codec wins over a wrong CA and resetting restores Auto", [] {
        const std::string raw = "\xcf\xf0\xe8\xe2\xe5\xf2";
        throws([&] { choose_encoding(raw, "UTF-8"); });
        CHECK(choose_encoding(raw, "UTF-8", "CP1251").name == "Windows-1251");
        CHECK(choose_encoding("Привет", "UTF-8", "auto").name == "UTF-8");
        CHECK(normalize_encoding(" WINDOWS_1251 ") == "Windows-1251");
    });
    test("automatic detection handles synthetic legacy records without CA", [] {
        const std::pair<const char*, const char*> codecs[] = {{"windows-1251", "Windows-1251"}, {"koi8-r", "KOI8-R"}, {"cp866", "CP866"}, {"iso-8859-5", "ISO-8859-5"}, {"utf-8", "UTF-8"}};
        for (const auto& codec : codecs) {
            std::ifstream f(std::string("tests/fixtures/") + codec.first + "-auto.sgf", std::ios::binary);
            CHECK(f.good());
            std::string raw((std::istreambuf_iterator<char>(f)), {});
            auto c = parse_sgf(raw);
            const auto choice = choose_encoding(game_text_sample(c.games[0]), "");
            CHECK(choice.name == codec.second);
            CHECK(decode_text(c.games[0].nodes[0].value("C"), choice.name) == "Привет, ёж! Чёрные окружили белую группу. Разбор партии и варианты продолжения.");
        }
    });
    test("ambiguous short non-UTF text exposes uncertainty", [] {
        const auto choice = choose_encoding("\xe9", "");
        CHECK(choice.uncertain);
        CHECK(choice.name == "ISO-8859-1");
        CHECK(choose_encoding("ASCII text", "").name == "UTF-8");
    });
    test("each game tree in a collection has its own declared charset", [] {
        auto c = parse_sgf("(;CA[CP1251]C[\xcf\xf0\xe8\xe2\xe5\xf2])(;CA[KOI8-R]C[\xf0\xd2\xc9\xd7\xc5\xd4])");
        for (const auto& g : c.games) {
            const auto e = choose_encoding(game_text_sample(g), g.nodes[0].value("CA"));
            CHECK(decode_text(g.nodes[0].value("C"), e.name) == "Привет");
        }
    });
    test("shared screen geometry fits small, Era and landscape screens", [] {
        Viewer viewer(parse_sgf("(;SZ[19]AB[dd]AW[pp]C[Комментарий])"));
        ViewModel model; model.position = viewer.position(); model.comment = "Комментарий к партии"; model.result_caption = "Результат: Победа белых: +2.5";
        model.has_game = true; model.has_previous = true; model.has_next = true;
        for (const auto dimensions : {std::pair<int, int>{320, 320}, {320, 480}, {600, 800}, {1264, 1680}, {1680, 1264}}) {
            const auto s = make_scene(model, dimensions.first, dimensions.second);
            CHECK(s.board.w > 0); CHECK(s.comments.h > 0);
            for (const auto& c : s.controls) {
                CHECK(c.bounds.w > 0 && c.bounds.h > 0);
                CHECK(c.bounds.x >= 0 && c.bounds.y >= 0);
                CHECK(c.bounds.x + c.bounds.w <= dimensions.first);
                CHECK(c.bounds.y + c.bounds.h <= dimensions.second);
            }
            for (const auto& op : s.drawing) {
                if (op.kind != DrawKind::line) CHECK(op.bounds.w >= 0 && op.bounds.h >= 0);
                CHECK(std::min(op.bounds.x, op.bounds.x + op.bounds.w) >= 0);
                CHECK(std::min(op.bounds.y, op.bounds.y + op.bounds.h) >= 0);
                CHECK(std::max(op.bounds.x, op.bounds.x + op.bounds.w) <= dimensions.first);
                CHECK(std::max(op.bounds.y, op.bounds.y + op.bounds.h) <= dimensions.second);
            }
        }
    });
    test("Russian wrapping respects code points and paragraph breaks", [] {
        CHECK(wrap_text("Привет мир\r\nЁж", 6) == std::vector<std::string>({"Привет", "мир", "Ёж"}));
        CHECK(wrap_text("abcdefgh", 3) == std::vector<std::string>({"abc", "def", "gh"}));
    });
    test("long comments have reachable pages and full-screen reading", [] {
        ViewModel model; model.position = replay(parse_sgf("(;SZ[9])").games[0], 0);
        for (int i = 0; i < 100; ++i) model.comment += "Длинный комментарий к ходу.\n";
        auto s = make_scene(model, 600, 800);
        CHECK(s.comment_pages > 1);
        bool next = false;
        for (const auto& c : s.controls) if (c.action == Action::comment_next) next = true;
        CHECK(next);
        model.comment_page = 10000;
        s = make_scene(model, 600, 800);
        CHECK(s.comment_page == s.comment_pages - 1);
        model.full_comment = true;
        auto full = make_scene(model, 600, 800);
        CHECK(full.comments.h > s.comments.h);
        CHECK(full.comment_pages < s.comment_pages);
    });
    test("an unbranched position has no branch hit target and uses the wider board", [] {
        ViewModel model; model.position = replay(parse_sgf("(;SZ[19])").games[0], 0);
        model.branches.push_back({none, "1"});
        const auto s = make_scene(model, 1264, 1680);
        CHECK(s.board.w >= 1160);
        for (const auto& c : s.controls) CHECK(c.action != Action::variations);
        bool centered_move = false;
        for (const auto& op : s.drawing) {
            CHECK(op.text.find("Комментарий 1/1") == std::string::npos);
            CHECK(op.text.find("нет комментария") == std::string::npos);
            if (op.text == "Ход 0") {
                CHECK(op.centered); CHECK(op.bounds.y >= s.board.y + s.board.h);
                centered_move = true;
            }
        }
        CHECK(centered_move);
    });
    test("ten-move navigation skips comments counts passes and clamps at endpoints", [] {
        Viewer v(parse_sgf("(;SZ[9];C[before];B[aa];C[note];W[];C[more];B[bb];W[cc];B[dd];W[ee];B[ff];W[gg];B[hh];W[ii];B[ab];W[bc])"));
        CHECK(v.last_move_number() == 12);
        CHECK(v.next()); CHECK(v.position().move_number == 0);
        CHECK(v.seek_move(0)); CHECK(v.node_index() == 0);
        CHECK(v.jump_moves(10)); CHECK(v.position().move_number == 10); CHECK(v.node().value("W") == "ii");
        CHECK(v.jump_moves(-10)); CHECK(v.node_index() == 0);
        CHECK(v.seek_move(2)); CHECK(v.node().find("W")); CHECK(v.node().value("W").empty()); CHECK(!v.position().last_move);
        CHECK(v.jump_moves(100)); CHECK(v.position().move_number == 12); CHECK(!v.jump_moves(10));
        CHECK(v.jump_moves(-100)); CHECK(v.node_index() == 0);
    });
    test("seeking a move retains the chosen continuation and rejects invalid input atomically", [] {
        Viewer v(parse_sgf("(;SZ[9];B[aa](;W[bb];B[cc])(;W[dd];B[ee];W[ff]))"));
        CHECK(v.next()); CHECK(v.next(1)); v.begin();
        CHECK(v.last_move_number() == 4);
        CHECK(v.seek_move(3)); CHECK(v.node().value("B") == "ee");
        const auto selected = v.node_index();
        throws([&] { v.seek_move(999); }); CHECK(v.node_index() == selected);
        throws([&] { v.seek_move(-1); }); CHECK(v.node_index() == selected);
        v.begin(); v.end(); CHECK(v.node().value("W") == "ff");
        CHECK(v.seek_move(2)); CHECK(v.node().value("W") == "dd");
    });
    test("a jump that encounters an invalid later move leaves the current position intact", [] {
        Viewer v(parse_sgf("(;SZ[9];B[aa];W[aa])"));
        throws([&] { v.jump_moves(10); }); CHECK(v.node_index() == 0);
        CHECK(v.seek_move(1));
        throws([&] { v.jump_moves(10); }); CHECK(v.node_index() == 1);
    });
    test("all five move controls occupy their own row immediately below the board", [] {
        ViewModel model; model.position = replay(parse_sgf("(;SZ[19])").games[0], 0);
        model.position.move_number = 5; model.has_game = true;
        model.has_previous = true; model.has_next = true;
        const auto scene = make_scene(model, 1264, 1680);
        std::vector<Rect> controls;
        for (const auto action : {Action::first, Action::back_ten, Action::go_to_move, Action::forward_ten, Action::last}) {
            const auto found = std::find_if(scene.controls.begin(), scene.controls.end(), [&](const Control& c) { return c.action == action; });
            CHECK(found != scene.controls.end()); CHECK(found->bounds.y >= scene.board.y + scene.board.h);
            controls.push_back(found->bounds);
        }
        for (std::size_t i = 1; i < controls.size(); ++i) {
            CHECK(controls[i].y == controls[0].y);
            CHECK(controls[i - 1].x + controls[i - 1].w <= controls[i].x);
        }
    });
    test("PNG preserves packed grayscale rows and ignores stride padding", [] {
        for (unsigned depth : {1u, 2u, 4u, 8u}) {
            const unsigned width = 5, height = 2, row = (width * depth + 7) / 8, stride = row + 3;
            std::vector<unsigned char> pixels(stride * height, 0xee);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < row; ++x) pixels[y * stride + x] = (x + 1) * (y + 1) * 17;
            const auto png = grayscale_png(width, height, depth, stride, pixels.data());
            CHECK(png.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
            CHECK(static_cast<unsigned char>(png[24]) == depth);
            std::string compressed;
            for (std::size_t offset = 8; offset + 12 <= png.size();) {
                unsigned size = 0;
                for (unsigned i = 0; i < 4; ++i) size = (size << 8) | static_cast<unsigned char>(png[offset + i]);
                CHECK(offset + 12 + size <= png.size());
                if (png.substr(offset + 4, 4) == "IDAT") compressed += png.substr(offset + 8, size);
                offset += size + 12;
            }
            std::vector<unsigned char> raw((row + 1) * height);
            uLongf size = raw.size();
            CHECK(uncompress(raw.data(), &size, reinterpret_cast<const Bytef*>(compressed.data()), compressed.size()) == Z_OK);
            CHECK(size == raw.size());
            for (unsigned y = 0; y < height; ++y) {
                CHECK(raw[y * (row + 1)] == 0);
                for (unsigned x = 0; x < row; ++x) CHECK(raw[y * (row + 1) + 1 + x] == pixels[y * stride + x]);
            }
        }
    });
    test("PNG rejects colour bitmaps invalid dimensions and short scanlines", [] {
        const unsigned char pixel = 0;
        throws([&] { grayscale_png(1, 1, 24, 3, &pixel); });
        throws([&] { grayscale_png(2, 1, 8, 1, &pixel); });
        throws([&] { grayscale_png(0, 1, 8, 1, &pixel); });
        throws([&] { grayscale_png(1, 1, 8, 1, nullptr); });
        throws([&] { grayscale_png(8193, 1, 8, 8193, &pixel); });
    });
    test("arrow strokes and labels fit their buttons even on small screens with results", [] {
        ViewModel model; model.position = replay(parse_sgf("(;SZ[9])").games[0], 0);
        model.has_game = true; model.has_previous = true; model.has_next = true;
        model.comment = "Comment"; model.result_caption = "Result";
        for (int width : {320, 600, 1264}) {
            const auto scene = make_scene(model, width, width);
            for (const auto& control : scene.controls) {
                if (control.action != Action::first && control.action != Action::back_ten && control.action != Action::forward_ten && control.action != Action::last) continue;
                const auto& r = control.bounds;
                bool inside = false;
                for (const auto& op : scene.drawing) {
                    if (op.kind == DrawKind::rectangle) {
                        inside = op.bounds.x == r.x && op.bounds.y == r.y && op.bounds.w == r.w && op.bounds.h == r.h;
                        continue;
                    }
                    if (!inside) continue;
                    // The central move label follows back_ten; its own hit target covers it.
                    if (op.kind == DrawKind::text && op.text != "10" && !op.text.empty()) break;
                    CHECK(std::min(op.bounds.x, op.bounds.x + op.bounds.w) >= r.x);
                    CHECK(std::max(op.bounds.x, op.bounds.x + op.bounds.w) <= r.x + r.w);
                    if (op.kind == DrawKind::line) {
                        CHECK(std::min(op.bounds.y, op.bounds.y + op.bounds.h) >= r.y);
                        CHECK(std::max(op.bounds.y, op.bounds.y + op.bounds.h) <= r.y + r.h);
                    }
                }
            }
        }
    });
    test("game results preserve unknown values and distinguish resignation time and draws", [] {
        CHECK(describe_result("").empty());
        CHECK(describe_result("0") == "Ничья");
        CHECK(describe_result("Void") == "Без результата");
        CHECK(describe_result("?") == "Неизвестен");
        CHECK(describe_result("B+R") == "Победа чёрных: сдача");
        CHECK(describe_result("W+Time") == "Победа белых: время");
        CHECK(describe_result("B+F") == "Победа чёрных: техническая");
        CHECK(describe_result("W+") == "Победа белых");
        CHECK(describe_result("custom result") == "custom result");
        Viewer viewer(parse_sgf("(;SZ[9]RE[B+R];B[aa](;W[bb];C[Final comment])(;W[cc]))"));
        CHECK(!viewer.at_main_line_end()); viewer.next(); viewer.next(1);
        CHECK(!viewer.at_main_line_end()); viewer.previous(); viewer.next(0);
        CHECK(!viewer.at_main_line_end()); viewer.next(); CHECK(viewer.at_main_line_end());
    });
    std::cout << (total - failures) << '/' << total << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
