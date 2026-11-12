// SheetStation - small self-contained SHA-256 (used to verify received files)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ss {

class Sha256 {
public:
    Sha256();
    void update(const void *data, size_t len);
    std::string hexDigest(); // finalizes; call once

private:
    void transform(const uint8_t block[64]);
    uint32_t m_state[8];
    uint64_t m_bitLen = 0;
    uint8_t m_buf[64];
    size_t m_bufLen = 0;
};

} // namespace ss
