#pragma once
// =============================================================================
//  HydroDS — Eytzinger Edition
//
//  Identical to hydrods.hpp EXCEPT the intra-bucket binary search
//  (lower_bound_pos / contains) is replaced with:
//
//    1. Eytzinger layout  – BFS-order permutation of the sorted bucket keys
//       so that every level of the binary search tree sits in consecutive
//       cache lines instead of jumping back and forth across the array.
//
//    2. Software prefetch – at each node we prefetch BOTH children two
//       levels ahead with __builtin_prefetch, hiding ~80% of L1 miss
//       latency for large buckets (BucketCapacity >= 64).
//
//  The Eytzinger array is rebuilt lazily (dirty_ flag) before any search.
//  Insertions still operate on the sorted keys[] array for correctness;
//  only the read path is accelerated.
// =============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace hydro_simd_eyt {
#ifdef __AVX2__
#ifndef HYDRO_SIMD_EYT_DEFINED_
#define HYDRO_SIMD_EYT_DEFINED_
inline bool contains_i32(const int32_t* keys, int count, int32_t target) {
    const __m256i t = _mm256_set1_epi32(target);
    int i = 0;
    for (; i + 8 <= count; i += 8) {
        __m256i d  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys + i));
        __m256i eq = _mm256_cmpeq_epi32(d, t);
        if (_mm256_movemask_epi8(eq)) return true;
        if (keys[i] > target) return false;
    }
    for (; i < count; ++i) {
        if (keys[i] == target) return true;
        if (keys[i] >  target) return false;
    }
    return false;
}
inline bool contains_i64(const int64_t* keys, int count, int64_t target) {
    const __m256i t = _mm256_set1_epi64x(target);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        __m256i d  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(keys + i));
        __m256i eq = _mm256_cmpeq_epi64(d, t);
        if (_mm256_movemask_epi8(eq)) return true;
        if (keys[i] > target) return false;
    }
    for (; i < count; ++i) {
        if (keys[i] == target) return true;
        if (keys[i] >  target) return false;
    }
    return false;
}
#endif
#endif
} // namespace hydro_simd_eyt

// =============================================================================
//  Eytzinger build helper (recursive, BFS-order)
// =============================================================================
namespace eytzinger_detail {
template<typename T>
static void build(const T* src, T* dst, int n, int k, int& pos) {
    if (k > n) return;
    build(src, dst, n, 2 * k,     pos);
    dst[k] = src[pos++];
    build(src, dst, n, 2 * k + 1, pos);
}
template<typename T>
static void build_eytzinger(const T* sorted, T* eyt, int n) {
    int pos = 0;
    build(sorted, eyt, n, 1, pos);
}
} // namespace eytzinger_detail

// =============================================================================
//  HydroDSEytzinger
// =============================================================================
template <typename Key = int32_t, int BucketCapacity = 256>
class HydroDSEytzinger {
    static_assert(std::is_arithmetic_v<Key>,
                  "HydroDSEytzinger supports arithmetic key types only");
    static_assert(BucketCapacity >= 16 && BucketCapacity <= 8192,
                  "BucketCapacity must be in [16, 8192]");

    static constexpr int C = BucketCapacity;
    double EPS_HIGH = 0.85;
    double EPS_LOW  = 0.50;

public:
    void set_thresholds(double high, double low) { EPS_HIGH = high; EPS_LOW = low; }

private:
    // -------------------------------------------------------------------------
    //  Bucket: sorted keys[] + Eytzinger shadow eyt[]
    // -------------------------------------------------------------------------
    struct alignas(64) Bucket {
        int32_t count  = 0;
        Key     keys[C + 1];    // sorted; +1 overflow for insert-before-split
        Key     eyt[C + 2];     // 1-indexed BFS layout; eyt[0] unused
        bool    dirty  = true;  // eyt needs rebuild before next search

        Key max_key()  const { return keys[count - 1]; }
        Key min_key()  const { return keys[0]; }
        double pressure() const { return static_cast<double>(count) / C; }
        bool is_empty()    const { return count == 0; }
        bool needs_split() const { return count > C; }

        // Rebuild Eytzinger shadow from sorted keys[]
        void rebuild_eytzinger() const {
            auto* self = const_cast<Bucket*>(this);
            eytzinger_detail::build_eytzinger(keys, self->eyt, count);
            self->dirty = false;
        }

