#include "erasgf/library.hpp"
#include "erasgf/storage.hpp"
#include "erasgf/encoding.hpp"
#include "erasgf/sgf.hpp"
#include "erasgf/viewer.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>

namespace erasgf {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message + ": " + std::strerror(errno)); }
void put_number(std::string& out, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out += static_cast<char>((value >> (i * 8)) & 255);
}
std::uint64_t get_number(const std::string& data, std::size_t at, unsigned bytes) {
    if (at > data.size() || bytes > data.size() - at) throw std::runtime_error("Truncated random index");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[at + i])) << (i * 8);
    return value;
}
std::uint64_t hash_number(const std::string& text) { return std::stoull(text, nullptr, 16); }
std::string read_exact(std::istream& input, std::size_t size) {
    std::string result(size, '\0');
    if (size && !input.read(&result[0], static_cast<std::streamsize>(size))) throw std::runtime_error("Truncated random index");
    return result;
}
std::string parent_directory(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
}
void flush_directory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) fail("Cannot open directory for sync");
    const int status = ::fsync(fd); const int saved_errno = errno; ::close(fd);
    if (status != 0) { errno = saved_errno; fail("Cannot flush directory"); }
}
std::string hex(const std::string& input) {
    const char* digits = "0123456789abcdef"; std::string out;
    for (unsigned char c : input) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string unhex(const std::string& input) {
    auto digit = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::runtime_error("Invalid catalogue text"); };
    if (input.size() % 2) throw std::runtime_error("Invalid catalogue text");
    std::string out;
    for (std::size_t i = 0; i < input.size(); i += 2) out += static_cast<char>((digit(input[i]) << 4) | digit(input[i + 1]));
    return out;
}
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> fields; std::size_t start = 0;
    while (true) { const auto end = text.find('\t', start); fields.push_back(text.substr(start, end == std::string::npos ? end : end - start)); if (end == std::string::npos) return fields; start = end + 1; }
}
unsigned long long number(const std::string& value) {
    if (value.empty() || value.size() > 18 || value.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Invalid catalogue number");
    return std::stoull(value);
}
std::string fold(std::string text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 'a' - 'A');
        else if (c == 0xD0 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[++i]);
            if (next == 0x81) out += "\xd1\x91";
            else if (next >= 0x90 && next <= 0x9F) { out += '\xd0'; out += static_cast<char>(next + 0x20); }
            else if (next >= 0xA0 && next <= 0xAF) { out += '\xd1'; out += static_cast<char>(next - 0x20); }
            else { out += static_cast<char>(c); out += static_cast<char>(next); }
        } else out += static_cast<char>(c);
    }
    return out;
}
bool sgf(const std::string& name) { return name.size() > 4 && fold(name.substr(name.size() - 4)) == ".sgf"; }
struct Stamp { long long bytes, modified, changed, modified_ns, changed_ns; };
Stamp stamp(const struct stat& info) {
    return {static_cast<long long>(info.st_size), info.st_mtime, info.st_ctime, info.st_mtim.tv_nsec, info.st_ctim.tv_nsec};
}
void apply_stamp(LibraryRecord& record, const Stamp& value) {
    record.modified = value.modified; record.changed = value.changed;
    record.modified_ns = value.modified_ns; record.changed_ns = value.changed_ns;
}
bool same_stamp(const LibraryRecord& record, const Stamp& value) {
    return record.changed && record.bytes == static_cast<std::size_t>(value.bytes) && record.modified == value.modified &&
        record.changed == value.changed && record.modified_ns == value.modified_ns && record.changed_ns == value.changed_ns;
}
void scan(const std::string& root, const std::string& relative, std::map<std::string, Stamp>& files, unsigned depth = 0) {
    if (depth > 32) throw std::runtime_error("Too many nested SGF directories");
    struct stat info{}; const auto absolute = root + '/' + relative;
    if (::lstat(absolute.c_str(), &info) != 0) { if (errno == ENOENT) return; fail("Cannot inspect " + absolute); }
    if (!S_ISDIR(info.st_mode)) return;
    DIR* directory = ::opendir(absolute.c_str()); if (!directory) fail("Cannot open " + absolute);
    try {
        while (const auto* entry = ::readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.empty() || name[0] == '.') continue;
            const auto path = relative + '/' + name;
            if (::lstat((root + '/' + path).c_str(), &info) != 0) fail("Cannot inspect " + path);
            if (S_ISDIR(info.st_mode)) scan(root, path, files, depth + 1);
            else if (S_ISREG(info.st_mode) && sgf(name)) files.emplace(path, stamp(info));
        }
    } catch (...) { ::closedir(directory); throw; }
    ::closedir(directory);
}
void exclusive_copy(const std::string& target, const std::string& raw) {
    int fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) fail("Cannot create " + target);
    try {
        std::size_t offset = 0;
        while (offset < raw.size()) {
            const auto written = ::write(fd, raw.data() + offset, raw.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) fail("Cannot write " + target);
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(fd) != 0) fail("Cannot flush " + target);
        if (::close(fd) != 0) { fd = -1; fail("Cannot close " + target); }
        fd = -1;
    } catch (...) { if (fd >= 0) ::close(fd); ::unlink(target.c_str()); throw; }
}
}

