#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace erasgf {
enum class ReadFilter { all, new_only, viewed_only };
struct LibraryFilter {
    ReadFilter status = ReadFilter::all;
    std::string year;
    std::string query;
};
struct LibraryRecord {
    std::string path, source, black, white, date, event, year, problem;
    bool viewed = false;
    std::size_t games = 1, bytes = 0;
    long long modified = 0;
};
class Library {
public:
    Library(std::string root, std::string state_directory);
    bool loaded() const { return loaded_; }
    const std::vector<LibraryRecord>& records() const { return records_; }
    const LibraryRecord* find(const std::string& path) const;
    std::vector<std::size_t> query(const LibraryFilter& filter) const;
    std::vector<std::string> years() const;
    void refresh();
    void track(const std::string& path, const std::string& raw);
    void set_viewed(const std::string& path, bool value);
    std::string move_to_viewed(const std::string& path);
    void erase(const std::string& path);
private:
    std::string root_, state_;
    std::vector<LibraryRecord> records_;
    bool loaded_ = false;
    std::string relative(const std::string& path) const;
    void check_file(const std::string& path) const;
    std::size_t index(const std::string& path) const;
    LibraryRecord describe(const std::string& path, const std::string& raw) const;
    std::string verified(std::size_t index) const;
    void save() const;
    void load();
};
std::string game_year(const std::string& date, const std::string& fallback = "");
} // namespace erasgf
