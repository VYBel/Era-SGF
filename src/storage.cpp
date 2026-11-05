#include "erasgf/storage.hpp"
#include "erasgf/encoding.hpp"
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

namespace erasgf {
namespace {
[[noreturn]] void io_error(const std::string& operation) { throw std::runtime_error(operation + ": " + std::strerror(errno)); }
void path_check(const std::string& path) { if (path.empty() || path.find('\0') != std::string::npos) throw std::runtime_error("Invalid file path"); }
bool regular_or_absent(const std::string& path, struct stat& info) {
    path_check(path);
    if (::lstat(path.c_str(), &info) != 0) {
        if (errno == ENOENT) return false;
        io_error("Cannot inspect " + path);
    }
    if (!S_ISREG(info.st_mode)) throw std::runtime_error("Refusing to replace a symlink or non-regular file: " + path);
    return true;
}
void write_all(int fd, const std::string& text, bool flush = true) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto n = ::write(fd, text.data() + offset, text.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) io_error("Write failed");
        offset += static_cast<std::size_t>(n);
    }
    if (flush && ::fsync(fd) != 0) io_error("Cannot flush file");
}
void atomic_write_chunks(const std::string& path, const std::function<std::string()>& next) {
    struct stat info{};
    const bool exists = regular_or_absent(path, info);
    std::string pattern = path + ".era-sgf-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end()); temp.push_back('\0');
    int fd = ::mkstemp(temp.data());
    if (fd < 0) io_error("Cannot create temporary file for " + path);
    try {
        if (exists) {
            struct statfs filesystem{};
            if (::fstatfs(fd, &filesystem) != 0) io_error("Cannot inspect settings filesystem");
            // FAT ownership/modes come from the mount, not from each file. The
            // reader's application UID may write there but cannot chmod a file
            // owned by the mount UID. Keep real POSIX-mode failures fatal.
            constexpr long fat_magic = 0x4d44;
            if (filesystem.f_type != fat_magic && ::fchmod(fd, info.st_mode & 0777) != 0) io_error("Cannot preserve file permissions");
        }
        for (;;) { const auto chunk = next(); if (chunk.empty()) break; write_all(fd, chunk, false); }
        if (::fsync(fd) != 0) io_error("Cannot flush file");
        if (::close(fd) != 0) { fd = -1; io_error("Cannot close file"); }
        fd = -1;
        if (::rename(temp.data(), path.c_str()) != 0) io_error("Cannot replace " + path);
    } catch (...) { if (fd >= 0) ::close(fd); ::unlink(temp.data()); throw; }
}
void atomic_write(const std::string& path, const std::string& text) {
    bool done = false;
    atomic_write_chunks(path, [&] { if (done) return std::string{}; done = true; return text; });
}
void ensure_directory(const std::string& path) {
    path_check(path);
    std::string current = path[0] == '/' ? "/" : "";
    std::istringstream in(path); std::string part;
    while (std::getline(in, part, '/')) {
        if (part.empty()) continue;
        if (!current.empty() && current.back() != '/') current += '/';
        current += part;
        if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) io_error("Cannot create " + current);
        struct stat info{};
        if (::lstat(current.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) throw std::runtime_error("Not a directory: " + current);
    }
}
std::string backup(const std::string& path, const std::string& contents) {
    for (unsigned n = 0; n < 10000; ++n) {
        const std::string name = path + ".era-sgf.bak" + (n ? "-" + std::to_string(n) : "");
        const int fd = ::open(name.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) { if (errno == EEXIST) continue; io_error("Cannot create association backup"); }
        try { write_all(fd, contents); } catch (...) { ::close(fd); ::unlink(name.c_str()); throw; }
        if (::close(fd) != 0) io_error("Cannot close association backup");
        return name;
    }
    throw std::runtime_error("Too many association backup files");
}
std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = value.find(separator, start);
        result.push_back(value.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return result;
        start = end + 1;
    }
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string merge(const std::string& old, bool install) {
    std::string out;
    bool found = false;
    std::size_t pos = 0;
    while (pos < old.size()) {
        const auto newline = old.find('\n', pos);
        const auto end = newline == std::string::npos ? old.size() : newline;
        auto line = old.substr(pos, end - pos);
        std::string eol = newline == std::string::npos ? "" : "\n";
        if (!line.empty() && line.back() == '\r') { line.pop_back(); eol = "\r" + eol; }
        const auto colon = line.find(':');
        std::string extension = trim(line.substr(0, colon));
        for (char& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
        if (extension == "sgf" && colon != std::string::npos) {
            found = true;
            auto fields = split(line, ':');
            if (fields.size() != 5) throw std::runtime_error("Malformed SGF entry in extensions.cfg; no changes made");
            std::vector<std::string> handlers;
            bool had_own = false;
            for (const auto& handler : split(fields[3], ',')) {
                if (trim(handler) == "era-sgf.app") had_own = true;
                else if (!trim(handler).empty()) handlers.push_back(handler);
            }
            if (install && had_own && fields[3].rfind("era-sgf.app", 0) == 0) out += line + eol;
            else if (!install && !had_own) out += line + eol;
            else {
                if (install) handlers.insert(handlers.begin(), "era-sgf.app");
                if (!handlers.empty()) {
                    fields[3].clear();
                    for (const auto& h : handlers) { if (!fields[3].empty()) fields[3] += ','; fields[3] += h; }
                    for (std::size_t i = 0; i < fields.size(); ++i) { if (i) out += ':'; out += fields[i]; }
                    out += eol;
                }
            }
        } else out += line + eol;
        pos = newline == std::string::npos ? old.size() : newline + 1;
    }
    if (install && !found) {
        const std::string eol = old.find("\r\n") == std::string::npos ? "\n" : "\r\n";
        if (!out.empty() && out.back() != '\n' && out.back() != '\r') out += eol;
        out += "sgf:@SGF_file:1:era-sgf.app:ICON_TXT" + eol;
    }
    return out;
}
std::string hex(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string unhex(const std::string& text) {
    if (text.size() % 2) throw std::runtime_error("Invalid saved state");
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::runtime_error("Invalid saved state"); };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2) out += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    return out;
}
std::size_t state_number(const std::string& value) {
    if (value.empty() || value.size() > 6 || value.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Invalid saved index");
    const auto result = std::stoul(value);
    if (result > 100000) throw std::runtime_error("Saved index exceeds parser limits");
    return result;
}
}

