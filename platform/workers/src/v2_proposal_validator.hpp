#pragma once

#include "pikdraw/interop/ipc_dto.hpp"
#include "pikdraw/workers/win32_common.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>

namespace pikdraw::workers {

inline constexpr std::chrono::minutes kMaxV2ProposalStreamTimeout{5};

[[nodiscard]] bool NegotiateV2WorkerProcess(
    HANDLE stdin_pipe,
    HANDLE stdout_pipe,
    HANDLE process,
    uint32_t required_features,
    bool has_source_capability,
    std::chrono::milliseconds timeout,
    uint32_t& accepted_features,
    std::string_view& failure) noexcept;

[[nodiscard]] bool TransactV2ProposalStreamProcess(
    HANDLE stdin_pipe,
    HANDLE stdout_pipe,
    HANDLE process,
    const pikdraw::interop::V2ImportBegin& expected,
    std::span<const uint8_t> request_bytes,
    std::chrono::milliseconds timeout,
    const pikdraw::interop::V2ProposalStreamLimits& limits,
    const std::function<bool(const pikdraw::interop::IpcV2Frame&)>& consumer,
    std::string_view& failure,
    std::stop_token cancellation) noexcept;

class V2ProposalStreamValidator final {
public:
    V2ProposalStreamValidator(
        pikdraw::interop::V2ImportBegin expected,
        uint64_t max_canonical_bytes,
        uint64_t max_records,
        uint32_t max_chunks) noexcept;
    ~V2ProposalStreamValidator() noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool Accept(const pikdraw::interop::IpcV2Frame& frame) noexcept;
    [[nodiscard]] bool IsComplete() const noexcept { return complete_; }
    [[nodiscard]] std::string_view Failure() const noexcept { return failure_; }

private:
    struct DigestState;
    struct SemanticState;

    pikdraw::interop::V2ImportBegin expected_;
    uint64_t max_canonical_bytes_{0};
    uint64_t max_records_{0};
    uint32_t max_chunks_{0};
    uint64_t canonical_bytes_{0};
    uint64_t record_count_{0};
    uint32_t chunk_count_{0};
    bool ready_{false};
    bool complete_{false};
    std::string_view failure_{"none"};
    std::unique_ptr<DigestState> digest_;
    std::unique_ptr<SemanticState> semantic_;
};

} // namespace pikdraw::workers
