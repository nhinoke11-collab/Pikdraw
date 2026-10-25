#include "pikdraw/desktop/shell/native_window.hpp"

#include "../performance/canvas_trace.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace pikdraw::desktop {

void NativeWindow::initialize_performance_trace() noexcept {
#ifdef _WIN32
    const DWORD required = GetEnvironmentVariableW(L"PIKDRAW_PERF_TRACE_PATH", nullptr, 0);
    if (required <= 1) return;
    std::wstring path(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"PIKDRAW_PERF_TRACE_PATH", path.data(), required);
    if (copied == 0 || copied >= required) return;
    path.resize(copied);

    try {
        canvas_trace_output_path_ = path;
        canvas_trace_ = std::make_unique<CanvasTrace>();
        canvas_trace_->set_enabled(true);
        auto* trace = canvas_trace_.get();
        auto* stop = &canvas_trace_stop_;
        auto* output_ready = &canvas_trace_output_ready_;
        const std::filesystem::path output_path(canvas_trace_output_path_);
        canvas_trace_worker_ = std::thread([trace, stop, output_ready, output_path] {
            std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
            if (!output) {
                trace->set_enabled(false);
                OutputDebugStringW(L"PikDraw performance trace could not open its configured output path.\n");
                return;
            }
            output_ready->store(true, std::memory_order_release);
            output << "#schema=1\n#counter_frequency=" << trace->counter_frequency()
                   << "\nsequence,event_id,frame_id,qpc_ticks,kind\n";
            const auto write_event = [&](const CanvasTraceEvent& event) {
                output << event.sequence << ',' << event.event_id << ',' << event.frame_id << ','
                       << event.qpc_ticks << ',' << static_cast<unsigned>(event.kind) << '\n';
            };
            while (!stop->load(std::memory_order_acquire)) {
                CanvasTraceEvent event{};
                bool drained = false;
                while (trace->try_pop(event)) {
                    write_event(event);
                    drained = true;
                }
                if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CanvasTraceEvent event{};
            while (trace->try_pop(event)) write_event(event);
            output << "#overflow_count=" << trace->overflow_count()
                   << "\n#clock_failure_count=" << trace->clock_failure_count() << '\n';
            output.flush();
        });
    } catch (...) {
        canvas_trace_.reset();
        canvas_trace_stop_.store(true, std::memory_order_release);
    }
#endif
}

void NativeWindow::shutdown_performance_trace() noexcept {
    if (!canvas_trace_) return;
    canvas_trace_->set_enabled(false);
    canvas_trace_stop_.store(true, std::memory_order_release);
    if (canvas_trace_worker_.joinable()) {
        try { canvas_trace_worker_.join(); } catch (...) {}
    }
#ifdef _WIN32
    if (canvas_trace_output_ready_.load(std::memory_order_acquire)) {
    try {
        SYSTEM_INFO system_info{};
        GetNativeSystemInfo(&system_info);
        MEMORYSTATUSEX memory_status{};
        memory_status.dwLength = sizeof(memory_status);
        const bool has_memory_status = GlobalMemoryStatusEx(&memory_status) != FALSE;
        const int display_width = GetSystemMetrics(SM_CXSCREEN);
        const int display_height = GetSystemMetrics(SM_CYSCREEN);
        int display_dpi = 0;
        HDC display_dc = GetDC(nullptr);
        if (display_dc) {
            display_dpi = GetDeviceCaps(display_dc, LOGPIXELSX);
            ReleaseDC(nullptr, display_dc);
        }
        const bool backend_hardware = active_tab_index_ < tabs_.size() &&
            tabs_[active_tab_index_].root &&
            tabs_[active_tab_index_].root->renderer().backend().is_hardware_accelerated();

        std::ofstream metadata(std::filesystem::path(canvas_trace_output_path_),
                               std::ios::binary | std::ios::app);
        if (metadata) {
#ifdef NDEBUG
            metadata << "#build=release\n";
#else
            metadata << "#build=debug\n";
#endif
            metadata << "#cpu_logical_processors=" << system_info.dwNumberOfProcessors << '\n'
                     << "#physical_memory_bytes="
                     << (has_memory_status ? memory_status.ullTotalPhys : 0) << '\n'
                     << "#primary_display_pixels=" << display_width << 'x' << display_height << '\n'
                     << "#primary_display_dpi=" << display_dpi << '\n'
                     << "#renderer_backend="
                     << (backend_hardware ? "direct2d_hardware" : "warp_or_unavailable") << '\n';
            metadata.flush();
        }
    } catch (...) {
        OutputDebugStringW(L"PikDraw performance trace metadata could not be appended.\n");
    }
    }
#endif
    canvas_trace_output_ready_.store(false, std::memory_order_release);
    canvas_trace_.reset();
    canvas_trace_output_path_.clear();
}

void NativeWindow::record_performance_event(CanvasTraceKind kind,
                                             uint64_t event_id,
                                             uint64_t frame_id) noexcept {
    if (canvas_trace_) (void)canvas_trace_->record(kind, event_id, frame_id);
}

} // namespace pikdraw::desktop
