#include "snap_index_worker.hpp"

#include <exception>

namespace pikdraw::desktop::tools {

SnapIndexWorker::SnapIndexWorker() : worker_([this] { run(); }) {}

SnapIndexWorker::~SnapIndexWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        ++generation_;
        pending_ = {};
    }
    condition_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void SnapIndexWorker::request(document::SnapshotPtr snapshot) {
    if (!snapshot) return;
    {
        std::lock_guard lock(mutex_);
        ++generation_;
        pending_ = {generation_, std::move(snapshot)};
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
            building_ = true;
        }

        vector::precision::snap::SnapIndexBuildResult result;
        try {
            result = vector::precision::snap::SnapSourceIndex::build(
                *request.snapshot, {300000});
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
            building_ = false;
        }
        condition_.notify_one();
    }
}

} // namespace pikdraw::desktop::tools
