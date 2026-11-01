#include "erasgf/sgf.hpp"
#include "erasgf/viewer.hpp"
#include <functional>
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
    std::cout << (total - failures) << '/' << total << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
