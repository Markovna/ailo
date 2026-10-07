#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// Little-endian binary writer/reader for the asset package formats.
// Packages are a magic, a version, then chunks: u32 tag | u32 size | payload (size bytes).
// Readers skip unknown chunks, so chunks can be added without breaking them.
namespace ailo::binary {

constexpr uint32_t makeTag(const char (&s)[5]) {
    return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24;
}

class Writer {
public:
    void u8(uint8_t v) { m_data.push_back(v); }
    void u32(uint32_t v) { bytes(&v, sizeof(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void string(const std::string& s) { u32(uint32_t(s.size())); bytes(s.data(), s.size()); }
    void bytes(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        m_data.insert(m_data.end(), b, b + n);
    }

    void beginChunk(uint32_t tag) {
        u32(tag);
        m_chunkSizeOffset = m_data.size();
        u32(0);
    }
    void endChunk() {
        uint32_t size = uint32_t(m_data.size() - m_chunkSizeOffset - sizeof(uint32_t));
        std::memcpy(m_data.data() + m_chunkSizeOffset, &size, sizeof(size));
    }

    std::vector<uint8_t> take() { return std::move(m_data); }

private:
    std::vector<uint8_t> m_data;
    size_t m_chunkSizeOffset = 0;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : m_data(data) {}

    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos >= m_data.size(); }

    uint8_t u8() { uint8_t v = 0; bytes(&v, 1); return v; }
    uint32_t u32() { uint32_t v = 0; bytes(&v, sizeof(v)); return v; }
    bool boolean() { return u8() != 0; }
    std::string string() {
        uint32_t n = u32();
        if (!check(n)) return {};
        std::string s(reinterpret_cast<const char*>(m_data.data() + m_pos), n);
        m_pos += n;
        return s;
    }
    void bytes(void* out, size_t n) {
        if (!check(n)) { std::memset(out, 0, n); return; }
        std::memcpy(out, m_data.data() + m_pos, n);
        m_pos += n;
    }
    std::span<const uint8_t> sub(size_t n) {
        if (!check(n)) return {};
        auto s = m_data.subspan(m_pos, n);
        m_pos += n;
        return s;
    }

    template<typename E>
    E enumeration(E maxValue) {
        uint8_t v = u8();
        if (v > uint8_t(maxValue)) m_ok = false;
        return E(v);
    }

private:
    bool check(size_t n) {
        if (!m_ok || m_data.size() - m_pos < n) {
            m_ok = false;
            return false;
        }
        return true;
    }

    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
    bool m_ok = true;
};

}
