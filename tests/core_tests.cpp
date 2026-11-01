#include "erasgf/sgf.hpp"
#include "erasgf/viewer.hpp"
#include "erasgf/encoding.hpp"
#include "erasgf/render.hpp"
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
        ViewModel model; model.position = viewer.position(); model.comment = "Комментарий к партии";
        for (const auto dimensions : {std::pair<int, int>{600, 800}, {1264, 1680}, {1680, 1264}}) {
            const auto s = make_scene(model, dimensions.first, dimensions.second);
            CHECK(s.board.w > 0); CHECK(s.comments.h > 0);
            for (const auto& c : s.controls) {
                CHECK(c.bounds.x >= 0 && c.bounds.y >= 0);
                CHECK(c.bounds.x + c.bounds.w <= dimensions.first);
                CHECK(c.bounds.y + c.bounds.h <= dimensions.second);
            }
            for (const auto& op : s.drawing) {
                CHECK(op.bounds.x >= 0 && op.bounds.y >= 0);
                CHECK(op.bounds.x + op.bounds.w <= dimensions.first);
                CHECK(op.bounds.y + op.bounds.h <= dimensions.second);
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
    std::cout << (total - failures) << '/' << total << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
