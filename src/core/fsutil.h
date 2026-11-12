// SheetStation - UTF-8 <-> std::filesystem::path (Windows paths are wide internally)
#pragma once

#include <filesystem>
#include <string>

namespace ss {
namespace fs = std::filesystem;

inline fs::path toPath(const std::string &utf8) { return fs::u8path(utf8); }
inline std::string fromPath(const fs::path &p) {
#if defined(__cpp_char8_t)
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

} // namespace ss
