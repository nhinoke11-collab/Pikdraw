#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/model/validation.hpp"
#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pikdraw::document {

// Immutable AVL roots share unchanged nodes and source objects across revisions.
class PersistentObjectMap {
    using Entry = std::pair<ObjectId, ObjectVariant>;
    using EntryPtr = std::shared_ptr<const Entry>;
    struct Node;
    using NodePtr = std::shared_ptr<const Node>;
    struct Node {
        EntryPtr entry;
        NodePtr left;
        NodePtr right;
        int height;
        Node(EntryPtr value, NodePtr lhs, NodePtr rhs)
            : entry(std::move(value)), left(std::move(lhs)), right(std::move(rhs)),
              height(1 + std::max(left ? left->height : 0, right ? right->height : 0)) {}
    };

public:
    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<ObjectId, ObjectVariant>;
        using difference_type = std::ptrdiff_t;
        using reference = std::pair<const ObjectId&, const ObjectVariant&>;
        using pointer = void;

        ConstIterator() = default;
        ConstIterator(NodePtr root, bool reverse) : root_(std::move(root)), reverse_(reverse) {
            descend(root_.get());
        }
        reference operator*() const {
            const auto& entry = *stack_.back()->entry;
            return {entry.first, entry.second};
        }
        ConstIterator& operator++() {
            const auto* node = stack_.back();
            stack_.pop_back();
            descend(reverse_ ? node->left.get() : node->right.get());
            return *this;
        }
        ConstIterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }
        bool operator==(const ConstIterator& other) const noexcept {
            if (stack_.empty() || other.stack_.empty()) return stack_.empty() == other.stack_.empty();
            return stack_.back() == other.stack_.back() && reverse_ == other.reverse_;
        }
    private:
        void descend(const Node* node) {
            while (node) {
                stack_.push_back(node);
                node = reverse_ ? node->right.get() : node->left.get();
            }
        }
        NodePtr root_;
        std::vector<const Node*> stack_;
        bool reverse_{false};
    };

    class View {
    public:
        View(NodePtr root, size_t count) : root_(std::move(root)), count_(count) {}
        ConstIterator begin() const { return {root_, false}; }
        ConstIterator end() const { return {}; }
        ConstIterator rbegin() const { return {root_, true}; }
        ConstIterator rend() const { return {}; }
        size_t size() const noexcept { return count_; }
        bool empty() const noexcept { return count_ == 0; }
    private:
        NodePtr root_;
        size_t count_;
    };

    [[nodiscard]] bool contains(ObjectId id) const noexcept { return find(id) != nullptr; }
    // Borrowed immutable value, valid while this root (or a snapshot holding it) remains alive.
    [[nodiscard]] const ObjectVariant* lookup_ref(ObjectId id) const noexcept {
        const auto* node = find(id);
        return node ? &node->entry->second : nullptr;
    }
    [[nodiscard]] std::optional<ObjectVariant> get(ObjectId id) const {
        const auto* node = find(id);
        if (!node) return std::nullopt;
        return node->entry->second;
    }
    // A lifetime-owning view; iteration does not copy geometry or materialize a std::map.
    [[nodiscard]] View all() const { return {root_, count_}; }
    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    [[nodiscard]] PersistentObjectMap set(ObjectId id, ObjectVariant object) const {
        validate_object_source(id, object);
        PersistentObjectMap result;
        result.count_ = count_ + (contains(id) ? 0 : 1);
        result.root_ = insert(root_, std::make_shared<const Entry>(id, std::move(object)));
        return result;
    }
    [[nodiscard]] PersistentObjectMap remove(ObjectId id) const {
        if (!contains(id)) return *this;
        PersistentObjectMap result;
        result.count_ = count_ - 1;
        result.root_ = erase(root_, id);
        return result;
    }

private:
    static int height(const NodePtr& node) noexcept { return node ? node->height : 0; }
    static NodePtr make(EntryPtr entry, NodePtr left, NodePtr right) {
        return std::make_shared<const Node>(std::move(entry), std::move(left), std::move(right));
    }
    static NodePtr rotate_left(const NodePtr& node) {
        const auto& pivot = node->right;
        return make(pivot->entry, make(node->entry, node->left, pivot->left), pivot->right);
    }
    static NodePtr rotate_right(const NodePtr& node) {
        const auto& pivot = node->left;
        return make(pivot->entry, pivot->left, make(node->entry, pivot->right, node->right));
    }
    static NodePtr balance(NodePtr node) {
        const auto delta = height(node->left) - height(node->right);
        if (delta > 1) {
            if (height(node->left->right) > height(node->left->left))
                node = make(node->entry, rotate_left(node->left), node->right);
            return rotate_right(node);
        }
        if (delta < -1) {
            if (height(node->right->left) > height(node->right->right))
                node = make(node->entry, node->left, rotate_right(node->right));
            return rotate_left(node);
        }
        return node;
    }
    static NodePtr insert(const NodePtr& node, const EntryPtr& entry) {
        if (!node) return make(entry, {}, {});
        if (entry->first == node->entry->first) return make(entry, node->left, node->right);
        if (entry->first < node->entry->first)
            return balance(make(node->entry, insert(node->left, entry), node->right));
        return balance(make(node->entry, node->left, insert(node->right, entry)));
    }
    static NodePtr erase(const NodePtr& node, ObjectId id) {
        if (id < node->entry->first)
            return balance(make(node->entry, erase(node->left, id), node->right));
        if (id > node->entry->first)
            return balance(make(node->entry, node->left, erase(node->right, id)));
        if (!node->left) return node->right;
        if (!node->right) return node->left;
        auto successor = node->right;
        while (successor->left) successor = successor->left;
        return balance(make(successor->entry, node->left, erase(node->right, successor->entry->first)));
    }
    const Node* find(ObjectId id) const noexcept {
        auto* node = root_.get();
        while (node) {
            if (id == node->entry->first) return node;
            node = id < node->entry->first ? node->left.get() : node->right.get();
        }
        return nullptr;
    }

    NodePtr root_;
    size_t count_{0};
};

} // namespace pikdraw::document
