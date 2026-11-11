#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <random>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <functional>

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
struct RandomGame {
    std::string path;
    std::size_t total = 0, unviewed = 0, attempts = 0;
    bool search_limited = false;
};
class Library {
public:
    Library(std::string root, std::string state_directory, bool defer_load = false);
    // Each step yields after a bounded number of rows or 8 ms of decoding.
    // Queries and move/delete require completion. track/set_viewed can persist
    // single-file changes in the pending queue without loading the catalogue.
    bool load_step(std::size_t rows = 256);
    unsigned load_percent() const;
    const std::string& load_error() const { return load_error_; }
    bool ready() const { return ready_; }
    // -1 means the saved catalogue has not been consulted for this file yet.
    int pending_viewed(const std::string& path) const;
    bool loaded() const { return loaded_; }
    const std::vector<LibraryRecord>& records() const { return records_; }
    const LibraryRecord* find(const std::string& path) const;
    std::vector<std::size_t> query(const LibraryFilter& filter) const;
    std::vector<std::string> years() const;
    std::string random_unviewed(std::mt19937& random) const;
    RandomGame random_game(std::mt19937& random) const;
    void rebuild_random_index() const;
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
    bool ready_ = false;
    bool load_started_ = false, load_complete_ = false;
    std::unique_ptr<std::ifstream> load_input_;
    std::size_t load_size_ = 0, load_bytes_ = 0;
    std::string load_error_;
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
    void load_changes();
    std::string pending_path(const std::string& path) const;
    std::pair<LibraryRecord, int> read_pending(const std::string& path) const;
    void write_pending(const LibraryRecord& record, int viewed);
    void merge_pending();
    std::pair<LibraryRecord, int> read_pending_file(const std::string& filename) const;
    void for_each_pending(const std::function<void(std::pair<LibraryRecord, int>)>& consume) const;
};
std::string game_year(const std::string& date, const std::string& fallback = "");
} // namespace erasgf
