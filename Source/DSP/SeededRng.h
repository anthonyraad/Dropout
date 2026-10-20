#pragma once

#include <cstdint>

namespace dropper {

// SplitMix64. Every audio-thread random value is a pure function of the seed
// and integer coordinates, so block size and callback timing cannot change it.
inline std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline double hash01(std::uint64_t seed, std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t x = splitmix64(seed + a * 0x9E3779B97F4A7C15ULL + b * 0xD1B54A32D192ED03ULL);
    return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
}

inline std::uint64_t mixSeed(std::uint64_t seed) noexcept {
    std::uint64_t next = splitmix64(seed + 0xA5A5A5A5A5A5A5A5ULL);
    if (next == 0)
        next = 1;
    return next;
}

} // namespace dropper
