// SheetStation - share-folder helpers (UTF-8 paths everywhere)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ss {

struct FileEntry {
    std::string name; // plain file name, no directories
    uint64_t size = 0;
};

// Lists regular, non-hidden files directly inside dir (no recursion), sorted by name.
std::vector<FileEntry> scanShareDir(const std::string &dir);

// True if name is a bare file name we are willing to serve or write
// (no path separators, no "..", no control characters, not empty).
bool isSafeFileName(const std::string &name);

// dir + name joined; if the file already exists, "name (1).ext", "name (2).ext", ...
std::string uniquePath(const std::string &dir, const std::string &name);

std::string joinPath(const std::string &dir, const std::string &name);
bool ensureDir(const std::string &dir);

} // namespace ss
