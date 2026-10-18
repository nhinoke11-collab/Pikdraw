#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace pikdraw::document {
namespace {
std::atomic<uint64_t> next_epoch{1};

uint64_t allocate_epoch(uint64_t greater_than = 0) {
    if (greater_than == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Document epoch exhausted");

    auto observed = next_epoch.load(std::memory_order_relaxed);
    for (;;) {
        if (observed == 0) throw std::overflow_error("Document epoch exhausted");
        const auto candidate = observed > greater_than ? observed : greater_than + 1;
        const auto successor = candidate == std::numeric_limits<uint64_t>::max() ? 0 : candidate + 1;
        if (next_epoch.compare_exchange_weak(observed, successor, std::memory_order_relaxed))
            return candidate;
    }
}

// History restores committed roots, never partially replays stateful user commands.
class CommittedState final : public DocumentCommand {
public:
    CommittedState(SnapshotPtr before, SnapshotPtr after)
        : before_(std::move(before)), after_(std::move(after)) {}
    void set_after(SnapshotPtr after) noexcept { after_ = std::move(after); }
    std::string_view name() const noexcept override { return "Committed transaction"; }
    CommandResult execute(Document& doc) override {
        doc.apply_page_collection(after_->pages(), after_->active_page_id());
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        doc.apply_page_collection(before_->pages(), before_->active_page_id());
        return CommandResult::ok();
    }
private:
    SnapshotPtr before_;
    SnapshotPtr after_;
};

struct BusyScope {
    bool& busy;
    explicit BusyScope(bool& flag) : busy(flag) { busy = true; }
    ~BusyScope() { busy = false; }
};
}

Document::Document(std::string document_id, Page page)
    : document_id_(std::move(document_id)), current_page_(std::move(page)), writer_(*this) {
    epoch_ = allocate_epoch();
    if (document_id_.empty()) throw std::invalid_argument("Document ID cannot be empty");
    saved_epoch_ = epoch_;
    validate_page(current_page_);
    pages_.push_back(make_document_page("Page 1", current_page_, current_objects_));
    active_page_id_ = pages_.front().id;
    current_snapshot_ = std::make_shared<const DocumentSnapshot>(
        epoch_, revision_, pages_, active_page_id_, document_id_);
}

void Document::require_writer() const {
    if (std::this_thread::get_id() != writer_thread_)
        throw std::logic_error("Document state belongs to its writer thread; read a snapshot instead");
}

Document::Document(const DocumentSnapshot& saved) : Document(saved.document_id()) {
    epoch_ = allocate_epoch(saved.epoch());
    saved_epoch_ = epoch_;
    pages_ = saved.pages();
    active_page_id_ = saved.active_page_id();
    current_page_ = saved.page();
    current_objects_ = saved.objects();
    revision_ = saved.revision();
    saved_revision_ = revision_;
    if (!current_objects_.empty()) reserve_object_id((*current_objects_.all().rbegin()).first);
    current_snapshot_ = std::make_shared<const DocumentSnapshot>(
        epoch_, revision_, pages_, active_page_id_, document_id_);
}

void Document::require_idle_writer() const {
    require_writer();
    if (!open_) throw std::logic_error("Document is closed");
    if (busy_) throw std::logic_error("Reentrant document mutation is not permitted");
}

CommandResult Document::prepare_command(DocumentCommand& cmd, bool reverse, SnapshotPtr& next) {
    Page before_page = current_page_;
    const auto before_objects = current_objects_;
    const auto before_pages = pages_;
    const auto before_active_page_id = active_page_id_;
    auto restore = [&] {
        current_page_ = std::move(before_page);
        current_objects_ = before_objects;
        pages_ = before_pages;
        active_page_id_ = before_active_page_id;
    };
    try {
        BusyScope execution(executing_);
        auto result = reverse ? cmd.undo(*this) : cmd.execute(*this);
        if (!result.success) {
            restore();
            return result;
        }
        if (result.is_no_change) {
            restore();
            return result;
        }
        if (revision_ == std::numeric_limits<uint64_t>::max()) {
            restore();
            return CommandResult::fail("Document revision exhausted");
        }
        next = std::make_shared<const DocumentSnapshot>(
            epoch_, revision_ + 1, pages_, active_page_id_, document_id_);
        return result;
    } catch (const std::exception& error) {
        restore();
        return CommandResult::fail(error.what());
    } catch (...) {
        restore();
        return CommandResult::fail("Unknown command exception");
    }
}

CommandResult Document::execute_command(CommandPtr cmd) {
    try { require_idle_writer(); }
    catch (const std::exception& error) { return CommandResult::fail(error.what()); }
    if (!cmd) return CommandResult::fail("Null document command");
    BusyScope scope(busy_);
    auto before = current_snapshot();
    std::unique_ptr<CommittedState> record;
    try { record = std::make_unique<CommittedState>(before, before); }
    catch (...) { return CommandResult::fail("Could not allocate history record"); }
    SnapshotPtr next;
    auto result = prepare_command(*cmd, false, next);
    if (!result.success) return result;
    if (result.is_no_change) return result;
    record->set_after(next);
    history_.push_undo(std::move(record));
    publish_snapshot(std::move(next));
    return result;
}

bool Document::move_history(bool reverse) {
    try { require_idle_writer(); }
    catch (...) { return false; }
    BusyScope scope(busy_);
    auto cmd = reverse ? history_.pop_undo() : history_.pop_redo();
    if (!cmd) return false;
    SnapshotPtr next;
    auto result = prepare_command(*cmd, reverse, next);
    if (result.success) {
        if (reverse) history_.push_redo(std::move(cmd));
        else history_.push_undo(std::move(cmd), false);
        publish_snapshot(std::move(next));
    } else {
        if (reverse) history_.push_undo(std::move(cmd), false);
        else history_.push_redo(std::move(cmd));
    }
    return result.success;
}

bool Document::undo() { return move_history(true); }
bool Document::redo() { return move_history(false); }

void Document::apply_mutation(Page page, PersistentObjectMap objects) {
    require_writer();
    if (!open_ || !executing_) throw std::logic_error("Mutation requires an executing document command");
    validate_page(page);
    current_page_ = std::move(page);
    current_objects_ = std::move(objects);
    const auto active = std::find_if(pages_.begin(), pages_.end(), [&](const auto& item) {
        return item.id == active_page_id_;
    });
    if (active == pages_.end()) throw std::logic_error("Active page root is missing");
    active->layout = current_page_;
    active->objects = current_objects_;
    if (!current_objects_.empty()) reserve_object_id((*current_objects_.all().rbegin()).first);
}

void Document::apply_page_collection(std::vector<DocumentPage> pages, PageId active_page_id) {
    require_writer();
    if (!open_ || !executing_)
        throw std::logic_error("Page collection mutation requires an executing document command");
    // Validate the entire replacement before publishing any part of it.
    DocumentSnapshot validation{epoch_, revision_, pages, active_page_id, document_id_};
    const auto* active = validation.find_page(active_page_id);
    if (!active) throw std::invalid_argument("Active page ID is not present in the document");
    pages_ = std::move(pages);
    active_page_id_ = active_page_id;
    current_page_ = active->layout;
    current_objects_ = active->objects;
    for (const auto& page : pages_)
        if (!page.objects.empty()) reserve_object_id((*page.objects.all().rbegin()).first);
}

bool Document::set_active_page(PageId page_id) {
    try { require_idle_writer(); }
    catch (...) { return false; }
    if (page_id == active_page_id_) return true;
    const auto target = std::find_if(pages_.begin(), pages_.end(), [&](const auto& page) {
        return page.id == page_id;
    });
    if (target == pages_.end()) return false;
    try {
        auto next = std::make_shared<const DocumentSnapshot>(
            epoch_, revision_, pages_, page_id, document_id_);
        active_page_id_ = page_id;
        current_page_ = target->layout;
        current_objects_ = target->objects;
        publish_snapshot(std::move(next));
        return true;
    } catch (...) {
        return false;
    }
}

bool Document::reset_from_snapshot(SnapshotPtr snapshot) {
    try { require_idle_writer(); }
    catch (...) { return false; }
    if (!snapshot) return false;

    BusyScope scope(busy_);
    try {
        // Prepare the replacement completely before changing the live document.
        auto next_document_id = snapshot->document_id().empty()
            ? document_id_ : snapshot->document_id();
        auto next_pages = snapshot->pages();
        auto next_active_page_id = snapshot->active_page_id();
        const auto next_revision = snapshot->revision();
        const auto next_epoch = allocate_epoch(epoch_);
        auto next = std::make_shared<const DocumentSnapshot>(
            next_epoch, next_revision, next_pages, next_active_page_id, next_document_id);

        document_id_ = std::move(next_document_id);
        epoch_ = next_epoch;
        pages_ = std::move(next_pages);
        active_page_id_ = next_active_page_id;
        current_page_ = next->page();
        current_objects_ = next->objects();
        history_.clear();
        saved_epoch_ = next_epoch;
        saved_revision_ = next_revision;
        recovery_pending_save_ = false;
        publish_snapshot(std::move(next));
        return true;
    } catch (...) {
        return false;
    }
}

void Document::publish_snapshot(SnapshotPtr next) noexcept {
    revision_ = next->revision();
    std::atomic_store_explicit(&current_snapshot_, next, std::memory_order_release);
    // Edits invalidate every outstanding proposal under strict-base semantics.
    jobs_.clear();
    for (const auto& listener : listeners_) {
        // An observer failure must not turn an already committed edit into a failed command.
        try { if (listener) listener(next); } catch (...) { ++listener_failures_; }
    }
}

void Document::add_snapshot_listener(SnapshotListener listener) {
    require_idle_writer();
    if (listener) listeners_.push_back(std::move(listener));
}

void Document::mark_saved(uint64_t epoch, uint64_t revision) {
    require_idle_writer();
    // Late or reordered save completions must never clear newer edits.
    if (epoch == epoch_ && revision == revision_) {
        saved_epoch_ = epoch;
        saved_revision_ = revision;
        recovery_pending_save_ = false;
    }
}

void Document::mark_recovered_unsaved() {
    require_idle_writer();
    if (!open_)
        throw std::logic_error("Cannot mark a closed document as recovered");
    recovery_pending_save_ = true;
}

} // namespace pikdraw::document
