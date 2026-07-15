#pragma once

#include <vector>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cmath>
#include <iostream>

// Highly Optimized Basic Packed Memory Array (PMA)
// Uses INT32_MAX to denote empty slots.
class PMA {
private:
    static constexpr int32_t EMPTY = std::numeric_limits<int32_t>::max();
    
    std::vector<int32_t> data_;
    size_t size_;
    size_t capacity_; // Must be a power of 2
    size_t segment_size_;
    size_t num_segments_;
    size_t height_;
    
    double t_upper_leaf = 1.0;
    double t_upper_root = 0.8;
    double t_lower_leaf = 0.08;
    double t_lower_root = 0.3;

    void rebuild() {
        std::vector<int32_t> elements;
        elements.reserve(size_);
        for (auto x : data_) {
            if (x != EMPTY) {
                elements.push_back(x);
            }
        }
        
        if (size_ > capacity_ * t_upper_root) {
            capacity_ *= 2;
        } else if (size_ < capacity_ * t_lower_root && capacity_ > 1024) {
            capacity_ /= 2;
        }
        
        segment_size_ = std::max<size_t>(16, std::ceil(std::log2(capacity_)));
        num_segments_ = (capacity_ + segment_size_ - 1) / segment_size_;
        capacity_ = num_segments_ * segment_size_;
        height_ = std::ceil(std::log2(num_segments_)) + 1;
        
        data_.assign(capacity_, EMPTY);
        
        if (elements.empty()) return;
        
        double spacing = (double)capacity_ / elements.size();
        for (size_t i = 0; i < elements.size(); ++i) {
            size_t pos = std::min<size_t>(capacity_ - 1, (size_t)(i * spacing));
            while (pos < capacity_ && data_[pos] != EMPTY) pos++;
            if (pos == capacity_) {
                pos = (size_t)(i * spacing);
                while (pos > 0 && data_[pos] != EMPTY) pos--;
            }
            data_[pos] = elements[i];
        }
    }

    size_t find_pos(int32_t key) const {
        size_t left = 0, right = capacity_;
        size_t last_valid = capacity_;
        
        while (left < right) {
            size_t mid = left + (right - left) / 2;
            
            size_t scan = mid;
            while (scan < right && data_[scan] == EMPTY) scan++;
            
            if (scan == right) {
                right = mid;
            } else {
                if (data_[scan] == key) return scan;
                if (data_[scan] < key) {
                    left = scan + 1;
                } else {
                    last_valid = scan;
                    right = mid;
                }
            }
        }
        return last_valid;
    }

public:
    PMA() : size_(0), capacity_(1024), segment_size_(16) {
        num_segments_ = capacity_ / segment_size_;
        height_ = std::ceil(std::log2(num_segments_)) + 1;
        data_.assign(capacity_, EMPTY);
    }

    void insert(int32_t key) {
        if (size_ >= capacity_ * t_upper_root) {
            rebuild();
        }
        
        size_t pos = find_pos(key);
        if (pos < capacity_ && data_[pos] == key) return; // Duplicate
        
        size_t empty_pos = pos;
        while (empty_pos < capacity_ && data_[empty_pos] != EMPTY) empty_pos++;
        
        if (empty_pos < capacity_ && empty_pos - pos < segment_size_) {
            for (size_t i = empty_pos; i > pos; --i) {
                data_[i] = data_[i-1];
            }
            data_[pos] = key;
        } else {
            size_t empty_left = (pos > 0) ? pos - 1 : 0;
            while (empty_left > 0 && data_[empty_left] != EMPTY) empty_left--;
            
            if (data_[empty_left] == EMPTY && pos - empty_left < segment_size_) {
                for (size_t i = empty_left; i < pos - 1; ++i) {
                    data_[i] = data_[i+1];
                }
                data_[pos - 1] = key;
            } else {
                rebuild();
                insert(key);
                return;
            }
        }
        size_++;
    }

    int32_t search(int32_t key) const {
        size_t pos = find_pos(key);
        return (pos < capacity_ && data_[pos] == key) ? 1 : 0;
    }

    int erase(int32_t key) {
        size_t pos = find_pos(key);
        if (pos < capacity_ && data_[pos] == key) {
            data_[pos] = EMPTY;
            size_--;
            if (size_ < capacity_ * t_lower_root && capacity_ > 1024) {
                rebuild();
            }
            return 1;
        }
        return 0;
    }

    int64_t range_query(int32_t low, int32_t high) const {
        int64_t sum = 0;
        size_t pos = find_pos(low);
        while (pos < capacity_) {
            if (data_[pos] != EMPTY) {
                if (data_[pos] > high) break;
                sum += data_[pos];
            }
            pos++;
        }
        return sum;
    }
};
