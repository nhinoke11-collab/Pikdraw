#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/cut_shape_tool_catalog.hpp"

#ifdef _WIN32

#include <algorithm>
#include <exception>
#include <utility>

namespace pikdraw::desktop {

bool NativeWindow::start_cut_geometry_job(CutGeometryRequest request) {
    if (!hwnd_ || !request.snapshot) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể bắt đầu xử lý hình học · tài liệu không đổi"
            : "Could not start geometry processing · document unchanged";
        return false;
    }
    if (cut_geometry_job_) {
        bool finished = false;
        {
            std::lock_guard lock(cut_geometry_job_->result_mutex);
            finished = cut_geometry_job_->proposal.has_value();
        }
        if (finished) handle_cut_geometry_worker_message(cut_geometry_job_->generation);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Một thao tác Cut & Shape vừa kết thúc hoặc đang xử lý · thử lại thao tác mới"
            : "A Cut & Shape operation just finished or is still processing · retry this new action";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    if (cut_geometry_worker_.joinable())
        cut_geometry_worker_.join();
    if (pending_cut_geometry_proposal_)
        discard_pending_cut_geometry_proposal();

    if (SetTimer(hwnd_, static_cast<UINT_PTR>(kCutGeometryPollTimerId), 50, nullptr) == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể theo dõi kết quả hình học · tài liệu không đổi"
            : "Could not monitor geometry completion · document unchanged";
        return false;
    }

    auto state = std::make_shared<CutGeometryJobState>();
    state->generation = ++cut_geometry_generation_;
    state->tab_id = active_tab().id;
    state->base_snapshot = request.snapshot;
    state->selected_ids = request.selected_ids;
    state->operation = request.operation;
    state->target_id = request.target_id;
    state->source_ids = request.source_ids;
    state->points_world = request.points_world;
    state->mode_flag = request.operation == CutGeometryOperation::TrimObjects
        ? request.keep_sources : request.erase_faces;
    state->menu_action = request.menu_action;
    state->preview_only = request.preview_only;
    state->initiating_tool = active_tool_;
    cut_geometry_job_ = state;
    const HWND notify_window = hwnd_;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang tính hình học Cut & Shape · Esc để hủy"
        : "Computing Cut & Shape geometry · Esc to cancel";
    InvalidateRect(hwnd_, nullptr, FALSE);

    try {
        cut_geometry_worker_ = std::jthread(
            [state, request = std::move(request), notify_window](std::stop_token stop) mutable {
            const auto post_message = [state, notify_window](
                    UINT message, WPARAM wparam, LPARAM lparam) noexcept {
                if (!notify_window) return;
                try {
                    state->dispatch_notification_if_enabled([&] {
                        (void)PostMessageW(notify_window, message, wparam, lparam);
                    });
                } catch (...) {
                    // The UI timer polls the published proposal if notification fails.
                }
            };
            CutGeometryProposal proposal;
            try {
                proposal = compute_cut_geometry(request, stop,
                    [state, post_message](std::size_t completed, std::size_t total) {
                        state->total.store(total, std::memory_order_release);
                        state->completed.store(completed, std::memory_order_release);
                        if (completed % 8 == 0 || completed == total)
                            post_message(kCutGeometryProgressMessage,
                                static_cast<WPARAM>(state->generation), static_cast<LPARAM>(completed));
                    });
            } catch (const std::exception& exception) {
                proposal.status = stop.stop_requested()
                    ? CutGeometryStatus::Cancelled : CutGeometryStatus::Invalid;
                proposal.operation = request.operation;
                proposal.base_snapshot = state->base_snapshot;
                proposal.message = exception.what();
            } catch (...) {
                proposal.status = stop.stop_requested()
                    ? CutGeometryStatus::Cancelled : CutGeometryStatus::Invalid;
                proposal.operation = request.operation;
                proposal.base_snapshot = state->base_snapshot;
                proposal.message = "Unknown exception during cut geometry computation";
            }
            {
                std::lock_guard lock(state->result_mutex);
                state->proposal.emplace(std::move(proposal));
            }
            post_message(kCutGeometryWorkerMessage,
                static_cast<WPARAM>(state->generation), 0);
            });
    } catch (...) {
        if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kCutGeometryPollTimerId));
        cut_geometry_job_.reset();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể khởi chạy worker hình học · tài liệu không đổi"
            : "Could not start the geometry worker · document unchanged";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    return true;
}

