#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace pikdraw::color {

/// Small, caller-synchronized LRU cache with a hard entry-count bound.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class BoundedLruCache {
public:
    explicit BoundedLruCache(std::size_t capacity) : capacity_(capacity) {}

    BoundedLruCache(const BoundedLruCache&) = delete;
    BoundedLruCache& operator=(const BoundedLruCache&) = delete;
    BoundedLruCache(BoundedLruCache&&) = delete;
    BoundedLruCache& operator=(BoundedLruCache&&) = delete;

    [[nodiscard]] std::optional<Value> get(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) return std::nullopt;
        order_.splice(order_.begin(), order_, found->second);
        return found->second->second;
    }

    void insert(Key key, Value value) {
        if (capacity_ == 0) return;
        if (const auto found = index_.find(key); found != index_.end()) {
            found->second->second = std::move(value);
            order_.splice(order_.begin(), order_, found->second);
            return;
        }
        if (order_.size() >= capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
        order_.emplace_front(std::move(key), std::move(value));
        try {
            index_.emplace(order_.front().first, order_.begin());
        } catch (...) {
            order_.pop_front();
            throw;
        }
    }

    void clear() noexcept {
        index_.clear();
        order_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }

private:
    using Entry = std::pair<Key, Value>;
    using EntryList = std::list<Entry>;

    std::size_t capacity_;
    EntryList order_;
    std::unordered_map<Key, typename EntryList::iterator, Hash> index_;
};

} // namespace pikdraw::color
