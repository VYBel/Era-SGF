#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <random>
#include <unordered_map>

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
    long long modified = 0, changed = 0, modified_ns = 0, changed_ns = 0;
};
struct RefreshStats { std::size_t discovered = 0, reused = 0, parsed = 0; };
class Library {
public:
    Library(std::string root, std::string state_directory);
    bool loaded() const { return loaded_; }
    const std::vector<LibraryRecord>& records() const { return records_; }
    const LibraryRecord* find(const std::string& path) const;
    std::vector<std::size_t> query(const LibraryFilter& filter) const;
    std::vector<std::string> years() const;
    std::string random_unviewed(std::mt19937& random) const;
    void refresh(bool force = false);
    const RefreshStats& refresh_stats() const { return refresh_stats_; }
    void track(const std::string& path, const std::string& raw);
    void set_viewed(const std::string& path, bool value);
    std::string move_to_viewed(const std::string& path);
    void erase(const std::string& path);
private:
    std::string root_, state_;
    std::vector<LibraryRecord> records_;
    bool loaded_ = false;
    std::unordered_map<std::string, std::size_t> by_path_;
    RefreshStats refresh_stats_;
    std::string generation_, changes_;
    std::size_t change_count_ = 0;
    mutable bool query_valid_ = false;
    mutable LibraryFilter query_filter_;
    mutable std::vector<std::size_t> query_result_;
    void rebuild_index();
    void invalidate_query();
    void save_changes(const std::string& previous_path, const LibraryRecord* record);
    std::string encode(const LibraryRecord& record) const;
    LibraryRecord decode(const std::string& line) const;
    std::string relative(const std::string& path) const;
    void check_file(const std::string& path) const;
    std::size_t index(const std::string& path) const;
    LibraryRecord describe(const std::string& path, const std::string& raw) const;
    std::string verified(std::size_t index) const;
    void save();
    void load();
};
std::string game_year(const std::string& date, const std::string& fallback = "");
} // namespace erasgf