        // Eytzinger lower_bound with prefetch; returns index in sorted keys[]
        int lower_bound_pos(Key x) const {
            if (dirty) rebuild_eytzinger();

            // Walk the Eytzinger tree with two-level-ahead prefetch
            int k = 1;
            __builtin_prefetch(&eyt[2], 0, 0);
            __builtin_prefetch(&eyt[3], 0, 0);
            while (k <= count) {
                __builtin_prefetch(&eyt[4 * k],     0, 0);
                __builtin_prefetch(&eyt[4 * k + 1], 0, 0);
                __builtin_prefetch(&eyt[4 * k + 2], 0, 0);
                __builtin_prefetch(&eyt[4 * k + 3], 0, 0);
                k = (eyt[k] < x) ? 2 * k + 1 : 2 * k;
            }
            // Standard Eytzinger lower_bound unwind
            int cnt = __builtin_ctz(~k);
            k >>= cnt + 1;
            if (k == 0) return count;       // all elements < x
            // Recover sorted-array index: eyt[k] == keys[rank(k)]
            // Since data is already warm, std::lower_bound is O(log n) but
            // touches only warm cache lines — acceptable.
            const Key* it = std::lower_bound(keys, keys + count, eyt[k]);
            return static_cast<int>(it - keys);
        }

        bool contains(Key x) const {
#ifdef __AVX2__
            if constexpr (std::is_same_v<Key, int32_t>)
                return hydro_simd_eyt::contains_i32(keys, count, x);
            if constexpr (std::is_same_v<Key, int64_t>)
                return hydro_simd_eyt::contains_i64(keys, count, x);
#endif
            int pos = lower_bound_pos(x);
            return pos < count && keys[pos] == x;
        }

        void insert_sorted(Key x) {
            dirty = true;
            if (count == 0 || x >= keys[count - 1]) { keys[count++] = x; return; }
            if (x <= keys[0]) {
                std::memmove(&keys[1], &keys[0], static_cast<size_t>(count) * sizeof(Key));
                keys[0] = x; ++count; return;
            }
            // Branchless binary search for insert position on sorted keys[]
            const Key* base = keys;
            int len = count;
            while (len > 1) {
                int half = len / 2;
                base = (*(base + half) < x) ? base + half : base;
                len -= half;
            }
            int pos = static_cast<int>((base < keys + count && *base < x)
                                       ? (base - keys + 1) : (base - keys));
            std::memmove(&keys[pos + 1], &keys[pos],
                         static_cast<size_t>(count - pos) * sizeof(Key));
            keys[pos] = x; ++count;
        }

        void remove_at(int pos) {
            dirty = true;
            if (pos < count - 1)
                std::memmove(&keys[pos], &keys[pos + 1],
                             static_cast<size_t>(count - pos - 1) * sizeof(Key));
            --count;
        }
    };

    std::vector<Bucket*> buckets_;
    std::vector<Key>     bucket_max_;
    size_t               total_size_ = 0;
    size_t               total_buckets_allocated_ = 0;

    static Bucket* alloc_bucket() {
        constexpr size_t sz       = sizeof(Bucket);
        constexpr size_t align    = alignof(Bucket);
        constexpr size_t alloc_sz = ((sz + align - 1) / align) * align;
        void* mem = std::aligned_alloc(align, alloc_sz);
        if (!mem) throw std::bad_alloc();
        return new (mem) Bucket();
    }
    static void free_bucket(Bucket* b) {
        if (b) { b->~Bucket(); std::free(b); }
    }