void NativeWindow::cancel_cut_geometry_job() noexcept {
    smart_trim_click_pending_request_.reset();
    smart_trim_hover_world_.reset();
#ifdef _WIN32
    if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId));
#endif
    discard_pending_cut_geometry_proposal();
    if (!cut_geometry_job_)
        return;
    ++cut_geometry_generation_;
    if (cut_geometry_worker_.joinable())
        cut_geometry_worker_.request_stop();
}

void NativeWindow::discard_pending_cut_geometry_proposal() noexcept {
    if (!pending_cut_geometry_proposal_)
        return;
    pending_cut_geometry_proposal_.reset();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::apply_pending_cut_geometry_proposal(
    CutGeometryOperation operation, bool erase_faces) {
    if (!cut_geometry_preview_ready(operation))
        return false;
    auto pending = std::move(*pending_cut_geometry_proposal_);
    pending_cut_geometry_proposal_.reset();
    const auto& state = pending.state;
    bool operation_context_matches = false;
    int expected_tool = 0;
    if (operation == CutGeometryOperation::TrimObjects) {
        expected_tool = static_cast<int>(CutShapeToolId::TrimObjects);
        operation_context_matches = state && !state->menu_action &&
            state->target_id == trim_objects_target_id_ &&
            state->source_ids == trim_objects_source_ids_ &&
            state->mode_flag == trim_objects_keep_sources_ &&
            pending.proposal.keep_sources == trim_objects_keep_sources_;
    } else if (operation == CutGeometryOperation::ShapeBuilder) {
        expected_tool = static_cast<int>(CutShapeToolId::ShapeBuilder);
        operation_context_matches = state &&
            state->points_world == shape_builder_seed_points_ &&
            state->mode_flag == erase_faces && pending.proposal.erase_faces == erase_faces;
    } else if (operation == CutGeometryOperation::SmartTrim) {
        expected_tool = static_cast<int>(CutShapeToolId::SmartTrim);
        operation_context_matches = state && state->preview_only &&
            state->points_world.size() == 1 && smart_trim_hover_world_ &&
            state->points_world.front().distance_to(*smart_trim_hover_world_) <= 1e-6;
    }
    const bool current_context = state && operation_context_matches &&
        state->operation == operation && active_tool_ == expected_tool &&
        state->generation == cut_geometry_generation_ &&
        state->tab_id == active_tab().id &&
        state->base_snapshot == root().document().current_snapshot() &&
        state->selected_ids == root().vector_controller().selection().selected_ids();
    if (!current_context) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản xem trước đã cũ hoặc ngữ cảnh đã đổi · tài liệu chưa thay đổi"
            : "Preview is stale or its context changed · document unchanged";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    const double approximation_error_bound_mm =
        pending.proposal.approximation_error_bound_mm;
    std::string error;
    const bool applied = workflow().apply_cut_geometry_proposal(
        std::move(pending.proposal), &error);
    if (!applied) {
        status_msg_ = (language_ == UILanguage::Vietnamese
            ? "Không áp dụng được bản xem trước: " : "Preview was not applied: ") +
            (error.empty() ? "document unchanged" : error);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (operation == CutGeometryOperation::TrimObjects) {
        trim_objects_target_id_ = document::kInvalidObjectId;
        trim_objects_source_ids_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã áp dụng Trim Objects · một bước Undo"
            : "Trim Objects applied · one Undo step";
    } else if (operation == CutGeometryOperation::SmartTrim) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã Smart Trim đoạn đang xem trước · một bước Undo"
            : "Smart Trim applied to the previewed interval · one Undo step";
    } else {
        shape_builder_seed_points_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (erase_faces
                ? "Đã xóa mặt Shape Builder · một bước Undo"
                : "Đã hợp nhất mặt Shape Builder · một bước Undo")
            : (erase_faces
                ? "Shape Builder erased the selected faces · one Undo step"
                : "Shape Builder merged the selected faces · one Undo step");
    }
    status_msg_ += language_ == UILanguage::Vietnamese
        ? " · sai số xấp xỉ ≤ " : " · approximation bound ≤ ";
    status_msg_ += std::to_string(approximation_error_bound_mm) + " mm";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::handle_cut_geometry_progress_message(std::uint64_t generation,
                                                          std::size_t completed) {
    const auto state = cut_geometry_job_;
    if (!state || state->generation != generation ||
        generation != cut_geometry_generation_ || state->tab_id != active_tab().id)
        return;
    const auto total = state->total.load(std::memory_order_acquire);
    if (total == 0) return;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang xử lý hình học Cut & Shape " + std::to_string(completed) + "/" +
              std::to_string(total) + " · Esc để hủy"
        : "Processing Cut & Shape geometry " + std::to_string(completed) + "/" +
              std::to_string(total) + " · Esc to cancel";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_cut_geometry_worker_message(std::uint64_t generation) {
    const auto state = cut_geometry_job_;
    if (!state || state->generation != generation)
        return;
    std::optional<CutGeometryProposal> proposal;
    {
        std::lock_guard lock(state->result_mutex);
        if (state->proposal) proposal.emplace(std::move(*state->proposal));
        state->proposal.reset();
    }
    if (!proposal) return;
    const double approximation_error_bound_mm =
        proposal->approximation_error_bound_mm;
    const auto proposal_operation = proposal->operation;
    const bool proposal_keep_sources = proposal->keep_sources;
    const bool proposal_erases_faces = proposal->erase_faces;
    if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kCutGeometryPollTimerId));
    if (cut_geometry_worker_.joinable())
        cut_geometry_worker_.join();

    const int expected_tool = proposal_operation == CutGeometryOperation::Knife
        ? static_cast<int>(CutShapeToolId::Knife)
        : proposal_operation == CutGeometryOperation::VectorEraser
            ? static_cast<int>(CutShapeToolId::VectorEraser)
            : proposal_operation == CutGeometryOperation::Scissors
                ? static_cast<int>(CutShapeToolId::Scissors)
                : proposal_operation == CutGeometryOperation::SmartTrim
                    ? static_cast<int>(CutShapeToolId::SmartTrim)
                    : proposal_operation == CutGeometryOperation::TrimObjects
                        ? static_cast<int>(CutShapeToolId::TrimObjects)
                        : proposal_operation == CutGeometryOperation::SmartFill
                            ? static_cast<int>(CutShapeToolId::SmartFill)
                            : static_cast<int>(CutShapeToolId::ShapeBuilder);
    bool operation_context_matches = true;
    if (proposal_operation == CutGeometryOperation::TrimObjects) {
        operation_context_matches = state->menu_action
            ? active_tool_ == state->initiating_tool
            : state->target_id == trim_objects_target_id_ &&
                state->source_ids == trim_objects_source_ids_ &&
                state->mode_flag == trim_objects_keep_sources_ &&
                proposal_keep_sources == trim_objects_keep_sources_;
    } else if (proposal_operation == CutGeometryOperation::ShapeBuilder) {
        operation_context_matches = state->points_world == shape_builder_seed_points_;
    } else if (proposal_operation == CutGeometryOperation::SmartTrim &&
               state->preview_only) {
        operation_context_matches = smart_trim_hover_world_ &&
            state->points_world.size() == 1 &&
            state->points_world.front().distance_to(*smart_trim_hover_world_) <= 1e-6;
    }
    const bool tool_context_matches = state->menu_action
        ? active_tool_ == state->initiating_tool : active_tool_ == expected_tool;
    const bool current_context = operation_context_matches && tool_context_matches &&
        generation == cut_geometry_generation_ &&
        state->tab_id == active_tab().id &&
        state->base_snapshot == root().document().current_snapshot() &&
        state->selected_ids == root().vector_controller().selection().selected_ids();
    const bool requires_preview = current_context &&
        proposal->status == CutGeometryStatus::Success &&
        ((proposal_operation == CutGeometryOperation::TrimObjects && !state->menu_action) ||
         proposal_operation == CutGeometryOperation::ShapeBuilder ||
         (proposal_operation == CutGeometryOperation::SmartTrim && state->preview_only));
    if (requires_preview) {
        cut_geometry_job_.reset();
        if (proposal->preview_contours.empty()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không tạo được đường viền xem trước · tài liệu chưa đổi"
                : "Could not create a geometry preview · document unchanged";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        pending_cut_geometry_proposal_.emplace(PendingCutGeometryProposal{
            std::move(*proposal), state});
        if (proposal_operation == CutGeometryOperation::SmartTrim) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đoạn Smart Trim đã được tô đỏ · nhấp để cắt · Esc để bỏ"
                : "Smart Trim interval highlighted in red · click to trim · Esc to dismiss";
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bản xem trước hình học đã sẵn sàng · Apply để ghi · Esc để bỏ"
                : "Geometry preview ready · Apply to commit · Esc to discard";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    std::string error;
    bool applied = false;
    if (current_context && proposal->status == CutGeometryStatus::Success)
        applied = workflow().apply_cut_geometry_proposal(std::move(*proposal), &error);
    else if (current_context)
        error = proposal->message.empty() ? "No geometry was changed" : proposal->message;
    else if (generation == cut_geometry_generation_)
        error = "Document, tab, page, or active tool changed; stale result was discarded";

    cut_geometry_job_.reset();
    if (current_context) {
        if (applied) {
            std::string completion;
            if (proposal_operation == CutGeometryOperation::Knife)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã cắt path · một bước Undo · Ctrl+Z để hoàn tác"
                    : "Knife cut completed · one Undo step";
            else if (proposal_operation == CutGeometryOperation::VectorEraser)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã tẩy vector · một bước Undo · Ctrl+Z để hoàn tác"
                    : "Vector Eraser completed · one Undo step";
            else if (proposal_operation == CutGeometryOperation::Scissors)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã cắt path bằng Scissors · một bước Undo"
                    : "Scissors split completed · one Undo step";
            else if (proposal_operation == CutGeometryOperation::SmartTrim)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã Smart Trim đoạn giao điểm · một bước Undo"
                    : "Smart Trim removed the bounded interval · one Undo step";
            else if (proposal_operation == CutGeometryOperation::TrimObjects)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã áp dụng Trim Objects · một bước Undo"
                    : "Trim Objects applied · one Undo step";
            else if (proposal_operation == CutGeometryOperation::SmartFill)
                completion = language_ == UILanguage::Vietnamese
                    ? "Đã tạo path Smart Fill chỉnh sửa được · nguồn được giữ · một bước Undo"
                    : "Created an editable Smart Fill path · sources preserved · one Undo step";
            else
                completion = language_ == UILanguage::Vietnamese
                    ? (proposal_erases_faces
                        ? "Đã xóa mặt Shape Builder trong một giao dịch · một bước Undo"
                        : "Đã hợp nhất mặt Shape Builder thành path · một bước Undo")
                    : (proposal_erases_faces
                        ? "Shape Builder erased the selected faces · one Undo step"
                        : "Shape Builder merged the selected faces · one Undo step");
            status_msg_ = std::move(completion);
            if (proposal_operation == CutGeometryOperation::Knife ||
                proposal_operation == CutGeometryOperation::VectorEraser ||
                proposal_operation == CutGeometryOperation::TrimObjects ||
                proposal_operation == CutGeometryOperation::SmartFill ||
                proposal_operation == CutGeometryOperation::ShapeBuilder) {
                status_msg_ += language_ == UILanguage::Vietnamese
                    ? " · sai số xấp xỉ ≤ " : " · approximation bound ≤ ";
                status_msg_ += std::to_string(approximation_error_bound_mm) + " mm";
            }
            if (proposal_operation == CutGeometryOperation::TrimObjects) {
                if (!state->menu_action) {
                    trim_objects_target_id_ = document::kInvalidObjectId;
                    trim_objects_source_ids_.clear();
                }
            } else if (proposal_operation == CutGeometryOperation::ShapeBuilder) {
                shape_builder_seed_points_.clear();
            }
        } else {
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Không áp dụng Cut & Shape: " : "Cut & Shape was not applied: ") +
                (error.empty() ? "no geometry was changed" : error);
        }
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    if (smart_trim_click_pending_request_)
        process_smart_trim_hover_timer();
}

void NativeWindow::poll_cut_geometry_job() {
    const auto state = cut_geometry_job_;
    if (!state) {
        if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kCutGeometryPollTimerId));
        return;
    }
    bool finished = false;
    {
        std::lock_guard lock(state->result_mutex);
        finished = state->proposal.has_value();
    }
    if (finished) handle_cut_geometry_worker_message(state->generation);
}

void NativeWindow::update_smart_trim_hover_preview(
    const geometry::Point2D& point, bool inside_canvas) {
    if (active_tool_ != static_cast<int>(CutShapeToolId::SmartTrim)) {
        smart_trim_hover_world_.reset();
        smart_trim_click_pending_request_.reset();
        if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId));
        if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim))
            discard_pending_cut_geometry_proposal();
        if (cut_geometry_job_ && cut_geometry_job_->operation == CutGeometryOperation::SmartTrim &&
            cut_geometry_job_->preview_only) {
            ++cut_geometry_generation_;
            if (cut_geometry_worker_.joinable()) cut_geometry_worker_.request_stop();
        }
        return;
    }
    if (!inside_canvas) {
        smart_trim_hover_world_.reset();
        if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId));
        if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim))
            discard_pending_cut_geometry_proposal();
        if (cut_geometry_job_ && cut_geometry_job_->operation == CutGeometryOperation::SmartTrim &&
            cut_geometry_job_->preview_only) {
            ++cut_geometry_generation_;
            if (cut_geometry_worker_.joinable()) cut_geometry_worker_.request_stop();
        }
        return;
    }
    if (!point.is_finite()) return;
    if (smart_trim_click_pending_request_) return;
    const double zoom = std::max(0.01, root().renderer().camera().zoom());
    const double movement_threshold_mm = 1.0 / zoom;
    if (smart_trim_hover_world_ &&
        point.distance_to(*smart_trim_hover_world_) <= movement_threshold_mm)
        return;
    smart_trim_hover_world_ = point;
    if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim))
        discard_pending_cut_geometry_proposal();
    if (cut_geometry_job_ && cut_geometry_job_->operation == CutGeometryOperation::SmartTrim &&
        cut_geometry_job_->preview_only) {
        ++cut_geometry_generation_;
        if (cut_geometry_worker_.joinable()) cut_geometry_worker_.request_stop();
    }
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Smart Trim · đang tìm đoạn giao điểm dưới con trỏ"
        : "Smart Trim · resolving the interval under the pointer";
    if (hwnd_ && SetTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId), 75, nullptr) == 0)
        process_smart_trim_hover_timer();
}

