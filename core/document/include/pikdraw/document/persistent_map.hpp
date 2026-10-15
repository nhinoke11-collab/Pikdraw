#pragma once
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace pikdraw::document {

// Private spike container: value-copy roots, COW directory and changed bucket.
// Never returns mutable iterators/references that could outlive publication.
template<class Key,class Value>
class PersistentMap {
    static_assert(std::is_integral_v<Key>);
    static_assert(std::is_nothrow_move_assignable_v<Value>,"Mapped values need nonthrowing replacement for atomic COW mutation");
    static_assert(std::is_nothrow_move_constructible_v<Value>);
    static constexpr size_t BucketCount=256;
    using Bucket=std::unordered_map<Key,Value>;
    struct Directory { std::array<std::shared_ptr<Bucket>,BucketCount> buckets{}; size_t size{}; };
    std::shared_ptr<Directory> root_;
    static size_t slot(Key key) noexcept {
        uint64_t x=static_cast<uint64_t>(key)+0x9e3779b97f4a7c15ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;
        return static_cast<size_t>((x^(x>>31))&(BucketCount-1));
    }
    const Directory& directory() const noexcept {
        static const Directory empty;
        return root_ ? *root_ : empty;
    }
    void detach_root() {
        if (!root_) root_=std::make_shared<Directory>();
        else if (root_.use_count()!=1) root_=std::make_shared<Directory>(*root_);
    }
public:
    class const_iterator {
        friend class PersistentMap;
        std::shared_ptr<const Directory> owner_;
        const Directory* directory_{};
        size_t bucket_{BucketCount};
        typename Bucket::const_iterator item_{};
        void seek() {
            while (bucket_<BucketCount) {
                const auto& page=directory_->buckets[bucket_];
                if (page && !page->empty()) { item_=page->cbegin(); return; }
                ++bucket_;
            }
        }
        const_iterator(std::shared_ptr<const Directory> owner,const Directory* directory,size_t bucket)
            :owner_(std::move(owner)),directory_(directory),bucket_(bucket) { seek(); }
        const_iterator(std::shared_ptr<const Directory> owner,const Directory* directory,size_t bucket,typename Bucket::const_iterator item)
            :owner_(std::move(owner)),directory_(directory),bucket_(bucket),item_(item) {}
    public:
        using iterator_category=std::forward_iterator_tag;
        using value_type=typename Bucket::value_type;
        using difference_type=std::ptrdiff_t;
        using pointer=const value_type*;
        using reference=const value_type&;
        const_iterator()=default;
        reference operator*() const { return *item_; }
        pointer operator->() const { return std::addressof(*item_); }
        const_iterator& operator++() {
            if (bucket_>=BucketCount) return *this;
            ++item_;
            if (item_==directory_->buckets[bucket_]->cend()) { ++bucket_; seek(); }
            return *this;
        }
        const_iterator operator++(int) { auto before=*this; ++*this; return before; }
        bool operator==(const const_iterator& other) const {
            if (bucket_==BucketCount && other.bucket_==BucketCount) return true;
            if (directory_!=other.directory_ || bucket_!=other.bucket_) return false;
            return bucket_==BucketCount || item_==other.item_;
        }
    };
    [[nodiscard]] size_t size() const noexcept { return directory().size; }
    [[nodiscard]] bool empty() const noexcept { return !size(); }
    [[nodiscard]] const_iterator begin() const { return {root_,&directory(),0}; }
    [[nodiscard]] const_iterator end() const { return {root_,&directory(),BucketCount}; }
    [[nodiscard]] const_iterator find(Key key) const {
        const auto index=slot(key);
        const auto& bucket=directory().buckets[index];
        if (!bucket) return end();
        auto item=bucket->find(key);
        return item==bucket->cend() ? end() : const_iterator(root_,&directory(),index,item);
    }
    [[nodiscard]] bool contains(Key key) const { return find(key)!=end(); }
    [[nodiscard]] const Value& at(Key key) const {
        const auto item=find(key);
        if (item==end()) throw std::out_of_range("PersistentMap key");
        return item->second;
    }
    void set(Key key,Value value) {
        detach_root();
        auto& bucket=root_->buckets[slot(key)];
        // Prepare the replacement off-root so throwing Value copies never corrupt a snapshot.
        if (!bucket || bucket.use_count()!=1) {
            auto next=bucket ? std::make_shared<Bucket>(*bucket) : std::make_shared<Bucket>();
            const bool added=!next->contains(key);
            next->insert_or_assign(key,std::move(value));
            bucket=std::move(next); if (added) ++root_->size;
        } else {
            // Used by bulk construction before any publication; replacement still prepares Value first.
            const bool added=!bucket->contains(key);
            bucket->insert_or_assign(key,std::move(value));
            if (added) ++root_->size;
        }
    }
    size_t erase(Key key) {
        if (!contains(key)) return 0;
        detach_root();
        auto& bucket=root_->buckets[slot(key)];
        if (bucket.use_count()!=1) bucket=std::make_shared<Bucket>(*bucket);
        bucket->erase(key); --root_->size;
        if (bucket->empty()) bucket.reset();
        return 1;
    }
    void swap(PersistentMap& other) noexcept { root_.swap(other.root_); }
};
}
