#include "catalog.h"
#include "fsutil.h"

#include <algorithm>
#include <system_error>

namespace ss {

std::vector<FileEntry> scanShareDir(const std::string &dir) {
    std::vector<FileEntry> out;
    std::error_code ec;
    fs::directory_iterator it(toPath(dir), ec), end;
    if (ec) return out;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2) || ec2) continue;
        std::string name = fromPath(it->path().filename());
        if (name.empty() || name[0] == '.' || !isSafeFileName(name)) continue;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0) continue;
        FileEntry e;
        e.name = name;
        e.size = static_cast<uint64_t>(it->file_size(ec2));
        if (ec2) continue;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(),
              [](const FileEntry &a, const FileEntry &b) { return a.name < b.name; });
    return out;
}

bool isSafeFileName(const std::string &name) {
    if (name.empty() || name.size() > 255) return false;
    if (name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

std::string joinPath(const std::string &dir, const std::string &name) {
    return fromPath(toPath(dir) / toPath(name));
}

bool ensureDir(const std::string &dir) {
    std::error_code ec;
    fs::create_directories(toPath(dir), ec);
    return fs::is_directory(toPath(dir), ec);
}

std::string uniquePath(const std::string &dir, const std::string &name) {
    fs::path base = toPath(dir);
    fs::path candidate = base / toPath(name);
    std::error_code ec;
    if (!fs::exists(candidate, ec)) return fromPath(candidate);

    fs::path n = toPath(name);
    std::string stem = fromPath(n.stem());
    std::string ext = fromPath(n.extension());
    for (int i = 1; i < 10000; ++i) {
        candidate = base / toPath(stem + " (" + std::to_string(i) + ")" + ext);
        if (!fs::exists(candidate, ec)) return fromPath(candidate);
    }
    return fromPath(base / toPath(stem + " (copy)" + ext));
}

} // namespace ss