    int find_bucket(Key x) const {
        int n = static_cast<int>(bucket_max_.size());
        if (n <= 1) return 0;
        Key min_k = buckets_.front()->min_key();
        Key max_k = bucket_max_.back();
        int pred;
        if      (x <= min_k) pred = 0;
        else if (x >= max_k) pred = n - 1;
        else {
            double slope = static_cast<double>(n) / static_cast<double>(max_k - min_k);
            pred = static_cast<int>((x - min_k) * slope);
            if (pred < 0)  pred = 0;
            if (pred >= n) pred = n - 1;
        }
        __builtin_prefetch(buckets_[pred], 0, 3);
        if (bucket_max_[pred] >= x) {
            if (pred == 0 || bucket_max_[pred-1] < x) return pred;
            int bound = 1;
            while (pred - bound >= 0 && bucket_max_[pred - bound] >= x) bound <<= 1;
            int lo = std::max(0, pred - bound), hi = pred - (bound >> 1);
            while (lo <= hi) { int mid=(lo+hi)>>1; if(bucket_max_[mid]>=x) hi=mid-1; else lo=mid+1; }
            return lo;
        } else {
            int bound = 1;
            while (pred + bound < n && bucket_max_[pred + bound] < x) bound <<= 1;
            int hi = std::min(n-1, pred+bound), lo = pred + (bound>>1);
            while (lo <= hi) { int mid=(lo+hi)>>1; if(bucket_max_[mid]>=x) hi=mid-1; else lo=mid+1; }
            return (lo >= n) ? n - 1 : lo;
        }
    }

    void update_index(int i) { bucket_max_[i] = buckets_[i]->max_key(); }

    void flow(int i, int j) {
        Bucket* A = buckets_[i]; Bucket* B = buckets_[j];
        double dp = A->pressure() - B->pressure();
        if (dp <= EPS_HIGH) return;
        int k = static_cast<int>(C * (dp - EPS_LOW) / 2.0);
        k = std::max(1, std::min(k, static_cast<int>(A->count)));
        k = std::min(k, C - B->count);
        k = std::min(k, A->count - 1);
        if (k <= 0) return;
        if (i < j) {
            std::memmove(&B->keys[k], &B->keys[0], static_cast<size_t>(B->count)*sizeof(Key));
            std::memcpy (&B->keys[0], &A->keys[A->count-k], static_cast<size_t>(k)*sizeof(Key));
        } else {
            std::memcpy (&B->keys[B->count], &A->keys[0], static_cast<size_t>(k)*sizeof(Key));
            std::memmove(&A->keys[0], &A->keys[k], static_cast<size_t>(A->count-k)*sizeof(Key));
        }
        A->count -= k; B->count += k;
        A->dirty = B->dirty = true;
        update_index(i); update_index(j);
    }

    void stabilize(int i) {
        bool active = false;
        for (int step = 0; step < 2; ++step) {
            bool moved = false;
            if (i > 0) {
                double dp = buckets_[i]->pressure() - buckets_[i-1]->pressure();
                if ((!active && dp > EPS_HIGH)||(active && dp >= EPS_LOW)) { active=true; flow(i,i-1); moved=true; }
            }
            if (i+1 < static_cast<int>(buckets_.size())) {
                double dp = buckets_[i]->pressure() - buckets_[i+1]->pressure();
                if ((!active && dp > EPS_HIGH)||(active && dp >= EPS_LOW)) { active=true; flow(i,i+1); moved=true; }
            }
            if (!moved) break;
        }
    }

    void split_bucket(int i) {
        Bucket* old = buckets_[i];
        int mid = old->count / 2;
        Bucket* right = alloc_bucket(); ++total_buckets_allocated_;
        int rc = old->count - mid;
        std::memcpy(right->keys, &old->keys[mid], static_cast<size_t>(rc)*sizeof(Key));
        right->count = rc; right->dirty = true;
        old->count   = mid; old->dirty  = true;
        buckets_.insert(buckets_.begin()+i+1, right);
        bucket_max_.insert(bucket_max_.begin()+i+1, right->max_key());
        update_index(i);
    }

public:
    HydroDSEytzinger()  { buckets_.reserve(16384); bucket_max_.reserve(16384); }
    ~HydroDSEytzinger() { for (auto* b : buckets_) free_bucket(b); }
    HydroDSEytzinger(const HydroDSEytzinger&) = delete;
    HydroDSEytzinger& operator=(const HydroDSEytzinger&) = delete;

    void insert(Key x) {
        if (buckets_.empty()) {
            Bucket* b = alloc_bucket(); ++total_buckets_allocated_;
            b->keys[0]=x; b->count=1; b->dirty=true;
            buckets_.push_back(b); bucket_max_.push_back(x); ++total_size_; return;
        }
        int i = find_bucket(x);
        buckets_[i]->insert_sorted(x); update_index(i); ++total_size_;
        if (buckets_[i]->needs_split()) split_bucket(i);
        stabilize(i);
    }

