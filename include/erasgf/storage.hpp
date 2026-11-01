#pragma once

#include <cstddef>
#include "erasgf/compat.hpp"
#include <string>

namespace erasgf {
std::string read_file(const std::string& path, std::size_t max_bytes = 16 * 1024 * 1024);
std::string fingerprint(const std::string& bytes);
struct AssociationResult { bool changed = false; std::string backup; };
AssociationResult update_association(const std::string& config_path, bool install);
struct FileState {
    std::string path;
    std::string content_fingerprint;
    std::size_t game = 0;
    std::size_t node = 0;
    std::string encoding = "auto";
};
Optional<FileState> load_state(const std::string& directory, const std::string& path, const std::string& source);
void save_state(const std::string& directory, const FileState& state);
Optional<std::string> load_last_file(const std::string& directory);
} // namespace erasgf
