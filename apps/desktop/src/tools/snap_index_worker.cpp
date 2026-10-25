#include "snap_index_worker.hpp"

#include <exception>

namespace pikdraw::desktop::tools {

SnapIndexWorker::SnapIndexWorker() : worker_([this] { run(); }) {}

SnapIndexWorker::~SnapIndexWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        ++generation_;
        if (active_cancellation_) active_cancellation_->store(true, std::memory_order_release);
        if (pending_.cancellation) pending_.cancellation->store(true, std::memory_order_release);
        pending_ = {};
    }
    condition_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void SnapIndexWorker::request(document::SnapshotPtr snapshot) {
    if (!snapshot) return;
    {
        std::lock_guard lock(mutex_);
        if (active_cancellation_)
            active_cancellation_->store(true, std::memory_order_release);
        if (pending_.cancellation)
            pending_.cancellation->store(true, std::memory_order_release);
        ++generation_;
        pending_ = {generation_, std::move(snapshot), std::make_shared<std::atomic_bool>(false)};
    }
    condition_.notify_one();
}

vector::precision::snap::SnapIndexBuildResult SnapIndexWorker::result_for(
    const document::DocumentSnapshot& snapshot) const {
    std::lock_guard lock(mutex_);
    if (completed_document_id_ == snapshot.document_id() &&
        completed_epoch_ == snapshot.epoch() &&
        completed_revision_ == snapshot.revision())
        return completed_;
    return {};
}

bool SnapIndexWorker::building() const noexcept {
    std::lock_guard lock(mutex_);
    return building_ || static_cast<bool>(pending_.snapshot);
}

void SnapIndexWorker::run() {
    for (;;) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || pending_.snapshot; });
            if (stopping_) return;
            request = std::move(pending_);
            pending_ = {};
            active_cancellation_ = request.cancellation;
            building_ = true;
        }

        vector::precision::snap::SnapIndexBuildResult result;
        try {
            vector::precision::snap::SnapIndexBuildLimits limits;
            limits.max_entries = 300000;
            limits.should_cancel = [token = request.cancellation] {
                return !token || token->load(std::memory_order_acquire);
            };
            result = vector::precision::snap::SnapSourceIndex::build(
                *request.snapshot, std::move(limits));
        } catch (const std::exception& error) {
            result.diagnostic = error.what();
        } catch (...) {
            result.diagnostic = "Unexpected error while building the snap index";
        }
        {
            std::lock_guard lock(mutex_);
            if (!stopping_ && request.generation == generation_) {
                completed_document_id_ = request.snapshot->document_id();
                completed_epoch_ = request.snapshot->epoch();
                completed_revision_ = request.snapshot->revision();
                completed_ = std::move(result);
            }
            if (active_cancellation_ == request.cancellation)
                active_cancellation_.reset();
            building_ = false;
        }
        condition_.notify_one();
    }
}

} // namespace pikdraw::desktop::tools
