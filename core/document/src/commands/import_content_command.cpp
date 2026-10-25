#include "pikdraw/document/commands/import_content_command.hpp"

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/structure.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pikdraw::document {

CommandResult ImportPagesCommand::execute(Document& doc) {
    if (executed_) return CommandResult::fail("Import pages command is already applied");
    if (pages_.empty()) return CommandResult::fail("Import page set is empty");

    const auto current_pages = doc.pages();
    size_t insertion_index = current_pages.size();
    if (!replace_document_pages_) {
        if (insert_after_page_id_ == kInvalidPageId)
            insert_after_page_id_ = doc.active_page_id();
        const auto anchor = std::find_if(current_pages.begin(), current_pages.end(),
            [&](const auto& page) { return page.id == insert_after_page_id_; });
        if (anchor == current_pages.end())
            return CommandResult::fail("Import insertion page no longer exists");
        insertion_index = static_cast<size_t>(std::distance(current_pages.begin(), anchor)) + 1;
    }

    if (!prepared_) {
        std::unordered_set<ObjectId> existing_object_ids;
        for (const auto& page : current_pages)
            for (const auto& [id, object] : page.objects.all()) {
                (void)object;
                existing_object_ids.insert(id);
            }
        std::unordered_set<ObjectId> imported_object_ids;
        std::vector<PersistentObjectMap> object_maps;
        object_maps.reserve(pages_.size());

        for (const auto& imported : pages_) {
            if (imported.name.empty())
                return CommandResult::fail("Imported page name cannot be empty");
            PersistentObjectMap objects;
            for (const auto& value : imported.objects) {
                const ObjectId id = std::visit(
                    [](const auto& object) { return object.id; }, value);
                if (id == kInvalidObjectId || existing_object_ids.contains(id) ||
                    !imported_object_ids.insert(id).second)
                    return CommandResult::fail("Imported pages contain an invalid or duplicate object ID");
                objects = objects.set(id, value);
            }
            try {
                validate_document_structure(imported.layout, objects);
            } catch (const std::exception& error) {
                return CommandResult::fail(
                    std::string("Imported page validation failed: ") + error.what());
            } catch (...) {
                return CommandResult::fail("Imported page validation failed");
            }
            object_maps.push_back(std::move(objects));
        }

        std::vector<DocumentPage> prepared_pages;
        prepared_pages.reserve(pages_.size());
        try {
            for (size_t index = 0; index < pages_.size(); ++index) {
                const auto& imported = pages_[index];
                if (!imported.import_fidelity_report.format_name.empty() &&
                    !imported.import_fidelity_report.is_valid())
                    return CommandResult::fail("Imported page has invalid fidelity report metadata");
                auto page = make_document_page(
                    imported.name, imported.layout, std::move(object_maps[index]));
                if (!imported.import_fidelity_report.format_name.empty())
                    page.import_fidelity_reports.push_back(imported.import_fidelity_report);
                prepared_pages.push_back(std::move(page));
            }
        } catch (const std::exception& error) {
            return CommandResult::fail(
                std::string("Could not allocate imported page identities: ") + error.what());
        } catch (...) {
            return CommandResult::fail("Could not allocate imported page identities");
        }
        prepared_pages_ = std::move(prepared_pages);
        prepared_ = true;
    }

    std::vector<DocumentPage> next_pages;
    PageId next_active_page_id = doc.active_page_id();
    if (replace_document_pages_) {
        next_pages = prepared_pages_;
        next_active_page_id = prepared_pages_.front().id;
    } else {
        next_pages = current_pages;
        next_pages.insert(next_pages.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                          prepared_pages_.begin(), prepared_pages_.end());
    }

    previous_pages_ = current_pages;
    previous_active_page_id_ = doc.active_page_id();
    try {
        doc.apply_page_collection(std::move(next_pages), next_active_page_id);
    } catch (const std::exception& error) {
        return CommandResult::fail(std::string("Imported page set was rejected: ") + error.what());
    } catch (...) {
        return CommandResult::fail("Imported page set was rejected");
    }
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ImportPagesCommand::undo(Document& doc) {
    if (!executed_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    executed_ = false;
    return CommandResult::ok();
}

ImportContentCommand::ImportContentCommand(std::vector<Layer> layers,
                                           std::vector<ObjectVariant> objects,
                                           std::optional<std::pair<double, double>> source_page_size_mm,
                                           PageId target_page_id,
                                           ImportFidelityReport import_fidelity_report)
    : imported_layers_(std::move(layers)), imported_objects_(std::move(objects)),
      source_page_size_mm_(std::move(source_page_size_mm)), target_page_id_(target_page_id),
      import_fidelity_report_(std::move(import_fidelity_report)) {}

CommandResult ImportContentCommand::execute(Document& doc) {
    if (executed_ || imported_layers_.empty() || imported_objects_.empty())
        return CommandResult::fail("Import proposal is empty or already applied");
    if (!import_fidelity_report_.format_name.empty() &&
        !import_fidelity_report_.is_valid())
        return CommandResult::fail("Import fidelity report metadata is invalid");

    if (target_page_id_ == kInvalidPageId) target_page_id_ = doc.active_page_id();
    const PageId target_page_id = target_page_id_;
    auto next_pages = doc.pages();
    const auto target_page = std::find_if(next_pages.begin(), next_pages.end(),
        [&](const DocumentPage& page) { return page.id == target_page_id; });
    if (target_page == next_pages.end())
        return CommandResult::fail("Import target page no longer exists");

    Page next_page = target_page->layout;
    if (source_page_size_mm_) {
        const auto [width_mm, height_mm] = *source_page_size_mm_;
        if (!std::isfinite(width_mm) || !std::isfinite(height_mm) ||
            width_mm <= 0.0 || height_mm <= 0.0 ||
            width_mm > geometry::limits::kCanvasMaxMm ||
            height_mm > geometry::limits::kCanvasMaxMm)
            return CommandResult::fail("Import source page size is outside the supported physical range");
        next_page.width_mm = width_mm;
        next_page.height_mm = height_mm;
    }
    PersistentObjectMap next_objects = target_page->objects;
    std::unordered_set<ObjectId> imported_ids;
    std::unordered_map<ObjectId, const ObjectVariant*> imported_by_id;
    imported_ids.reserve(imported_objects_.size());
    imported_by_id.reserve(imported_objects_.size());
    std::unordered_set<ObjectId> document_object_ids;
    for (const auto& page : next_pages)
        for (const auto& [id, object] : page.objects.all()) {
            (void)object;
            document_object_ids.insert(id);
        }
    for (const auto& value : imported_objects_) {
        const ObjectId id = std::visit([](const auto& object) { return object.id; }, value);
        if (id == kInvalidObjectId || document_object_ids.contains(id) ||
            !imported_ids.insert(id).second)
            return CommandResult::fail("Import proposal contains an invalid or duplicate object ID");
        imported_by_id.emplace(id, &value);
        next_objects = next_objects.set(id, value);
    }

    std::unordered_set<ObjectId> layer_roots;
    layer_roots.reserve(imported_ids.size());
    std::vector<std::pair<ObjectId, size_t>> pending;
    for (const auto& layer : imported_layers_) {
        if (layer.name.empty()) return CommandResult::fail("Import proposal contains an unnamed layer");
        for (const ObjectId id : layer.object_ids) {
            if (!imported_ids.contains(id) || !layer_roots.insert(id).second)
                return CommandResult::fail("Import layer references are incomplete or invalid");
            pending.emplace_back(id, 0);
        }
        next_page.layers.push_back(layer);
    }
    std::unordered_set<ObjectId> reached;
    reached.reserve(imported_ids.size());
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        const auto found = imported_by_id.find(id);
        if (found == imported_by_id.end() || depth > 256 || !reached.insert(id).second)
            return CommandResult::fail("Import proposal hierarchy is invalid, cyclic or multiply parented");
        if (const auto* group = std::get_if<GroupObject>(found->second))
            for (const ObjectId child : group->children_ids) pending.emplace_back(child, depth + 1);
    }
    if (reached.size() != imported_ids.size())
        return CommandResult::fail("Import proposal contains an object outside its layer/group hierarchy");

    try {
        validate_document_structure(next_page, next_objects);
    } catch (const std::exception& error) {
        return CommandResult::fail(std::string("Import proposal validation failed: ") + error.what());
    } catch (...) {
        return CommandResult::fail("Import proposal validation failed");
    }

    target_page->layout = std::move(next_page);
    target_page->objects = std::move(next_objects);
    for (auto& artboard : target_page->artboards) {
        if (!artboard.linked_to_page_bounds) continue;
        artboard.x_mm = 0.0;
        artboard.y_mm = 0.0;
        artboard.width_mm = target_page->layout.width_mm;
        artboard.height_mm = target_page->layout.height_mm;
    }
    if (!import_fidelity_report_.format_name.empty())
        target_page->import_fidelity_reports.push_back(import_fidelity_report_);
    if (!has_before_) {
        before_pages_ = doc.pages();
        before_active_page_id_ = doc.active_page_id();
        has_before_ = true;
    }
    doc.apply_page_collection(std::move(next_pages), doc.active_page_id());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ImportContentCommand::undo(Document& doc) {
    if (!executed_ || !has_before_)
        return CommandResult::fail("Import command has no applied state to undo");
    doc.apply_page_collection(before_pages_, before_active_page_id_);
    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
