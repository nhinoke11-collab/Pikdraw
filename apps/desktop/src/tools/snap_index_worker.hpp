#pragma once

#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace pikdraw::desktop::tools {

class SnapIndexWorker {
public:
    SnapIndexWorker();
    ~SnapIndexWorker();
    SnapIndexWorker(const SnapIndexWorker&) = delete;
    SnapIndexWorker& operator=(const SnapIndexWorker&) = delete;

    void request(document::SnapshotPtr snapshot);
    [[nodiscard]] vector::precision::snap::SnapIndexBuildResult result_for(
        const document::DocumentSnapshot& snapshot) const;
    [[nodiscard]] bool building() const noexcept;

private:
    struct Request {
        uint64_t generation{0};
        document::SnapshotPtr snapshot;
    };

    void run();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    Request pending_;
    uint64_t generation_{0};
    bool building_{false};
    bool stopping_{false};
    vector::precision::snap::SnapIndexBuildResult completed_;
    std::string completed_document_id_;
    uint64_t completed_epoch_{0};
    uint64_t completed_revision_{0};
};

} // namespace pikdraw::desktop::tools
