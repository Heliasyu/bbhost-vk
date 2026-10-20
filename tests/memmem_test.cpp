// rc6: host_memmem (core/portable.cpp, SSE2) against the byte loop it
// replaced: the same first match, or none, for random haystacks over small
// alphabets (many partial matches), needles of 1 to 40 bytes, every
// alignment and every haystack end near a match.
#include "core/portable.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

static const void* naive(const void* hay, std::size_t hay_len, const void* needle, std::size_t needle_len) {
    if (!needle_len) return hay;
    if (!hay || hay_len < needle_len) return nullptr;
    const auto* h = static_cast<const unsigned char*>(hay);
    const auto* n = static_cast<const unsigned char*>(needle);
    for (std::size_t i = 0; i + needle_len <= hay_len; ++i) {
        if (h[i] == n[0] && std::memcmp(h + i, n, needle_len) == 0) return h + i;
    }
    return nullptr;
}

int main() {
    std::mt19937_64 rng(0xbb6);
    long checks = 0, bad = 0, found = 0;
    for (int round = 0; round < 4000; ++round) {
        const int alphabet = 1 + static_cast<int>(rng() % 4);
        const std::size_t len = rng() % 300;
        std::vector<unsigned char> hay(len + 64);
        for (auto& c : hay) c = static_cast<unsigned char>('a' + rng() % alphabet);
        const std::size_t nlen = 1 + rng() % 40;
        std::vector<unsigned char> needle(nlen);
        if (len >= nlen && rng() % 2) {
            const std::size_t at = rng() % (len - nlen + 1);
            std::memcpy(needle.data(), hay.data() + at, nlen);
        } else {
            for (auto& c : needle) c = static_cast<unsigned char>('a' + rng() % alphabet);
        }
        for (std::size_t off = 0; off < 17 && off <= len; ++off) {
            for (std::size_t cut = 0; cut < 3 && cut <= len - off; ++cut) {
                const std::size_t hl = len - off - cut;
                const void* a = host_memmem(hay.data() + off, hl, needle.data(), nlen);
                const void* b = naive(hay.data() + off, hl, needle.data(), nlen);
                ++checks;
                found += b != nullptr;
                if (a != b && bad++ < 5) std::fprintf(stderr, "memmem_test: round %d off %zu len %zu needle %zu differs\n", round, off, hl, nlen);
            }
        }
    }
    // The camera's search: the name inside a large region, near both ends.
    static const char kName[] = "LockCamParam";
    std::vector<unsigned char> big(1 << 20, 0);
    for (std::size_t at : {std::size_t(0), std::size_t(5), std::size_t(4093), big.size() - sizeof(kName)}) {
        std::fill(big.begin(), big.end(), 'L');
        std::memcpy(big.data() + at, kName, sizeof(kName));
        ++checks;
        if (host_memmem(big.data(), big.size(), kName, sizeof(kName)) != naive(big.data(), big.size(), kName, sizeof(kName))) ++bad;
    }
    std::printf("memmem_test: %ld checks (%ld with a match), %ld differ\n", checks, found, bad);
    return bad ? 1 : 0;
}