std::string game_year(const std::string& date, const std::string& fallback) {
    auto year = [](const std::string& text, std::size_t i) {
        const auto candidate = text.substr(i, 4);
        if (candidate.size() == 4 && candidate.find_first_not_of("0123456789") == std::string::npos && candidate >= "1000" && candidate <= "2199" &&
            (i + 4 == text.size() || text[i + 4] < '0' || text[i + 4] > '9')) return candidate;
        return std::string{};
    };
    for (std::size_t i = 0; i + 4 <= date.size(); ++i) {
        if (i && date[i - 1] >= '0' && date[i - 1] <= '9') continue;
        const auto result = year(date, i); if (!result.empty()) return result;
    }
    // Only a year directory or a leading date in the basename is a fallback,
    // never an arbitrary four-digit substring of a player's name or content hash.
    std::istringstream parts(fallback); std::string part;
    while (std::getline(parts, part, '/')) {
        if (part.size() == 4 || (parts.eof() && part.size() > 4 && (part[4] == '-' || part[4] == '.' || part[4] == ' '))) {
            const auto result = year(part, 0); if (!result.empty()) return result;
        }
    }
    return "unknown";
}
Library::Library(std::string root, std::string state_directory, bool defer_load) : root_(std::move(root)), state_(std::move(state_directory)) {
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    if (root_.empty() || root_[0] != '/' || root_ == "/") throw std::runtime_error("Library requires a reader storage root");
    if (!defer_load) while (!load_step()) {}
}
std::string Library::relative(const std::string& path) const {
    if (path.empty() || path.find('\0') != std::string::npos) throw std::runtime_error("Invalid SGF path");
    std::string result = path;
    if (result[0] == '/') {
        if (result.compare(0, root_.size() + 1, root_ + '/') != 0) throw std::runtime_error("SGF is outside reader storage");
        result = result.substr(root_.size() + 1);
    }
    std::istringstream parts(result); std::string part;
    while (std::getline(parts, part, '/')) if (part.empty() || part == "." || part == "..") throw std::runtime_error("Invalid SGF path");
    if (!sgf(result)) throw std::runtime_error("Expected an SGF file");
    return result;
}
void Library::check_file(const std::string& relative_path) const {
    struct stat info{}; std::string path = root_;
    if (::lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) throw std::runtime_error("Invalid reader storage root");
    std::istringstream parts(relative_path); std::string part;
    while (std::getline(parts, part, '/')) {
        path += '/' + part;
        if (::lstat(path.c_str(), &info) != 0) fail("Cannot inspect " + path);
        if (S_ISLNK(info.st_mode)) throw std::runtime_error("SGF symlinks are not followed");
        if (path != root_ + '/' + relative_path && !S_ISDIR(info.st_mode)) throw std::runtime_error("Invalid SGF directory");
    }
    if (!S_ISREG(info.st_mode)) throw std::runtime_error("Expected a regular SGF file");
}
std::size_t Library::index(const std::string& path) const {
    const auto normalized = relative(path);
    const auto found = by_path_.find(normalized);
    if (found != by_path_.end()) return found->second;
    throw std::runtime_error("Game is absent from the catalogue; refresh the library");
}
const LibraryRecord* Library::find(const std::string& path) const {
    try { return &records_[index(path)]; } catch (const std::exception&) { return nullptr; }
}
LibraryRecord Library::describe(const std::string& path, const std::string& raw) const {
    LibraryRecord record; record.path = path; record.source = fingerprint(raw); record.bytes = raw.size();
    struct stat info{}; if (::lstat((root_ + '/' + path).c_str(), &info) == 0) apply_stamp(record, stamp(info));
    try {
        const auto collection = parse_sgf(raw); record.games = collection.games.size();
        const auto& game = collection.games[0]; const auto& node = game.nodes[0];
        // A syntactically valid SGF may still have an unsupported board/game or
        // invalid setup coordinates. Random candidates must open at their root.
        replay(game, 0);
        auto decode = [&](const std::string& key) {
            const auto value = node.value(key); if (value.empty()) return value;
            std::string result;
            try { result = decode_text(value, choose_encoding(value, node.value("CA")).name); }
            catch (const std::exception&) { try { result = decode_text(value, "UTF-8"); } catch (const std::exception&) { return std::string("?"); } }
            for (char& c : result) if (static_cast<unsigned char>(c) < 32) c = ' ';
            return result;
        };
        record.black = decode("PB"); record.white = decode("PW"); record.date = decode("DT"); record.event = decode("EV");
    } catch (const std::exception& e) { record.problem = e.what(); }
    record.year = game_year(record.date, record.path);
    return record;
}
void Library::invalidate_query() { query_valid_ = false; query_result_.clear(); }
void Library::rebuild_index() {
    by_path_.clear(); by_path_.reserve(records_.size());
    for (std::size_t i = 0; i < records_.size(); ++i) by_path_.emplace(records_[i].path, i);
    invalidate_query();
}
std::string Library::encode(const LibraryRecord& r) const {
    std::ostringstream out;
    out << hex(r.path) << '\t' << r.source << '\t' << hex(r.black) << '\t' << hex(r.white) << '\t' << hex(r.date) << '\t' << hex(r.event) << '\t' << hex(r.year) << '\t' << hex(r.problem) << '\t' << r.viewed << '\t' << r.games << '\t' << r.bytes << '\t' << std::max(0LL, r.modified)
        << '\t' << std::max(0LL, r.changed) << '\t' << r.modified_ns << '\t' << r.changed_ns;
    return out.str();
}
LibraryRecord Library::decode(const std::string& line) const {
    const auto f = split(line); if (f.size() != 12 && f.size() != 15) throw std::runtime_error("Invalid catalogue row");
    LibraryRecord r; r.path = relative(unhex(f[0])); r.source = f[1];
    if (r.source.size() != 16 || r.source.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::runtime_error("Invalid catalogue identity");
    r.black = unhex(f[2]); r.white = unhex(f[3]); r.date = unhex(f[4]); r.event = unhex(f[5]); r.year = unhex(f[6]); r.problem = unhex(f[7]);
    // Recompute on migration: old catalogues excluded historical years.
    r.year = game_year(r.date, r.path);
    if (f[8] != "0" && f[8] != "1") throw std::runtime_error("Invalid viewed flag");
    r.viewed = f[8] == "1"; r.games = number(f[9]); r.bytes = number(f[10]); r.modified = number(f[11]);
    if (f.size() == 15) { r.changed = number(f[12]); r.modified_ns = number(f[13]); r.changed_ns = number(f[14]); }
    if (r.games > 100000 || r.bytes > 16 * 1024 * 1024 + 1 || r.modified_ns >= 1000000000 || r.changed_ns >= 1000000000) throw std::runtime_error("Catalogue limits exceeded");
    return r;
}
unsigned Library::load_percent() const {
    return !load_input_ && load_started_ ? 100 : load_size_ ? static_cast<unsigned>(100ULL * load_bytes_ / load_size_) : 0;
}
bool Library::load_step(std::size_t rows) {
    if (load_started_ && !load_input_) return true;
    if (!rows) throw std::runtime_error("Catalogue load needs a positive row budget");
    try {
        std::string line;
        if (!load_started_) {
            load_started_ = true;
            const auto path = state_ + "/library.db"; struct stat info{};
            if (::lstat(path.c_str(), &info) != 0) {
                if (errno == ENOENT) { merge_pending(); ready_ = true; return true; }
                fail("Cannot inspect catalogue");
            }
            if (!S_ISREG(info.st_mode) || info.st_size > 256 * 1024 * 1024) throw std::runtime_error("Invalid catalogue file");
            load_size_ = static_cast<std::size_t>(info.st_size);
            load_input_.reset(new std::ifstream(path));
            if (!std::getline(*load_input_, line)) throw std::runtime_error("Cannot read catalogue header");
            load_bytes_ = line.size() + 1;
            const auto header = split(line);
            if (header.size() < 3 || header[0] != "ERA-SGF-LIBRARY" || (header[1] != "1" && header[1] != "2") || (header[2] != "complete" && header[2] != "partial")) throw std::runtime_error("Invalid catalogue header");
            if (header[1] == "2") {
                if (header.size() != 4 || header[3].size() != 16) throw std::runtime_error("Invalid catalogue generation");
                generation_ = header[3];
            }
            load_complete_ = header[2] == "complete";
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        for (std::size_t i = 0; i < rows; ++i) {
            if (!std::getline(*load_input_, line)) {
                if (load_input_->bad()) throw std::runtime_error("Cannot read catalogue");
                load_input_.reset(); loaded_ = load_complete_; load_changes(); merge_pending(); ready_ = true; return true;
            }
            if (load_input_->eof()) throw std::runtime_error("Truncated catalogue row");
            load_bytes_ += line.size() + 1;
            auto r = decode(line);
            if (!by_path_.emplace(r.path, records_.size()).second) throw std::runtime_error("Duplicate catalogue path");
            records_.push_back(std::move(r));
            if (std::chrono::steady_clock::now() >= deadline) break;
        }
        return false;
    } catch (const std::bad_alloc&) {
        // Memory exhaustion is not a corrupt/empty catalogue. Never replace the
        // saved catalogue with a partial one after an allocation failure.
        load_input_.reset(); records_.clear(); by_path_.clear(); load_error_ = "Out of memory"; throw;
    } catch (const std::exception& e) {
        load_input_.reset(); records_.clear(); by_path_.clear(); generation_.clear(); loaded_ = false;
        load_error_ = e.what(); record_diagnostic(state_, load_error_); return true;
    }
}
void Library::load_changes() {
    // An atomic, checksummed delta belongs to exactly one snapshot generation.
    // A stale delta after successful snapshot replacement must never be replayed.
    try {
        struct stat info{}; const auto path = state_ + "/library.changes";
        if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || generation_.empty()) return;
        const auto raw = read_file(path, 4 * 1024 * 1024); const auto newline = raw.find('\n');
        if (newline == std::string::npos) throw std::runtime_error("Invalid change header");
        const auto header = split(raw.substr(0, newline));
        if (header.size() != 4 || header[0] != "ERA-SGF-CHANGES" || header[1] != "1") throw std::runtime_error("Invalid change header");
        if (header[2] != generation_) return;
        const auto body = raw.substr(newline + 1);
        if (fingerprint(body) != header[3] || body.empty() || body.back() != '\n') throw std::runtime_error("Damaged catalogue changes");
        std::vector<std::pair<std::string, LibraryRecord>> pending;
        std::istringstream input(body); std::string line;
        while (std::getline(input, line)) {
            if (line.compare(0, 2, "U\t") == 0) pending.push_back({"", decode(line.substr(2))});
            else if (line.compare(0, 2, "D\t") == 0) pending.push_back({relative(unhex(line.substr(2))), {}});
            else throw std::runtime_error("Invalid catalogue change");
        }
        for (auto& change : pending) {
            if (change.first.empty()) {
                auto found = by_path_.find(change.second.path);
                if (found == by_path_.end()) { by_path_[change.second.path] = records_.size(); records_.push_back(std::move(change.second)); }
                else records_[found->second] = std::move(change.second);
            } else {
                auto found = by_path_.find(change.first);
                if (found == by_path_.end()) continue;
                const auto i = found->second; by_path_.erase(found);
                if (i != records_.size() - 1) { records_[i] = std::move(records_.back()); by_path_[records_[i].path] = i; }
                records_.pop_back();
            }
        }
        changes_ = body; change_count_ = pending.size();
    } catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& e) { record_diagnostic(state_, e.what()); }
}
void Library::save() {
    static unsigned serial = 0;
    const auto generation = fingerprint(std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ':' + std::to_string(::getpid()) + ':' + std::to_string(++serial));
    std::size_t i = 0, bytes = 0; bool header = false;
    write_file_atomic_chunks(state_ + "/library.db", [&] {
        std::string chunk;
        if (!header) { chunk = std::string("ERA-SGF-LIBRARY\t2\t") + (loaded_ ? "complete" : "partial") + '\t' + generation + '\n'; header = true; }
        while (i < records_.size() && chunk.size() < 65536) chunk += encode(records_[i++]) + '\n';
        bytes += chunk.size(); if (bytes > 256 * 1024 * 1024) throw std::runtime_error("Catalogue exceeds 256 MiB");
        return chunk;
    });
    generation_ = generation; changes_.clear(); change_count_ = 0;
    // This is a disposable cache. Failure must not roll back an already committed
    // catalogue; its generation check prevents use of an obsolete cache.
    try { rebuild_random_index(); } catch (const std::exception& e) { record_diagnostic(state_, e.what()); }
}
void Library::save_changes(const std::string& previous_path, const LibraryRecord* record) {
    // Mutations have already been staged in memory; snapshot compaction therefore
    // commits the same transaction as writing its small delta would.
    if (generation_.empty() || change_count_ >= 512 || changes_.size() > 1024 * 1024) { save(); return; }
    struct stat info{};
    if (::lstat((state_ + "/library.db").c_str(), &info) != 0 || !S_ISREG(info.st_mode)) throw std::runtime_error("Catalogue snapshot is unavailable");
    auto next = changes_; std::size_t count = 0;
    if (!previous_path.empty()) { next += "D\t" + hex(previous_path) + '\n'; ++count; }
    if (record) { next += "U\t" + encode(*record) + '\n'; ++count; }
    if (next.size() > 1024 * 1024) { save(); return; }
    write_file_atomic(state_ + "/library.changes", "ERA-SGF-CHANGES\t1\t" + generation_ + '\t' + fingerprint(next) + '\n' + next);
    changes_ = std::move(next); change_count_ += count;
}
std::string Library::pending_path(const std::string& path) const {
    return state_ + "/library.pending/" + fingerprint(path) + ".record";
}
std::pair<LibraryRecord, int> Library::read_pending(const std::string& path) const {
    auto pending = read_pending_file(pending_path(path));
    if (!pending.first.path.empty() && pending.first.path != path) throw std::runtime_error("Pending game identity mismatch");
    return pending;
}
std::pair<LibraryRecord, int> Library::read_pending_file(const std::string& filename) const {
    struct stat info{};
    if (::lstat(filename.c_str(), &info) != 0) {
        if (errno == ENOENT) return {{}, -1};
        fail("Cannot inspect pending game");
    }
    if (!S_ISREG(info.st_mode)) throw std::runtime_error("Invalid pending game file");
    const auto raw = read_file(filename, 64 * 1024 * 1024); const auto end = raw.find('\n');
    if (end == std::string::npos) throw std::runtime_error("Invalid pending game header");
    const auto header = split(raw.substr(0, end)); const auto body = raw.substr(end + 1);
    if (header.size() != 4 || header[0] != "ERA-SGF-PENDING" || header[1] != "1" ||
        (header[2] != "-1" && header[2] != "0" && header[2] != "1") || body.empty() || body.back() != '\n' ||
        fingerprint(body) != header[3]) throw std::runtime_error("Damaged pending game");
    auto record = decode(body.substr(0, body.size() - 1));
    if (pending_path(record.path) != filename) throw std::runtime_error("Pending game identity mismatch");
    return {std::move(record), std::stoi(header[2])};
}
int Library::pending_viewed(const std::string& path) const { return read_pending(relative(path)).second; }
void Library::write_pending(const LibraryRecord& record, int viewed) {
    const auto filename = pending_path(record.path); const auto folder = parent_directory(filename);
    // Atomic file rename also needs durable directory entries. Remember newly
    // created ancestors so first use is safe when the state directory is absent.
    std::vector<std::string> missing;
    for (auto path = folder;; path = parent_directory(path)) {
        struct stat info{};
        if (::lstat(path.c_str(), &info) == 0) break;
        if (errno != ENOENT) fail("Cannot inspect pending parent directory");
        missing.push_back(path);
    }
    const auto body = encode(record) + '\n';
    write_file_atomic(filename, "ERA-SGF-PENDING\t1\t" + std::to_string(viewed) + '\t' + fingerprint(body) + '\n' + body);
    flush_directory(folder);
    for (const auto& path : missing) flush_directory(parent_directory(path));
}
void Library::for_each_pending(const std::function<void(std::pair<LibraryRecord, int>)>& consume) const {
    const auto folder = state_ + "/library.pending"; struct stat info{};
    if (::lstat(folder.c_str(), &info) != 0) { if (errno == ENOENT) return; fail("Cannot inspect pending games"); }
    if (!S_ISDIR(info.st_mode)) throw std::runtime_error("Invalid pending games directory");
    DIR* directory = ::opendir(folder.c_str()); if (!directory) fail("Cannot open pending games");
    try {
        while (const auto* entry = ::readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.empty() || name[0] == '.') continue;
            // A killed atomic writer can leave its uncommitted temporary file.
            // Keep it for recovery, but never interpret it as a committed entry.
            if (name.size() == 38 && name.substr(16, 16) == ".record.era-sgf-" &&
                name.substr(0, 16).find_first_not_of("0123456789abcdef") == std::string::npos &&
                name.substr(32).find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") == std::string::npos) continue;
            if (name.size() != 23 || name.substr(16) != ".record") throw std::runtime_error("Invalid pending game filename");
            const auto filename = folder + '/' + name;
            if (::lstat(filename.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) throw std::runtime_error("Invalid pending game file");
            auto pending = read_pending_file(filename);
            if (pending.first.path.empty()) throw std::runtime_error("Pending game disappeared during reading");
            consume(std::move(pending));
        }
    } catch (...) { ::closedir(directory); throw; }
    ::closedir(directory);
}
void Library::merge_pending() {
    const auto folder = state_ + "/library.pending";
    std::vector<std::string> consumed, changed_paths;
    for_each_pending([&](std::pair<LibraryRecord, int> pending) {
        const auto path = pending.first.path;
        auto found = by_path_.find(path);
        if (found == by_path_.end()) {
            pending.first.viewed = pending.second == 1;
            by_path_[path] = records_.size(); records_.push_back(std::move(pending.first)); changed_paths.push_back(path);
        } else {
            auto& old = records_[found->second];
            const bool same = old.source == pending.first.source;
            const bool viewed = pending.second < 0 ? same && old.viewed : pending.second == 1;
            if (!same) { pending.first.viewed = viewed; old = std::move(pending.first); changed_paths.push_back(path); }
            else if (old.viewed != viewed) { old.viewed = viewed; changed_paths.push_back(path); }
        }
        consumed.push_back(pending_path(path));
    });
    // Commit before removing the queue. Reapplying after a crash is idempotent.
    if (!changed_paths.empty()) {
        if (generation_.empty() || changed_paths.size() > 32) save();
        else for (const auto& path : changed_paths) save_changes("", &records_.at(by_path_.at(path)));
        invalidate_query();
    }
    // storage's atomic writer fsyncs the file before rename; make that rename
    // durable as well before removing the only replayable source of changes.
    if (!consumed.empty()) flush_directory(state_);
    for (const auto& filename : consumed) if (::unlink(filename.c_str()) != 0) fail("Cannot remove applied pending game");
    if (!consumed.empty()) flush_directory(folder);
}
void Library::refresh(bool force) {
    refresh_stats_ = {};
    struct stat storage_info{};
    if (::lstat(root_.c_str(), &storage_info) != 0 || !S_ISDIR(storage_info.st_mode)) throw std::runtime_error("Invalid reader storage root");
    std::map<std::string, Stamp> paths;
    scan(root_, "sgf", paths); scan(root_, "Books/Era-SGF", paths);
    for (const auto& old : records_) {
        if (paths.count(old.path)) continue;
        try { check_file(old.path); struct stat info{}; if (::lstat((root_ + '/' + old.path).c_str(), &info) == 0) paths.emplace(old.path, stamp(info)); } catch (const std::exception&) {}
    }
    refresh_stats_.discovered = paths.size();
    // The common unchanged case needs no second copy of every metadata record.
    if (!force && loaded_ && !generation_.empty() && paths.size() == records_.size()) {
        bool unchanged = true;
        for (const auto& item : paths) { const auto* old = find(item.first); if (!old || !same_stamp(*old, item.second)) { unchanged = false; break; } }
        if (unchanged) { refresh_stats_.reused = paths.size(); rebuild_random_index(); return; }
    }
    std::unordered_map<std::string, std::vector<std::size_t>> missing;
    for (std::size_t i = 0; i < records_.size(); ++i) if (!paths.count(records_[i].path)) missing[records_[i].source].push_back(i);
    std::vector<LibraryRecord> updated; updated.reserve(paths.size());
    bool changed = !loaded_ || generation_.empty() || paths.size() != records_.size();
    for (const auto& item : paths) {
        const auto& path = item.first; const auto* old = find(path);
        if (!force && old && same_stamp(*old, item.second)) { updated.push_back(*old); ++refresh_stats_.reused; continue; }
        LibraryRecord record; ++refresh_stats_.parsed; changed = true;
        try { check_file(path); record = describe(path, read_file(root_ + '/' + path)); }
        catch (const std::exception& e) {
            record.path = path; record.year = game_year("", path); record.problem = e.what();
            record.bytes = static_cast<std::size_t>(std::max(0LL, std::min(item.second.bytes, 16LL * 1024 * 1024 + 1)));
            record.source = fingerprint("unreadable:" + std::to_string(item.second.bytes) + ':' + std::to_string(item.second.modified));
        }
        apply_stamp(record, item.second);
        if (old && old->source == record.source) record.viewed = old->viewed;
        else if (!old) {
            auto found = missing.find(record.source);
            if (found != missing.end() && !found->second.empty()) { record.viewed = records_[found->second.back()].viewed; found->second.pop_back(); }
        }
        updated.push_back(std::move(record));
    }
    if (!changed) return;
    records_.swap(updated); const bool was_loaded = loaded_; loaded_ = true;
    try { save(); } catch (...) { records_.swap(updated); loaded_ = was_loaded; throw; }
    rebuild_index();
}
void Library::track(const std::string& path, const std::string& raw) {
    const auto normalized = relative(path); check_file(normalized);
    if (!ready_) {
        const auto old = read_pending(normalized);
        const auto source = fingerprint(raw);
        if (old.first.path == normalized && old.first.source == source) return;
        write_pending(describe(normalized, raw), -1);
        return;
    }
    // Opening an unchanged game must not parse it again just for the catalogue.
    const auto* old = find(normalized);
    if (old && old->source == fingerprint(raw)) return;
    auto record = describe(normalized, raw);
    if (old) {
        const auto i = index(normalized); const auto before = records_[i]; records_[i] = std::move(record);
        try { save_changes("", &records_[i]); } catch (...) { records_[i] = before; throw; }
    } else {
        records_.push_back(std::move(record));
        try { save_changes("", &records_.back()); } catch (...) { records_.pop_back(); throw; }
        by_path_[normalized] = records_.size() - 1;
    }
    invalidate_query();
}
std::vector<std::size_t> Library::query(const LibraryFilter& filter) const {
    if (query_valid_ && query_filter_.status == filter.status && query_filter_.year == filter.year && query_filter_.query == filter.query) return query_result_;
    std::vector<std::size_t> result; const auto needle = fold(filter.query);
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        if (filter.status == ReadFilter::new_only && r.viewed) continue;
        if (filter.status == ReadFilter::viewed_only && !r.viewed) continue;
        if (!filter.year.empty() && filter.year != r.year) continue;
        if (!needle.empty() && fold(r.black + '\n' + r.white + '\n' + r.date + '\n' + r.event + '\n' + r.path).find(needle) == std::string::npos) continue;
        result.push_back(i);
    }
    std::sort(result.begin(), result.end(), [&](std::size_t a, std::size_t b) {
        const auto& left = records_[a]; const auto& right = records_[b];
        return left.date != right.date ? left.date > right.date : left.path < right.path;
    });
    query_filter_ = filter; query_result_ = result; query_valid_ = true;
    return result;
}
std::vector<std::string> Library::years() const {
    std::set<std::string, std::greater<std::string>> unique;
    for (const auto& record : records_) unique.insert(record.year);
    return {unique.begin(), unique.end()};
}
std::string Library::random_unviewed(std::mt19937& random) const {
    if (!ready_) return random_game(random).path;
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < records_.size(); ++i)
        if (!records_[i].viewed && records_[i].problem.empty()) candidates.push_back(i);
    if (candidates.empty()) return {};
    const auto selected = std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(random);
    return records_[candidates[selected]].path;
}
void Library::rebuild_random_index() const {
    if (generation_.size() != 16 || load_input_) throw std::runtime_error("Load the catalogue before rebuilding its random index");
    if (records_.size() > 1000000) throw std::runtime_error("Random index exceeds record limit");
    const std::uint64_t paths_begin = 64 + records_.size() * 32ULL;
    std::uint64_t offset = paths_begin;
    std::string table; table.reserve(records_.size() * 32);
    for (const auto& record : records_) {
        if (record.path.size() > 65536) throw std::runtime_error("Random index path too long");
        put_number(table, hash_number(fingerprint(record.path)), 8);
        put_number(table, hash_number(record.source), 8);
        put_number(table, offset, 8); put_number(table, record.path.size(), 4);
        put_number(table, (record.viewed ? 1 : 0) | (record.problem.empty() ? 2 : 0), 4);
        offset += record.path.size();
    }
    if (offset > 256 * 1024 * 1024) throw std::runtime_error("Random index exceeds size limit");
    std::string header = "ESGFRI1\n" + generation_;
    put_number(header, records_.size(), 8); put_number(header, offset, 8);
    header += fingerprint(table); header.resize(64, '\0');
    unsigned phase = 0; std::size_t i = 0;
    write_file_atomic_chunks(state_ + "/library.random", [&] {
        if (phase == 0) { ++phase; return header; }
        if (phase == 1) { ++phase; if (!table.empty()) return std::move(table); }
        std::string chunk;
        while (i < records_.size() && chunk.size() < 65536) chunk += records_[i++].path;
        return chunk;
    });
    flush_directory(state_);
}
RandomGame Library::random_game(std::mt19937& random) const {
    const auto unavailable = [] { return std::runtime_error("Индекс случайных партий отсутствует или устарел. Библиотека → инструменты → Обновить список партий."); };
    std::string generation; struct stat info{};
    if (::lstat((state_ + "/library.db").c_str(), &info) == 0) {
        if (!S_ISREG(info.st_mode)) throw unavailable();
        std::ifstream snapshot(state_ + "/library.db"); std::string line;
        if (!std::getline(snapshot, line)) throw unavailable();
        const auto header = split(line);
        if (header.size() != 4 || header[0] != "ERA-SGF-LIBRARY" || header[1] != "2") throw unavailable();
        generation = header[3];
    } else if (errno != ENOENT) fail("Cannot inspect catalogue header");
    struct Row { std::uint64_t hash, source, offset; std::uint32_t length, flags; };
    std::vector<Row> rows; std::ifstream index_file;
    if (!generation.empty()) {
        const auto filename = state_ + "/library.random";
        if (::lstat(filename.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 64 || info.st_size > 256 * 1024 * 1024) throw unavailable();
        index_file.open(filename, std::ios::binary);
        const auto header = read_exact(index_file, 64);
        const auto count = get_number(header, 24, 8), size = get_number(header, 32, 8);
        if (header.substr(0, 8) != "ESGFRI1\n" || header.substr(8, 16) != generation || count > 1000000 ||
            size != static_cast<std::uint64_t>(info.st_size) || 64 + count * 32 > size) throw unavailable();
        const auto table = read_exact(index_file, static_cast<std::size_t>(count * 32));
        if (fingerprint(table) != header.substr(40, 16)) throw unavailable();
        rows.reserve(static_cast<std::size_t>(count));
        std::uint64_t next_path = 64 + count * 32;
        for (std::size_t i = 0; i < count; ++i) {
            Row row{get_number(table, i * 32, 8), get_number(table, i * 32 + 8, 8), get_number(table, i * 32 + 16, 8),
                    static_cast<std::uint32_t>(get_number(table, i * 32 + 24, 4)), static_cast<std::uint32_t>(get_number(table, i * 32 + 28, 4))};
            if (row.flags > 3 || !row.length || row.length > 65536 || row.offset != next_path || row.length > size - next_path) throw unavailable();
            next_path += row.length; rows.push_back(row);
        }
        if (next_path != size) throw unavailable();
    }
    const auto base_count = rows.size(); std::vector<std::string> added_paths;
    auto path_for = [&](std::size_t i) {
        if (i >= base_count) return added_paths.at(i - base_count);
        index_file.clear(); index_file.seekg(static_cast<std::streamoff>(rows[i].offset));
        const auto path = relative(read_exact(index_file, rows[i].length));
        if (hash_number(fingerprint(path)) != rows[i].hash) throw unavailable();
        return path;
    };
    struct Change { std::uint64_t source; int viewed; bool valid, deleted = false, applied = false; };
    std::map<std::string, Change> changes;
    const auto journal = state_ + "/library.changes";
    if (::lstat(journal.c_str(), &info) == 0 && !generation.empty()) {
        if (!S_ISREG(info.st_mode)) throw std::runtime_error("Invalid catalogue journal");
        const auto raw = read_file(journal, 4 * 1024 * 1024); const auto newline = raw.find('\n');
        if (newline == std::string::npos) throw std::runtime_error("Invalid catalogue journal");
        const auto header = split(raw.substr(0, newline));
        if (header.size() != 4 || header[0] != "ERA-SGF-CHANGES" || header[1] != "1") throw std::runtime_error("Invalid catalogue journal");
        if (header[2] == generation) {
            const auto body = raw.substr(newline + 1);
            if (body.empty() || body.back() != '\n' || fingerprint(body) != header[3]) throw std::runtime_error("Damaged catalogue journal");
            std::istringstream lines(body); std::string line;
            while (std::getline(lines, line)) {
                if (line.compare(0, 2, "U\t") == 0) {
                    auto record = decode(line.substr(2)); const auto path = record.path; const int viewed = record.viewed;
                    changes[path] = {hash_number(record.source), viewed, record.problem.empty(), false, false};
                } else if (line.compare(0, 2, "D\t") == 0) changes[relative(unhex(line.substr(2)))] = {0, 0, false, true, false};
                else throw std::runtime_error("Invalid catalogue journal operation");
            }
        }
    } else if (errno != ENOENT && !generation.empty()) fail("Cannot inspect catalogue journal");
    for_each_pending([&](std::pair<LibraryRecord, int> pending) {
        const auto& path = pending.first.path; const auto source = hash_number(pending.first.source);
        auto old = changes.find(path);
        if (pending.second < 0 && old != changes.end()) pending.second = !old->second.deleted &&
            old->second.source == source && old->second.viewed == 1;
        changes[path] = {source, pending.second, pending.first.problem.empty(), false, false};
    });
    std::unordered_map<std::uint64_t, std::vector<std::pair<const std::string, Change>*>> by_hash;
    for (auto& change : changes) by_hash[hash_number(fingerprint(change.first))].push_back(&change);
    for (std::size_t i = 0; i < base_count; ++i) {
        const auto found = by_hash.find(rows[i].hash); if (found == by_hash.end()) continue;
        const auto path = path_for(i);
        for (auto* item : found->second) if (item->first == path) {
            auto& change = item->second; change.applied = true;
            if (change.deleted) { rows[i].flags = 4; continue; }
            const auto source = change.source;
            const bool viewed = change.viewed < 0 ? source == rows[i].source && (rows[i].flags & 1) : change.viewed == 1;
            rows[i].source = source; rows[i].flags = (viewed ? 1 : 0) | (change.valid ? 2 : 0);
        }
    }
    for (const auto& item : changes) if (!item.second.applied && !item.second.deleted) {
        const auto& change = item.second;
        rows.push_back({hash_number(fingerprint(item.first)), change.source, 0, 0,
                        static_cast<std::uint32_t>((change.viewed == 1 ? 1 : 0) | (change.valid ? 2 : 0))});
        added_paths.push_back(item.first);
    }
    RandomGame result; std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].flags == 4) continue;
        ++result.total;
        if (!(rows[i].flags & 1)) { ++result.unviewed; if (rows[i].flags & 2) candidates.push_back(i); }
    }
    // A stale archive must not cause tens of thousands of filesystem probes.
    // The clock is checked between probes; an individual OS I/O call is not cancellable here.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!candidates.empty()) {
        if (result.attempts >= 32 || (result.attempts && std::chrono::steady_clock::now() >= deadline)) {
            result.search_limited = true;
            break;
        }
        ++result.attempts;
        const auto selected = std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(random);
        const auto i = candidates[selected]; candidates[selected] = candidates.back(); candidates.pop_back();
        const auto path = path_for(i);
        try { check_file(path); std::ifstream probe(root_ + '/' + path, std::ios::binary); if (!probe) continue; }
        catch (const std::exception&) { continue; }
        result.path = path; break;
    }
    return result;
}
std::string Library::verified(std::size_t i) const {
    const auto& r = records_.at(i); check_file(r.path);
    const auto raw = read_file(root_ + '/' + r.path);
    if (fingerprint(raw) != r.source) throw std::runtime_error("Файл изменён. Сначала обнови библиотеку.");
    return raw;
}
void Library::set_viewed(const std::string& path, bool value) {
    if (!ready_) {
        auto pending = read_pending(relative(path));
        if (pending.first.path.empty()) throw std::runtime_error("Open the game before marking it viewed");
        if (pending.second != static_cast<int>(value)) write_pending(pending.first, value);
        return;
    }
    const auto i = index(path); if (records_[i].viewed == value) return;
    verified(i); records_[i].viewed = value;
    try { save_changes("", &records_[i]); } catch (...) { records_[i].viewed = !value; throw; }
    invalidate_query();
}
std::string Library::move_to_viewed(const std::string& path) {
    const auto i = index(path); const auto before = records_[i]; const auto raw = verified(i);
    if (before.path.compare(0, 11, "sgf/viewed/") == 0) { set_viewed(path, true); return before.path; }
    const auto folder = "sgf/viewed/" + before.year;
    make_directories(root_ + '/' + folder);
    const auto slash = before.path.find_last_of('/'); const auto name = before.path.substr(slash == std::string::npos ? 0 : slash + 1);
    std::string destination; bool copied = false;
    for (unsigned n = 1; n <= 10000; ++n) {
        destination = folder + '/' + (n == 1 ? name : name.substr(0, name.size() - 4) + " (" + std::to_string(n) + ").sgf");
        struct stat info{};
        if (::lstat((root_ + '/' + destination).c_str(), &info) == 0) continue;
        if (errno != ENOENT) fail("Cannot inspect move destination");
        exclusive_copy(root_ + '/' + destination, raw); copied = true; break;
    }
    if (!copied) throw std::runtime_error("Cannot create a unique viewed copy");
    if (read_file(root_ + '/' + destination) != raw) { ::unlink((root_ + '/' + destination).c_str()); throw std::runtime_error("Viewed copy verification failed"); }
    const auto old_absolute = root_ + '/' + before.path, new_absolute = root_ + '/' + destination;
    const auto resume = load_state(state_, old_absolute, raw);
    try {
        if (resume) { auto moved = *resume; moved.path = new_absolute; save_state(state_, moved); }
        records_[i].path = destination; records_[i].viewed = true; save_changes(before.path, &records_[i]);
        verified(i);
        check_file(before.path);
        if (fingerprint(read_file(old_absolute)) != before.source) throw std::runtime_error("Source changed while moving");
        if (::unlink(old_absolute.c_str()) != 0) fail("Cannot remove old SGF after moving");
    } catch (...) {
        records_[i] = before;
        try { save_changes(destination, &records_[i]); } catch (...) {}
        bool restored = !resume;
        try { if (resume) save_state(state_, *resume); restored = true; } catch (...) {}
        // If the resume rollback also fails, retain the copy its last pointer uses.
        if (restored) ::unlink(new_absolute.c_str());
        throw;
    }
    by_path_.erase(before.path); by_path_[destination] = i; invalidate_query();
    return destination;
}
void Library::erase(const std::string& path) {
    const auto i = index(path); verified(i); const auto before = records_[i];
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(i));
    try {
        save_changes(before.path, nullptr);
        check_file(before.path);
        if (fingerprint(read_file(root_ + '/' + before.path)) != before.source) throw std::runtime_error("Source changed while deleting");
        if (::unlink((root_ + '/' + before.path).c_str()) != 0) fail("Cannot delete selected SGF");
    } catch (...) {
        records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(i), before);
        try { save_changes("", &records_[i]); } catch (...) {}
        throw;
    }
    rebuild_index();
}
} // namespace erasgf