void make_directories(const std::string& path) { ensure_directory(path); }
void write_file_atomic(const std::string& path, const std::string& bytes) {
    path_check(path);
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) ensure_directory(slash ? path.substr(0, slash) : "/");
    atomic_write(path, bytes);
}
void write_file_atomic_chunks(const std::string& path, const std::function<std::string()>& next) {
    path_check(path);
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) ensure_directory(slash ? path.substr(0, slash) : "/");
    atomic_write_chunks(path, next);
}
void record_diagnostic(const std::string& directory, const std::string& message) noexcept {
    int fd = -1;
    try {
        ensure_directory(directory);
        const auto path = directory + "/diagnostics.log";
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
        if (fd < 0) return;
        struct stat info{};
        if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) { ::close(fd); return; }
        if (info.st_size > 32768 && ::ftruncate(fd, 0) != 0) { ::close(fd); return; }
        const auto now = std::time(nullptr); std::tm time{}; char stamp[32]{};
        if (::gmtime_r(&now, &time)) std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ ", &time);
        write_all(fd, std::string(stamp) + message.substr(0, 2048) + '\n');
        ::close(fd); fd = -1;
    } catch (...) { if (fd >= 0) ::close(fd); }
}

std::string read_file(const std::string& path, std::size_t max_bytes) {
    path_check(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open " + path);
    std::string result;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof buffer);
        const auto n = static_cast<std::size_t>(input.gcount());
        if (result.size() > max_bytes || n > max_bytes - result.size()) throw std::runtime_error("File exceeds the size limit: " + path);
        result.append(buffer, n);
    }
    if (input.bad()) throw std::runtime_error("Cannot read " + path);
    return result;
}

std::string fingerprint(const std::string& bytes) {
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (unsigned char b : bytes) { value ^= b; value *= UINT64_C(1099511628211); }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

AssociationResult update_association(const std::string& config_path, bool install) {
    struct stat info{};
    const bool exists = regular_or_absent(config_path, info);
    const auto old = exists ? read_file(config_path, 1024 * 1024) : std::string{};
    const auto updated = merge(old, install);
    if (old == updated) return {};
    AssociationResult result{true, exists ? backup(config_path, old) : ""};
    atomic_write(config_path, updated);
    return result;
}

Optional<FileState> load_state(const std::string& directory, const std::string& path, const std::string& source) {
    try {
        const auto data = read_file(directory + '/' + fingerprint(path) + ".state", 65536);
        std::istringstream input(data); std::string line;
        std::map<std::string, std::string> fields;
        while (std::getline(input, line)) { const auto e = line.find('='); if (e != std::string::npos) fields[line.substr(0, e)] = line.substr(e + 1); }
        if (unhex(fields.at("path")) != path || fields.at("source") != fingerprint(source)) return {};
        return FileState{path, fields.at("source"), state_number(fields.at("game")), state_number(fields.at("node")), normalize_encoding(unhex(fields.at("encoding")))};
    } catch (const std::exception&) { return {}; }
}

void save_state(const std::string& directory, const FileState& state) {
    ensure_directory(directory);
    const auto data = "path=" + hex(state.path) + "\nsource=" + state.content_fingerprint + "\ngame=" + std::to_string(state.game) + "\nnode=" + std::to_string(state.node) + "\nencoding=" + hex(normalize_encoding(state.encoding)) + '\n';
    atomic_write(directory + '/' + fingerprint(state.path) + ".state", data);
    atomic_write(directory + "/last", hex(state.path));
}

Optional<std::string> load_last_file(const std::string& directory) {
    try { auto path = unhex(read_file(directory + "/last", 32768)); path_check(path); return path; } catch (const std::exception&) { return {}; }
}
}