void NativeWindow::process_smart_trim_hover_timer() {
    if (hwnd_) KillTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId));
    if (active_tool_ != static_cast<int>(CutShapeToolId::SmartTrim)) {
        smart_trim_hover_world_.reset();
        smart_trim_click_pending_request_.reset();
        return;
    }
    if (cut_geometry_job_) {
        if (hwnd_) SetTimer(hwnd_, static_cast<UINT_PTR>(kSmartTrimHoverTimerId), 50, nullptr);
        return;
    }
    if (cut_geometry_preview_ready(CutGeometryOperation::SmartTrim)) return;
    CutGeometryRequest request;
    const bool deferred_click = smart_trim_click_pending_request_.has_value();
    if (deferred_click) {
        request = *smart_trim_click_pending_request_;
    } else {
        if (pointer_active_ || !smart_trim_hover_world_) return;
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return;
        request.operation = CutGeometryOperation::SmartTrim;
        request.snapshot = snapshot;
        request.selected_ids = root().vector_controller().selection().selected_ids();
        request.points_world = {*smart_trim_hover_world_};
        request.hit_tolerance_mm = 6.0 /
            std::max(0.01, root().renderer().camera().zoom());
        request.preview_only = true;
    }
    (void)start_cut_geometry_job(std::move(request));
    if (deferred_click) smart_trim_click_pending_request_.reset();
}

} // namespace pikdraw::desktop

#endif
