#include "pikdraw/document/document.hpp"
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
        doc.apply_mutation(after_->page(), after_->objects());
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        doc.apply_mutation(before_->page(), before_->objects());
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
    current_snapshot_ = std::make_shared<const DocumentSnapshot>(
        epoch_, revision_, current_page_, current_objects_, document_id_);
}

void Document::require_writer() const {
    if (std::this_thread::get_id() != writer_thread_)
        throw std::logic_error("Document state belongs to its writer thread; read a snapshot instead");
}

Document::Document(const DocumentSnapshot& saved) : Document(saved.document_id()) {
    epoch_ = allocate_epoch(saved.epoch());
    saved_epoch_ = epoch_;
    current_page_ = saved.page();
    current_objects_ = saved.objects();
    revision_ = saved.revision();
    saved_revision_ = revision_;
    if (!current_objects_.empty()) reserve_object_id((*current_objects_.all().rbegin()).first);
    current_snapshot_ = std::make_shared<const DocumentSnapshot>(
        epoch_, revision_, current_page_, current_objects_, document_id_);
}

void Document::require_idle_writer() const {
    require_writer();
    if (!open_) throw std::logic_error("Document is closed");
    if (busy_) throw std::logic_error("Reentrant document mutation is not permitted");
}

CommandResult Document::prepare_command(DocumentCommand& cmd, bool reverse, SnapshotPtr& next) {
    Page before_page = current_page_;
    const auto before_objects = current_objects_;
    auto restore = [&] {
        current_page_ = std::move(before_page);
        current_objects_ = before_objects;
    };
    try {
        BusyScope execution(executing_);
        if (revision_ == std::numeric_limits<uint64_t>::max())
            return CommandResult::fail("Document revision exhausted");
        auto result = reverse ? cmd.undo(*this) : cmd.execute(*this);
        if (!result.success) {
            restore();
            return result;
        }
        next = std::make_shared<const DocumentSnapshot>(
            epoch_, revision_ + 1, current_page_, current_objects_, document_id_);
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
    if (!current_objects_.empty()) reserve_object_id((*current_objects_.all().rbegin()).first);
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
        auto next_page = snapshot->page();
        auto next_objects = snapshot->objects();
        const auto next_revision = snapshot->revision();
        validate_page(next_page);
        const auto next_epoch = allocate_epoch(epoch_);
        auto next = std::make_shared<const DocumentSnapshot>(
            next_epoch, next_revision, next_page, next_objects, next_document_id);

        if (!next_objects.empty()) reserve_object_id((*next_objects.all().rbegin()).first);
        document_id_ = std::move(next_document_id);
        epoch_ = next_epoch;
        current_page_ = std::move(next_page);
        current_objects_ = std::move(next_objects);
        history_.clear();
        saved_epoch_ = next_epoch;
        saved_revision_ = next_revision;
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
    }
}

} // namespace pikdraw::document
