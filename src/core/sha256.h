#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using Sha256Digest=std::array<uint8_t,32>;

// FIPS180-4 byte-oriented SHA-256. No platform APIs, file I/O or global state.
class Sha256 {
public:
    void update(const void* data,size_t size);
    void update(std::string_view text){update(text.data(),text.size());}
    Sha256Digest digest() const;
private:
    std::array<uint32_t,8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<uint8_t,64> buffer{};
    uint64_t bytes=0;
    size_t used=0;
    void block(const uint8_t* data);
};

std::string digestHex(const Sha256Digest& value);