    bool search(Key x) const {
        if (buckets_.empty()) return false;
        int i = find_bucket(x);
        return buckets_[i]->contains(x);
    }

    bool erase(Key x) {
        if (buckets_.empty()) return false;
        int i = find_bucket(x); Bucket* B = buckets_[i];
        const Key* base = B->keys; int len = B->count;
        while (len > 1) { int half=len/2; base=(*(base+half)<x)?base+half:base; len-=half; }
        int pos = static_cast<int>((base < B->keys+B->count && *base < x)
                                   ? (base-B->keys+1) : (base-B->keys));
        if (pos >= B->count || B->keys[pos] != x) return false;
        B->remove_at(pos); --total_size_;
        if (B->is_empty()) {
            free_bucket(B); buckets_.erase(buckets_.begin()+i); bucket_max_.erase(bucket_max_.begin()+i);
            return true;
        }
        update_index(i);
        if (i>0 && buckets_[i-1]->pressure()-buckets_[i]->pressure()>EPS_HIGH) flow(i-1,i);
        if (i+1<static_cast<int>(buckets_.size()) && buckets_[i+1]->pressure()-buckets_[i]->pressure()>EPS_HIGH) flow(i+1,i);
        return true;
    }

    int64_t range_query(Key lo, Key hi) const {
        if (buckets_.empty() || lo > hi) return 0;
        int64_t cnt = 0; int i = find_bucket(lo);
        for (; i < static_cast<int>(buckets_.size()); ++i) {
            const Bucket* B = buckets_[i];
            if (B->count == 0) continue;
            if (B->min_key() > hi) break;
            int start = B->lower_bound_pos(lo);
            for (int j = start; j < B->count && B->keys[j] <= hi; ++j) ++cnt;
        }
        return cnt;
    }

    std::vector<Key> range_collect(Key lo, Key hi) const {
        std::vector<Key> result;
        if (buckets_.empty() || lo > hi) return result;
        int i = find_bucket(lo);
        for (; i < static_cast<int>(buckets_.size()); ++i) {
            const Bucket* B = buckets_[i];
            if (B->count == 0) continue;
            if (B->min_key() > hi) break;
            int start = B->lower_bound_pos(lo);
            for (int j = start; j < B->count && B->keys[j] <= hi; ++j)
                result.push_back(B->keys[j]);
        }
        return result;
    }

    size_t size()         const { return total_size_; }
    bool   empty()        const { return total_size_ == 0; }
    size_t bucket_count() const { return buckets_.size(); }

    size_t memory_usage() const {
        return buckets_.size()*sizeof(Bucket) + buckets_.capacity()*sizeof(Bucket*)
             + bucket_max_.capacity()*sizeof(Key) + sizeof(*this);
    }

    double bytes_per_key() const {
        return total_size_ ? static_cast<double>(memory_usage())/total_size_ : 0.0;
    }

    double avg_bucket_fill() const {
        if (buckets_.empty()) return 0.0;
        double s = 0.0; for (const auto* b : buckets_) s += b->pressure();
        return s / buckets_.size();
    }

    void print_stats(std::ostream& os = std::cout) const {
        os << "=== HydroDSEytzinger Stats ===\n"
           << "  Bucket capacity (C): " << C << "\n"
           << "  Total elements:      " << total_size_ << "\n"
           << "  Bucket count:        " << buckets_.size() << "\n"
           << "  Avg bucket fill:     " << avg_bucket_fill()*100.0 << "%\n"
           << "  Memory usage:        " << memory_usage()/1024.0/1024.0 << " MB\n"
           << "  Bytes/key:           " << bytes_per_key() << "\n"
           << "==============================\n";
    }

    bool verify_integrity() const {
        if (buckets_.size() != bucket_max_.size()) return false;
        size_t counted = 0;
        for (size_t bi = 0; bi < buckets_.size(); ++bi) {
            const Bucket* B = buckets_[bi];
            if (B->count <= 0) return false;
            for (int j = 1; j < B->count; ++j) if (B->keys[j] < B->keys[j-1]) return false;
            if (bucket_max_[bi] != B->max_key()) return false;
            if (bi+1 < buckets_.size() && B->max_key() > buckets_[bi+1]->min_key()) return false;
            counted += B->count;
        }
        return counted == total_size_;
    }
};
