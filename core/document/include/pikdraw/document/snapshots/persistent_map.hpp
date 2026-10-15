#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/model/object.hpp"
#include <map>
#include <memory>
#include <optional>

namespace pikdraw::document {

/// Copy-On-Write persistent map holding document objects with structural sharing
class PersistentObjectMap {
public:
    PersistentObjectMap() : data_(std::make_shared<std::map<ObjectId, ObjectVariant>>()) {}

    [[nodiscard]] bool contains(ObjectId id) const {
        return data_->find(id) != data_->end();
    }

    [[nodiscard]] std::optional<ObjectVariant> get(ObjectId id) const {
        auto it = data_->find(id);
        if (it != data_->end()) return it->second;
        return std::nullopt;
    }

    [[nodiscard]] const std::map<ObjectId, ObjectVariant>& all() const noexcept {
        return *data_;
    }

    [[nodiscard]] size_t size() const noexcept {
        return data_->size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_->empty();
    }

    /// Sets or replaces an object, returning a new persistent map sharing unmodified nodes
    [[nodiscard]] PersistentObjectMap set(ObjectId id, ObjectVariant obj) const {
        auto new_data = std::make_shared<std::map<ObjectId, ObjectVariant>>(*data_);
        (*new_data)[id] = std::move(obj);
        PersistentObjectMap next;
        next.data_ = std::move(new_data);
        return next;
    }

    /// Removes an object, returning a new persistent map
    [[nodiscard]] PersistentObjectMap remove(ObjectId id) const {
        if (!contains(id)) return *this;
        auto new_data = std::make_shared<std::map<ObjectId, ObjectVariant>>(*data_);
        new_data->erase(id);
        PersistentObjectMap next;
        next.data_ = std::move(new_data);
        return next;
    }

private:
    std::shared_ptr<const std::map<ObjectId, ObjectVariant>> data_;
};

} // namespace pikdraw::document
