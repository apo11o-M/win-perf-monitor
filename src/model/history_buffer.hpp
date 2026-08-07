#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace perfmon::model {

// Fixed-capacity circular buffer. Push is O(1), storage never grows after
// construction, and Snapshot() returns values oldest -> newest.
template <typename T>
class HistoryBuffer {
public:
    explicit HistoryBuffer(std::size_t capacity)
        : storage_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("HistoryBuffer capacity must be greater than zero");
        }
    }

    void Push(const T& value) {
        storage_[next_] = value;
        Advance();
    }

    void Push(T&& value) {
        storage_[next_] = std::move(value);
        Advance();
    }

    void Clear() noexcept {
        next_ = 0;
        size_ = 0;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept {
        return storage_.size();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] const T* Latest() const noexcept {
        if (size_ == 0) {
            return nullptr;
        }
        const std::size_t index = (next_ + storage_.size() - 1) % storage_.size();
        return &storage_[index];
    }

    [[nodiscard]] std::vector<T> Snapshot() const {
        std::vector<T> result;
        result.reserve(size_);
        if (size_ == 0) {
            return result;
        }

        const std::size_t oldest = size_ == storage_.size() ? next_ : 0;
        for (std::size_t offset = 0; offset < size_; ++offset) {
            result.push_back(storage_[(oldest + offset) % storage_.size()]);
        }
        return result;
    }

private:
    void Advance() noexcept {
        next_ = (next_ + 1) % storage_.size();
        size_ = std::min(size_ + 1, storage_.size());
    }

    std::vector<T> storage_;
    std::size_t next_ = 0;
    std::size_t size_ = 0;
};

} // namespace perfmon::model
