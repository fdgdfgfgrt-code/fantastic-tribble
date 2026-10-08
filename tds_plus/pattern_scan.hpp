// pattern_scan.hpp — byte pattern matcher for data that arrives in consecutive chunks.
// A match that straddles a chunk border is reported exactly once; wildcards are std::nullopt.
// No platform dependencies, so it can be tested natively.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class PatternScanner {
public:
    explicit PatternScanner(std::vector<std::optional<uint8_t>> pattern) : pat_(std::move(pattern)) {}

    // Forget buffered bytes: call when the next chunk does not directly follow the previous one
    // (unreadable or partially read chunk).
    void reset() { tail_.clear(); }

    // Feed the next chunk (data lives at address `addr`); on_hit(address) is called per match.
    template <class OnHit>
    void feed(uintptr_t addr, const uint8_t* data, size_t n, OnHit on_hit) {
        const size_t m = pat_.size();
        if (m == 0) return;

        if (!tail_.empty()) {  // matches that start in the previous chunk and end in this one
            std::vector<uint8_t> join(tail_);
            join.insert(join.end(), data, data + std::min(n, m - 1));
            for (size_t i = 0; i < tail_.size() && i + m <= join.size(); ++i)
                if (matches(join.data() + i)) on_hit(tail_addr_ + i);
        }
        for (size_t i = 0; i + m <= n; ++i)  // matches entirely inside this chunk
            if (matches(data + i)) on_hit(addr + i);

        const size_t keep = std::min(m - 1, tail_.size() + n);
        std::vector<uint8_t> next;
        uintptr_t next_addr;
        if (n >= keep) {
            next.assign(data + (n - keep), data + n);
            next_addr = addr + (n - keep);
        } else {
            const size_t from_old = keep - n;
            next.assign(tail_.end() - from_old, tail_.end());
            next.insert(next.end(), data, data + n);
            next_addr = tail_addr_ + (tail_.size() - from_old);
        }
        tail_ = std::move(next);
        tail_addr_ = next_addr;
    }

private:
    bool matches(const uint8_t* p) const {
        for (size_t j = 0; j < pat_.size(); ++j)
            if (pat_[j] && p[j] != *pat_[j]) return false;
        return true;
    }

    std::vector<std::optional<uint8_t>> pat_;
    std::vector<uint8_t> tail_;  // last pattern.size()-1 bytes of the previous chunk
    uintptr_t tail_addr_ = 0;
};
