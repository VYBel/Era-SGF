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
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace erasgf {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message + ": " + std::strerror(errno)); }
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
void scan(const std::string& root, const std::string& relative, std::set<std::string>& files, unsigned depth = 0) {
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
            else if (S_ISREG(info.st_mode) && sgf(name)) files.insert(path);
            if (files.size() > 20000) throw std::runtime_error("The SGF library exceeds 20000 files");
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
    for (const auto* value : {&date, &fallback}) for (std::size_t i = 0; i + 4 <= value->size(); ++i) {
        const auto candidate = value->substr(i, 4);
        if ((i && (*value)[i - 1] >= '0' && (*value)[i - 1] <= '9') || (i + 4 < value->size() && (*value)[i + 4] >= '0' && (*value)[i + 4] <= '9')) continue;
        if (candidate.find_first_not_of("0123456789") == std::string::npos && candidate >= "1800" && candidate <= "2199") return candidate;
    }
    return "unknown";
}
Library::Library(std::string root, std::string state_directory) : root_(std::move(root)), state_(std::move(state_directory)) {
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    if (root_.empty() || root_[0] != '/' || root_ == "/") throw std::runtime_error("Library requires a reader storage root");
    load();
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
    for (std::size_t i = 0; i < records_.size(); ++i) if (records_[i].path == normalized) return i;
    throw std::runtime_error("Game is absent from the catalogue; refresh the library");
}
const LibraryRecord* Library::find(const std::string& path) const {
    try { return &records_[index(path)]; } catch (const std::exception&) { return nullptr; }
}
LibraryRecord Library::describe(const std::string& path, const std::string& raw) const {
    LibraryRecord record; record.path = path; record.source = fingerprint(raw); record.bytes = raw.size();
    struct stat info{}; if (::lstat((root_ + '/' + path).c_str(), &info) == 0) record.modified = info.st_mtime;
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
void Library::load() {
    try {
        std::istringstream input(read_file(state_ + "/library.db", 32 * 1024 * 1024)); std::string line;
        if (!std::getline(input, line)) return;
        const bool complete = line == "ERA-SGF-LIBRARY\t1\tcomplete";
        if (!complete && line != "ERA-SGF-LIBRARY\t1\tpartial") return;
        std::set<std::string> paths;
        while (std::getline(input, line)) {
            const auto f = split(line); if (f.size() != 12) throw std::runtime_error("Invalid catalogue row");
            LibraryRecord r; r.path = relative(unhex(f[0])); r.source = f[1];
            if (!paths.insert(r.path).second || r.source.size() != 16 || r.source.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::runtime_error("Invalid catalogue identity");
            r.black = unhex(f[2]); r.white = unhex(f[3]); r.date = unhex(f[4]); r.event = unhex(f[5]); r.year = unhex(f[6]); r.problem = unhex(f[7]);
            if (r.year != game_year(r.date, r.path)) throw std::runtime_error("Invalid catalogue year");
            if (f[8] != "0" && f[8] != "1") throw std::runtime_error("Invalid viewed flag");
            r.viewed = f[8] == "1"; r.games = number(f[9]); r.bytes = number(f[10]); r.modified = number(f[11]);
            if (r.games > 100000 || r.bytes > 16 * 1024 * 1024 + 1 || records_.size() >= 20000) throw std::runtime_error("Catalogue limits exceeded");
            records_.push_back(std::move(r));
        }
        loaded_ = complete;
    } catch (const std::exception&) { records_.clear(); loaded_ = false; }
}
void Library::save() const {
    std::ostringstream out; out << "ERA-SGF-LIBRARY\t1\t" << (loaded_ ? "complete" : "partial") << '\n';
    for (const auto& r : records_) out << hex(r.path) << '\t' << r.source << '\t' << hex(r.black) << '\t' << hex(r.white) << '\t' << hex(r.date) << '\t' << hex(r.event) << '\t' << hex(r.year) << '\t' << hex(r.problem) << '\t' << r.viewed << '\t' << r.games << '\t' << r.bytes << '\t' << std::max(0LL, r.modified) << '\n';
    write_file_atomic(state_ + "/library.db", out.str());
}
void Library::refresh() {
    std::set<std::string> paths;
    scan(root_, "sgf", paths); scan(root_, "Books/Era-SGF", paths);
    for (const auto& old : records_) {
        try { check_file(old.path); paths.insert(old.path); } catch (const std::exception&) {}
    }
    std::vector<LibraryRecord> updated; std::set<std::string> reused;
    for (const auto& path : paths) {
        check_file(path); LibraryRecord record;
        try { record = describe(path, read_file(root_ + '/' + path)); }
        catch (const std::exception& e) {
            // A bad or unreadable record must not hide all the other games.
            struct stat info{}; ::lstat((root_ + '/' + path).c_str(), &info);
            record.path = path; record.year = game_year("", path); record.problem = e.what();
            record.bytes = static_cast<std::size_t>(std::max<off_t>(0, std::min<off_t>(info.st_size, 16 * 1024 * 1024 + 1)));
            record.modified = info.st_mtime;
            record.source = fingerprint("unreadable:" + std::to_string(info.st_size) + ':' + std::to_string(info.st_mtime));
        }
        const auto* old = find(path);
        if (old && old->source == record.source) record.viewed = old->viewed;
        else if (!old) for (const auto& missing : records_) {
            if (!paths.count(missing.path) && !reused.count(missing.path) && missing.source == record.source) {
                record.viewed = missing.viewed; reused.insert(missing.path); break;
            }
        }
        updated.push_back(std::move(record));
    }
    records_.swap(updated); const bool was_loaded = loaded_; loaded_ = true;
    try { save(); } catch (...) { records_.swap(updated); loaded_ = was_loaded; throw; }
}
void Library::track(const std::string& path, const std::string& raw) {
    const auto normalized = relative(path); check_file(normalized);
    auto record = describe(normalized, raw);
    if (const auto* old = find(normalized)) {
        if (old->source == record.source) return;
        records_[index(normalized)] = std::move(record);
    } else records_.push_back(std::move(record));
    save();
}
std::vector<std::size_t> Library::query(const LibraryFilter& filter) const {
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
    return result;
}
std::vector<std::string> Library::years() const {
    std::set<std::string, std::greater<std::string>> unique;
    for (const auto& record : records_) unique.insert(record.year);
    return {unique.begin(), unique.end()};
}
std::string Library::random_unviewed(std::mt19937& random) const {
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < records_.size(); ++i)
        if (!records_[i].viewed && records_[i].problem.empty()) candidates.push_back(i);
    if (candidates.empty()) return {};
    const auto selected = std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(random);
    return records_[candidates[selected]].path;
}
std::string Library::verified(std::size_t i) const {
    const auto& r = records_.at(i); check_file(r.path);
    const auto raw = read_file(root_ + '/' + r.path);
    if (fingerprint(raw) != r.source) throw std::runtime_error("Файл изменён. Сначала обнови библиотеку.");
    return raw;
}
void Library::set_viewed(const std::string& path, bool value) {
    const auto i = index(path); if (records_[i].viewed == value) return;
    verified(i); records_[i].viewed = value;
    try { save(); } catch (...) { records_[i].viewed = !value; throw; }
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
        records_[i].path = destination; records_[i].viewed = true; save();
        verified(i);
        check_file(before.path);
        if (fingerprint(read_file(old_absolute)) != before.source) throw std::runtime_error("Source changed while moving");
        if (::unlink(old_absolute.c_str()) != 0) fail("Cannot remove old SGF after moving");
    } catch (...) {
        records_[i] = before;
        try { save(); } catch (...) {}
        bool restored = !resume;
        try { if (resume) save_state(state_, *resume); restored = true; } catch (...) {}
        // If the resume rollback also fails, retain the copy its last pointer uses.
        if (restored) ::unlink(new_absolute.c_str());
        throw;
    }
    return destination;
}
void Library::erase(const std::string& path) {
    const auto i = index(path); verified(i); const auto before = records_;
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(i));
    try {
        save();
        check_file(before[i].path);
        if (fingerprint(read_file(root_ + '/' + before[i].path)) != before[i].source) throw std::runtime_error("Source changed while deleting");
        if (::unlink((root_ + '/' + before[i].path).c_str()) != 0) fail("Cannot delete selected SGF");
    } catch (...) { records_ = before; try { save(); } catch (...) {} throw; }
}
} // namespace erasgf
