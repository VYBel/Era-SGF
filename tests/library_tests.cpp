#include "erasgf/library.hpp"
#include "erasgf/storage.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace erasgf;
namespace {
int total = 0, failed = 0;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed: ") + #x); } while (false)
template<class F> void throws(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } CHECK(caught); }
void test(const char* name, const std::function<void()>& f) {
    ++total;
    try { f(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
struct Fixture {
    std::string root, state;
    Fixture() {
        char name[] = "/tmp/era-library-XXXXXX"; const auto* made = ::mkdtemp(name);
        if (!made) throw std::runtime_error("mkdtemp failed");
        root = made; state = root + "/system/config/era-sgf";
        write("sgf/1969/first.sgf", "(;CA[UTF-8]SZ[9]PB[Go Seigen]PW[Cho Chikun]DT[1969-11-14]EV[Study];B[aa];W[bb])");
        write("sgf/2001/second.sgf", "(;CA[UTF-8]SZ[9]PB[Cho Chikun]PW[O Rissei]DT[2001-02-03]EV[Final];B[cc])");
    }
    ~Fixture() {
        // Only the private mkdtemp directory is removed, without following links.
        const auto pid = ::fork();
        if (!pid) { ::execl("/bin/rm", "rm", "-rf", "--", root.c_str(), static_cast<char*>(nullptr)); ::_exit(127); }
        if (pid > 0) { int status; while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
    void write(const std::string& rel, const std::string& bytes) { write_file_atomic(root + '/' + rel, bytes); }
};
}
int main() {
    test("random selection excludes viewed and invalid records and preserves status", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        std::mt19937 random(1234);
        library.set_viewed("sgf/1969/first.sgf", true);
        f.write("sgf/broken.sgf", "not an SGF");
        f.write("sgf/invalid-position.sgf", "(;SZ[19]AB[zz])"); library.refresh();
        for (int i = 0; i < 32; ++i) CHECK(library.random_unviewed(random) == "sgf/2001/second.sgf");
        CHECK(!library.find("sgf/2001/second.sgf")->viewed);
        library.set_viewed("sgf/2001/second.sgf", true);
        CHECK(library.random_unviewed(random).empty());
        library.set_viewed("sgf/1969/first.sgf", false);
        CHECK(library.random_unviewed(random) == "sgf/1969/first.sgf");
        ::unlink((f.root + "/sgf/1969/first.sgf").c_str()); library.refresh();
        CHECK(library.random_unviewed(random).empty());
    });
    test("recursive catalogue decodes metadata and combines year status and text filters", [] {
        Fixture f; f.write("sgf/1970/русская.sgf", "(;SZ[9]CA[windows-1251]PB[\xcf\xe5\xf2\xf0]PW[Cho]DT[1970-01-01])");
        Library library(f.root, f.state); CHECK(!library.loaded()); library.refresh();
        CHECK(library.records().size() == 3);
        CHECK(library.find("sgf/1969/first.sgf")->black == "Go Seigen");
        LibraryFilter filter; filter.query = "Пётр"; CHECK(library.query(filter).empty());
        filter.query = "Петр"; CHECK(library.query(filter).size() == 1);
        filter.query = "CHO"; filter.year = "1969"; CHECK(library.query(filter).size() == 1);
        CHECK(library.years().size() == 3);
        library.set_viewed("sgf/1969/first.sgf", true);
        filter.query.clear(); filter.year.clear(); filter.status = ReadFilter::new_only;
        CHECK(library.query(filter).size() == 2);
        filter.status = ReadFilter::viewed_only; CHECK(library.query(filter).size() == 1);
        filter.year = "2001"; CHECK(library.query(filter).empty());
    });
    test("viewed status survives reload and unchanged refresh but not a replaced file", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        library.set_viewed("sgf/1969/first.sgf", true);
        Library reopened(f.root, f.state); CHECK(reopened.loaded());
        CHECK(reopened.find("sgf/1969/first.sgf")->viewed);
        reopened.refresh(); CHECK(reopened.find("sgf/1969/first.sgf")->viewed);
        f.write("sgf/1969/first.sgf", "(;SZ[9]DT[1969];B[dd])");
        reopened.refresh(); CHECK(!reopened.find("sgf/1969/first.sgf")->viewed);
    });
    test("cached queries do not rescan storage and explicit refresh discovers new files", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        f.write("sgf/2002/added.sgf", "(;SZ[9]DT[2002])");
        CHECK(library.query({}).size() == 2);
        library.refresh(); CHECK(library.query({}).size() == 3);
    });
    test("a malformed catalogue recovers while invalid SGFs remain individually visible", [] {
        Fixture f; f.write("system/config/era-sgf/library.db", "broken catalogue\n");
        f.write("sgf/broken.sgf", "(;C[unterminated)");
        Library library(f.root, f.state); CHECK(!library.loaded()); library.refresh();
        CHECK(library.records().size() == 3); CHECK(!library.find("sgf/broken.sgf")->problem.empty());
    });
    test("an associated file inside reader storage can join the library without a scan", [] {
        Fixture f; f.write("Books/партия.sgf", "(;SZ[9]PB[A]PW[B]DT[2005])");
        Library library(f.root, f.state);
        library.track(f.root + "/Books/партия.sgf", read_file(f.root + "/Books/партия.sgf"));
        CHECK(library.find("Books/партия.sgf"));
        library.refresh(); CHECK(library.records().size() == 3);
    });
    test("moving into viewed preserves bytes resume and status without overwriting a collision", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        const auto old = f.root + "/sgf/1969/first.sgf", raw = read_file(old);
        save_state(f.state, {old, fingerprint(raw), 0, 2, "Windows-1251"});
        f.write("sgf/viewed/1969/first.sgf", "existing file must survive");
        const auto moved = library.move_to_viewed("sgf/1969/first.sgf");
        CHECK(moved == "sgf/viewed/1969/first (2).sgf");
        CHECK(::access(old.c_str(), F_OK) != 0);
        CHECK(read_file(f.root + '/' + moved) == raw);
        CHECK(read_file(f.root + "/sgf/viewed/1969/first.sgf") == "existing file must survive");
        CHECK(library.find(moved)->viewed);
        const auto state = load_state(f.state, f.root + '/' + moved, raw);
        CHECK(state && state->node == 2 && state->encoding == "Windows-1251");
        CHECK(*load_last_file(f.state) == f.root + '/' + moved);
    });
    test("moving and deleting reject a source replaced since indexing", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        f.write("sgf/1969/first.sgf", "replacement");
        throws([&] { library.move_to_viewed("sgf/1969/first.sgf"); });
        throws([&] { library.erase("sgf/1969/first.sgf"); });
        CHECK(read_file(f.root + "/sgf/1969/first.sgf") == "replacement");
    });
    test("deleting one selected file leaves other files and removes its catalogue entry", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        const auto other = read_file(f.root + "/sgf/2001/second.sgf");
        library.erase("sgf/1969/first.sgf");
        CHECK(::access((f.root + "/sgf/1969/first.sgf").c_str(), F_OK) != 0);
        CHECK(!library.find("sgf/1969/first.sgf"));
        CHECK(read_file(f.root + "/sgf/2001/second.sgf") == other);
        Library reopened(f.root, f.state); CHECK(reopened.records().size() == 1);
    });
    test("symlink entries are not indexed and file actions cannot escape reader storage", [] {
        Fixture f;
        CHECK(::symlink((f.root + "/sgf/1969").c_str(), (f.root + "/sgf/link").c_str()) == 0);
        CHECK(::symlink((f.root + "/sgf/1969/first.sgf").c_str(), (f.root + "/sgf/link.sgf").c_str()) == 0);
        Library library(f.root, f.state); library.refresh(); CHECK(library.records().size() == 2);
        throws([&] { library.track(f.root + "/sgf/link.sgf", "(;SZ[9])"); });
        throws([&] { library.erase("../outside.sgf"); });
        CHECK(::symlink(f.root.c_str(), (f.root + "/root-alias").c_str()) == 0);
        Library aliased(f.root + "/root-alias", f.state);
        throws([&] { aliased.refresh(); });
        throws([&] { library.move_to_viewed("/tmp/outside.sgf"); });
        CHECK(::unlink((f.root + "/sgf/1969/first.sgf").c_str()) == 0);
        CHECK(::symlink((f.root + "/sgf/2001/second.sgf").c_str(), (f.root + "/sgf/1969/first.sgf").c_str()) == 0);
        throws([&] { library.erase("sgf/1969/first.sgf"); });
    });
    test("an external move preserves viewed status without merging duplicate files", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        library.set_viewed("sgf/1969/first.sgf", true);
        const auto raw = read_file(f.root + "/sgf/1969/first.sgf");
        f.write("sgf/1969/duplicate.sgf", raw);
        CHECK(::rename((f.root + "/sgf/1969/first.sgf").c_str(), (f.root + "/sgf/1969/renamed.sgf").c_str()) == 0);
        library.refresh(); CHECK(library.records().size() == 3);
        CHECK(library.find("sgf/1969/renamed.sgf")); CHECK(library.find("sgf/1969/duplicate.sgf"));
        int viewed = 0; for (const auto& r : library.records()) if (r.viewed) ++viewed;
        CHECK(viewed == 1);
    });
    test("catalogue write failure during a move independently restores the last-file pointer", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        const auto path = f.root + "/sgf/1969/first.sgf", raw = read_file(path);
        save_state(f.state, {path, fingerprint(raw), 0, 2, "auto"});
        CHECK(::unlink((f.state + "/library.db").c_str()) == 0);
        CHECK(::mkdir((f.state + "/library.db").c_str(), 0700) == 0);
        throws([&] { library.move_to_viewed("sgf/1969/first.sgf"); });
        CHECK(read_file(path) == raw);
        CHECK(*load_last_file(f.state) == path);
        CHECK(load_state(f.state, path, raw)->node == 2);
    });
    test("one oversized SGF remains a problem entry without blocking normal games", [] {
        Fixture f;
        const auto path = f.root + "/sgf/oversized.sgf";
        std::ofstream large(path, std::ios::binary); large.seekp(16 * 1024 * 1024); large.put('x'); large.close();
        Library library(f.root, f.state); library.refresh();
        CHECK(library.records().size() == 3);
        CHECK(!library.find("sgf/oversized.sgf")->problem.empty());
        CHECK(library.find("sgf/1969/first.sgf")->problem.empty());
        Library reopened(f.root, f.state); CHECK(reopened.loaded()); CHECK(reopened.records().size() == 3);
    });
    test("incremental refresh reuses unchanged files and forced refresh reparses them", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        CHECK(library.refresh_stats().parsed == 2);
        library.set_viewed("sgf/1969/first.sgf", true);
        const auto snapshot = read_file(f.state + "/library.db");
        library.refresh(); CHECK(library.refresh_stats().parsed == 0); CHECK(library.refresh_stats().reused == 2);
        CHECK(read_file(f.state + "/library.db") == snapshot);
        f.write("sgf/new.sgf", "(;SZ[9]PB[A]PW[B])");
        library.refresh(); CHECK(library.refresh_stats().parsed == 1); CHECK(library.refresh_stats().reused == 2);
        library.refresh(true); CHECK(library.refresh_stats().parsed == 3);
        CHECK(library.find("sgf/1969/first.sgf")->viewed);
    });
    test("viewed changes leave snapshot untouched and cached queries follow mutations", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        const auto snapshot = read_file(f.state + "/library.db");
        LibraryFilter filter; filter.status = ReadFilter::new_only;
        CHECK(library.query(filter).size() == 2);
        library.set_viewed("sgf/1969/first.sgf", true);
        CHECK(read_file(f.state + "/library.db") == snapshot);
        CHECK(library.query(filter).size() == 1);
        Library reopened(f.root, f.state); CHECK(reopened.loaded());
        CHECK(reopened.find("sgf/1969/first.sgf")->viewed);
        reopened.set_viewed("sgf/1969/first.sgf", false);
        Library again(f.root, f.state); CHECK(!again.find("sgf/1969/first.sgf")->viewed);
    });
    test("failed small change write rolls back memory and leaves snapshot readable", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        CHECK(::mkdir((f.state + "/library.changes").c_str(), 0700) == 0);
        throws([&] { library.set_viewed("sgf/1969/first.sgf", true); });
        CHECK(!library.find("sgf/1969/first.sgf")->viewed);
        Library reopened(f.root, f.state); CHECK(reopened.loaded());
        CHECK(!reopened.find("sgf/1969/first.sgf")->viewed);
    });
    test("large metadata survives snapshots and oversized delta compaction", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        const std::string raw = "(;SZ[19]PB[" + std::string(600000, 'A') + "]PW[B])";
        f.write("sgf/long.sgf", raw); library.track("sgf/long.sgf", raw);
        Library reopened(f.root, f.state); CHECK(reopened.loaded()); CHECK(reopened.records().size() == 3);
        CHECK(reopened.find("sgf/long.sgf")->black.size() == 600000);
        reopened.set_viewed("sgf/long.sgf", true);
        Library again(f.root, f.state); CHECK(again.find("sgf/long.sgf")->viewed);
    });
    test("stale journal cannot undo compacted status and a damaged batch keeps the snapshot", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        library.set_viewed("sgf/1969/first.sgf", true);
        const auto stale = read_file(f.state + "/library.changes");
        library.set_viewed("sgf/1969/first.sgf", false); library.refresh(true);
        f.write("system/config/era-sgf/library.changes", stale);
        Library reopened(f.root, f.state); CHECK(reopened.loaded()); CHECK(!reopened.find("sgf/1969/first.sgf")->viewed);
        reopened.set_viewed("sgf/1969/first.sgf", true);
        auto damaged = read_file(f.state + "/library.changes"); damaged.pop_back();
        f.write("system/config/era-sgf/library.changes", damaged);
        Library again(f.root, f.state); CHECK(again.loaded()); CHECK(again.records().size() == 2);
        CHECK(!again.find("sgf/1969/first.sgf")->viewed);
    });
    test("v1 catalogue migration preserves viewed state and historical years", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        library.set_viewed("sgf/1969/first.sgf", true); library.refresh(true);
        const auto snapshot = read_file(f.state + "/library.db");
        std::istringstream in(snapshot); std::string line, legacy = "ERA-SGF-LIBRARY\t1\tcomplete\n"; std::getline(in, line);
        while (std::getline(in, line)) {
            std::size_t at = 0; for (int i = 0; i < 12; ++i) at = line.find('\t', at) + 1;
            legacy += line.substr(0, at - 1) + '\n';
        }
        f.write("system/config/era-sgf/library.db", legacy);
        Library migrated(f.root, f.state); CHECK(migrated.loaded()); CHECK(migrated.find("sgf/1969/first.sgf")->viewed);
        migrated.refresh(); CHECK(migrated.refresh_stats().parsed == 2);
        Library again(f.root, f.state); CHECK(again.find("sgf/1969/first.sgf")->viewed);
        CHECK(game_year("1054", "sgf/hash-1980.sgf") == "1054");
        CHECK(game_year("", "sgf/1650/00/001/Без даты -- aa1980ab.sgf") == "1650");
        CHECK(game_year("", "sgf/Без-даты/Без даты -- aa1980ab.sgf") == "unknown");
    });
    test("catalogues above twenty thousand records load and query", [] {
        Fixture f; Library library(f.root, f.state); library.refresh();
        auto snapshot = read_file(f.state + "/library.db"); const auto end = snapshot.find('\n');
        const auto row = snapshot.substr(end + 1, snapshot.find('\n', end + 1) - end - 1);
        std::string many = snapshot.substr(0, end + 1); const auto rest = row.substr(row.find('\t'));
        auto hex = [](const std::string& value) { std::string out; const char* digits = "0123456789abcdef"; for (unsigned char c : value) { out += digits[c >> 4]; out += digits[c & 15]; } return out; };
        for (int i = 0; i < 25001; ++i) many += hex("sgf/" + std::to_string(i) + ".sgf") + rest + '\n';
        f.write("system/config/era-sgf/library.db", many);
        Library reopened(f.root, f.state); CHECK(reopened.loaded()); CHECK(reopened.records().size() == 25001);
        CHECK(reopened.query({}).size() == 25001); CHECK(reopened.find("sgf/25000.sgf"));
    });
    std::cout << (total - failed) << '/' << total << " library tests passed\n";
    return failed ? 1 : 0;
}
