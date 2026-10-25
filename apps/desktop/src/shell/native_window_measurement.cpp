#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/production/measurement/measurement_import.hpp"
#include "pikdraw/production/measurement/measurement_report.hpp"
#include "pikdraw/production/measurement/measurement_run_store.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {
namespace {
#ifdef _WIN32
constexpr uint64_t kMaximumMeasurementBytes = 64ull * 1024ull * 1024ull;
constexpr int kToleranceMeanEdit = 6101;
constexpr int kToleranceP95Edit = 6102;
constexpr int kToleranceMaximumEdit = 6103;

struct ToleranceDialogState {
    bool vietnamese{false};
    bool accepted{false};
    std::optional<production::measurement::DeltaETolerance> tolerance;
};

std::optional<double> parse_limit(HWND dialog, int control) {
    wchar_t wide[64]{};
    const int length = GetDlgItemTextW(dialog, control, wide,
                                       static_cast<int>(std::size(wide)));
    if (length == 0) return std::nullopt;
    std::string ascii;
    ascii.reserve(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        if (wide[i] > 0x7f) return std::nullopt;
        ascii.push_back(static_cast<char>(wide[i]));
    }
    const auto comma = ascii.find(',');
    if (comma != std::string::npos) {
        if (ascii.find(',', comma + 1) != std::string::npos ||
            ascii.find('.') != std::string::npos)
            return std::nullopt;
        ascii[comma] = '.';
    }
    double value = 0.0;
    const auto result = std::from_chars(ascii.data(), ascii.data() + ascii.size(),
                                        value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != ascii.data() + ascii.size() ||
        !std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return value;
}

bool is_blank(HWND dialog, int control) {
    wchar_t text[64]{};
    GetDlgItemTextW(dialog, control, text, static_cast<int>(std::size(text)));
    for (const wchar_t ch : text)
        if (ch != L'\0' && !iswspace(ch)) return false;
    return true;
}

INT_PTR CALLBACK measurement_tolerance_dialog_proc(HWND dialog, UINT message,
                                                    WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<ToleranceDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<ToleranceDialogState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, state->vietnamese
            ? L"Ngưỡng ΔE00" : L"Delta E00 limits");
        const HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const wchar_t* help_text = state->vietnamese
            ? L"Để trống cả ba nếu chỉ cần số liệu. Dùng dấu chấm hoặc phẩy thập phân."
            : L"Leave all blank for metrics only. Use a dot or comma decimal separator.";
        HWND help = CreateWindowExW(0, L"STATIC", help_text,
            WS_CHILD | WS_VISIBLE, 12, 10, 400, 38, dialog, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        const wchar_t* labels[] = {state->vietnamese ? L"Trung bình" : L"Mean",
            L"P95", state->vietnamese ? L"Tối đa" : L"Maximum"};
        const int controls[] = {kToleranceMeanEdit, kToleranceP95Edit,
                                kToleranceMaximumEdit};
        for (int index = 0; index < 3; ++index) {
            const int y = 54 + index * 29;
            HWND label = CreateWindowExW(0, L"STATIC", labels[index],
                WS_CHILD | WS_VISIBLE, 14, y + 3, 90, 20, dialog, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                108, y, 290, 23, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(controls[index])),
                GetModuleHandleW(nullptr), nullptr);
            if (label) SendMessageW(label, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
            if (edit) SendMessageW(edit, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
        }
        HWND ok = CreateWindowExW(0, L"BUTTON",
            state->vietnamese ? L"So sánh" : L"Compare",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            273, 149, 63, 24, dialog, reinterpret_cast<HMENU>(IDOK),
            GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON",
            state->vietnamese ? L"Hủy" : L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            343, 149, 63, 24, dialog, reinterpret_cast<HMENU>(IDCANCEL),
            GetModuleHandleW(nullptr), nullptr);
        for (HWND control : {help, ok, cancel})
            if (control) SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(GetDlgItem(dialog, kToleranceMeanEdit));
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        const int controls[] = {kToleranceMeanEdit, kToleranceP95Edit,
                                kToleranceMaximumEdit};
        const bool all_blank = is_blank(dialog, controls[0]) &&
            is_blank(dialog, controls[1]) && is_blank(dialog, controls[2]);
        if (!all_blank) {
            const auto mean = parse_limit(dialog, controls[0]);
            const auto p95 = parse_limit(dialog, controls[1]);
            const auto maximum = parse_limit(dialog, controls[2]);
            if (!mean || !p95 || !maximum) {
                MessageBoxW(dialog, state->vietnamese
                    ? L"Nhập đủ ba ngưỡng số hữu hạn không âm hoặc để trống cả ba."
                    : L"Enter all three finite non-negative limits, or leave all three blank.",
                    state->vietnamese ? L"Ngưỡng không hợp lệ" : L"Invalid limits",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            state->tolerance = production::measurement::DeltaETolerance{
                *mean, *p95, *maximum};
        }
        state->accepted = true;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

std::optional<ToleranceDialogState> choose_measurement_tolerance(HWND owner,
                                                                  bool vietnamese) {
    struct EmptyDialog { DLGTEMPLATE dialog; WORD menu{0}, klass{0}, title{0}; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
        DS_CENTER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.cx = 430;
    layout.dialog.cy = 190;
    ToleranceDialogState state{vietnamese};
    const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
        &layout.dialog, owner, measurement_tolerance_dialog_proc,
        reinterpret_cast<LPARAM>(&state));
    if (result != IDOK || !state.accepted) return std::nullopt;
    return state;
}

std::wstring choose_measurement_input(HWND owner, bool vietnamese) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"CGATS / CSV measurement (*.txt;*.csv;*.cgats)\0*.txt;*.csv;*.cgats\0All files (*.*)\0*.*\0\0";
    dialog.lpstrTitle = vietnamese ? L"Nhập số đo CGATS / CSV" : L"Import CGATS / CSV measurements";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring{};
}

std::wstring choose_measurement_output(HWND owner, const std::filesystem::path& input,
                                      bool vietnamese) {
    std::vector<wchar_t> path(32768, L'\0');
    auto suggested = input;
    suggested.replace_extension(L".pdmr");
    const auto name = suggested.wstring();
    std::copy_n(name.c_str(), (std::min)(name.size(), path.size() - 1), path.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PikDraw measurement run (*.pdmr)\0*.pdmr\0\0";
    dialog.lpstrTitle = vietnamese ? L"Lưu Measurement Run" : L"Save Measurement Run";
    dialog.lpstrDefExt = L"pdmr";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    return GetSaveFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring{};
}

bool same_as_open_document(const std::filesystem::path& output,
                           const std::string& document_path) {
    if (document_path.empty()) return false;
    std::error_code left_error, right_error;
    const auto left = std::filesystem::absolute(output, left_error).lexically_normal().wstring();
    const auto right = std::filesystem::absolute(NativeWindow::utf8_to_wide(document_path),
                                                  right_error).lexically_normal().wstring();
    if (left_error || right_error) return true;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool same_path(const std::filesystem::path& first,
               const std::filesystem::path& second) {
    std::error_code left_error, right_error;
    const auto left = std::filesystem::absolute(first, left_error).lexically_normal().wstring();
    const auto right = std::filesystem::absolute(second, right_error).lexically_normal().wstring();
    return left_error || right_error || CompareStringOrdinal(left.c_str(),
        static_cast<int>(left.size()), right.c_str(), static_cast<int>(right.size()),
        TRUE) == CSTR_EQUAL;
}

std::wstring choose_run_input(HWND owner, const wchar_t* title) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PikDraw Measurement Run (*.pdmr)\0*.pdmr\0\0";
    dialog.lpstrTitle = title;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        OFN_HIDEREADONLY;
    return GetOpenFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring{};
}

std::wstring choose_comparison_output(HWND owner, const std::filesystem::path& input,
                                      bool vietnamese) {
    std::vector<wchar_t> path(32768, L'\0');
    auto suggested = input;
    suggested.replace_extension(L".comparison.csv");
    const auto name = suggested.wstring();
    std::copy_n(name.c_str(), (std::min)(name.size(), path.size() - 1), path.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"CSV report (*.csv)\0*.csv\0\0";
    dialog.lpstrTitle = vietnamese ? L"Lưu báo cáo so sánh" : L"Save comparison report";
    dialog.lpstrDefExt = L"csv";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR |
        OFN_HIDEREADONLY;
    return GetSaveFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring{};
}
#endif
}  // namespace

void NativeWindow::import_measurement_run() {
#ifdef _WIN32
    const bool vietnamese = language_ == UILanguage::Vietnamese;
    const auto input_name = choose_measurement_input(hwnd_, vietnamese);
    if (input_name.empty()) return;
    const std::filesystem::path input_path(input_name);
    std::error_code file_error;
    const auto size = std::filesystem::file_size(input_path, file_error);
    if (file_error || size == 0 || size > kMaximumMeasurementBytes) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Tệp số đo rỗng, không đọc được hoặc vượt giới hạn 64 MiB."
            : L"The measurement file is empty, unreadable, or exceeds the 64 MiB limit.",
            vietnamese ? L"Không thể nhập số đo" : L"Measurement import failed",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto output_name = choose_measurement_output(hwnd_, input_path, vietnamese);
    if (output_name.empty()) return;
    const std::filesystem::path output_path(output_name);
    auto extension = output_path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (extension != L".pdmr") {
        MessageBoxW(hwnd_, vietnamese
            ? L"Measurement Run phải dùng phần mở rộng .pdmr."
            : L"Measurement Runs must use the .pdmr extension.",
            vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (same_as_open_document(output_path, document_path_)) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Không thể ghi Measurement Run lên tài liệu .pr đang mở."
            : L"A Measurement Run cannot replace the open .pr document.",
            vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }

    std::string contents(static_cast<size_t>(size), '\0');
    std::ifstream file(input_path, std::ios::binary);
    if (!file || !file.read(contents.data(), static_cast<std::streamsize>(contents.size())) ||
        file.peek() != std::char_traits<char>::eof()) {
        MessageBoxW(hwnd_, vietnamese ? L"Không đọc đủ dữ liệu số đo." : L"Could not read the complete measurement file.",
            vietnamese ? L"Không thể nhập số đo" : L"Measurement import failed",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto parsed = production::measurement::parse_measurement_run(contents);
    if (!parsed) {
        const auto message = utf8_to_wide(parsed.diagnostic);
        MessageBoxW(hwnd_, message.c_str(),
            vietnamese ? L"Dữ liệu số đo không hợp lệ" : L"Invalid measurement data",
            MB_OK | MB_ICONWARNING);
        return;
    }
    std::string error;
    if (!production::measurement::save_measurement_run(output_path, *parsed.run, &error)) {
        const auto message = utf8_to_wide(error);
        MessageBoxW(hwnd_, message.c_str(),
            vietnamese ? L"Không lưu được Measurement Run" : L"Measurement Run save failed",
            MB_OK | MB_ICONERROR);
        return;
    }
    const auto& run = *parsed.run;
    std::wstring report = vietnamese
        ? L"Đã lưu Measurement Run bất biến.\n\n"
        : L"Immutable Measurement Run saved.\n\n";
    report += (vietnamese ? L"Số patch: " : L"Patches: ") + std::to_wstring(run.patches.size());
    report += L"\n" + std::wstring(vietnamese ? L"Tuyến in: " : L"Print route: ") +
        utf8_to_wide(run.condition.print_route);
    report += L"\n" + std::wstring(vietnamese ? L"Thiết bị: " : L"Instrument: ") +
        utf8_to_wide(run.condition.instrument);
    report += L"\n" + std::wstring(vietnamese ? L"Chế độ đo: " : L"Measurement mode: ") +
        utf8_to_wide(run.condition.measurement_mode);
    report += L"\n" + std::wstring(vietnamese ? L"Nguồn sáng / observer: " : L"Illuminant / observer: ") +
        utf8_to_wide(run.condition.illuminant + " / " + run.condition.observer);
    report += L"\n" + std::wstring(vietnamese ? L"Nền đo: " : L"Backing: ") +
        utf8_to_wide(run.condition.backing);
    report += L"\n" + std::wstring(vietnamese ? L"Ngày đo: " : L"Measured at: ") +
        utf8_to_wide(run.condition.measured_at);
    report += L"\n" + std::wstring(vietnamese ? L"Mã chart: " : L"Chart hash: ") +
        utf8_to_wide(run.condition.chart_hash.substr(0, 16));
    report += L"\n" + std::wstring(vietnamese ? L"Mã nguồn: " : L"Source hash: ") +
        utf8_to_wide(run.condition.source_sha256.substr(0, 16));
    report += L"\n" + std::wstring(vietnamese ? L"Mã profile đích: " : L"Output-profile hash: ") +
        utf8_to_wide(run.condition.output_profile_sha256.substr(0, 16));
    report += L"\n" + std::wstring(vietnamese ? L"Tệp run: " : L"Run file: ") +
        output_path.filename().wstring();
    report += vietnamese
        ? L"\n\nChưa đánh giá đạt/trượt: cần dữ liệu tham chiếu và ngưỡng ΔE phù hợp điều kiện in. Run này không phải chứng nhận contract proof."
        : L"\n\nNot assessed: a reference chart and Delta E limits for this print condition are required. This run is not a contract-proof certification.";
    MessageBoxW(hwnd_, report.c_str(), vietnamese ? L"Báo cáo số đo" : L"Measurement Report",
        MB_OK | MB_ICONINFORMATION);
    status_msg_ = vietnamese ? "Đã nhập và lưu Measurement Run · chưa đánh giá dung sai"
                             : "Measurement Run imported and saved · tolerance not assessed";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::compare_measurement_runs() {
#ifdef _WIN32
    using namespace production::measurement;
    const bool vietnamese = language_ == UILanguage::Vietnamese;
    const auto measured_name = choose_run_input(hwnd_,
        vietnamese ? L"Chọn Measurement Run cần đánh giá"
                   : L"Choose the measured Measurement Run");
    if (measured_name.empty()) return;
    const auto reference_name = choose_run_input(hwnd_,
        vietnamese ? L"Chọn Measurement Run tham chiếu"
                   : L"Choose the reference Measurement Run");
    if (reference_name.empty()) return;
    const std::filesystem::path measured_path(measured_name);
    const std::filesystem::path reference_path(reference_name);
    if (same_path(measured_path, reference_path)) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Hãy chọn hai Measurement Run khác nhau."
            : L"Choose two different Measurement Runs.",
            vietnamese ? L"Không thể so sánh" : L"Comparison unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }

    const auto measured = load_measurement_run(measured_path);
    if (!measured) {
        const auto message = utf8_to_wide(measured.diagnostic);
        MessageBoxW(hwnd_, message.c_str(),
            vietnamese ? L"Measurement Run không hợp lệ"
                       : L"Invalid Measurement Run",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto reference = load_measurement_run(reference_path);
    if (!reference) {
        const auto message = utf8_to_wide(reference.diagnostic);
        MessageBoxW(hwnd_, message.c_str(),
            vietnamese ? L"Measurement Run tham chiếu không hợp lệ"
                       : L"Invalid reference Measurement Run",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto selected_limits = choose_measurement_tolerance(hwnd_, vietnamese);
    if (!selected_limits) return;

    std::vector<ReferencePatch> reference_patches;
    reference_patches.reserve(reference.run->patches.size());
    for (const auto& patch : reference.run->patches)
        reference_patches.push_back({patch.sample_id, patch.lab});
    const auto comparison = compare_measurement_run(*measured.run,
        reference.run->condition, reference_patches, selected_limits->tolerance);
    const auto output_name = choose_comparison_output(hwnd_, measured_path, vietnamese);
    if (output_name.empty()) return;
    const std::filesystem::path output_path(output_name);
    auto extension = output_path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (extension != L".csv" || same_path(output_path, measured_path) ||
        same_path(output_path, reference_path) ||
        same_as_open_document(output_path, document_path_)) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Báo cáo phải là .csv và không được ghi đè run đầu vào hoặc tài liệu .pr đang mở."
            : L"The report must use .csv and cannot replace either input run or the open .pr document.",
            vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }
    std::string error;
    if (!save_measurement_comparison_csv(output_path, *measured.run,
            *reference.run, comparison, selected_limits->tolerance, &error)) {
        const auto message = utf8_to_wide(error);
        MessageBoxW(hwnd_, message.c_str(),
            vietnamese ? L"Không lưu được báo cáo"
                       : L"Could not save comparison report",
            MB_OK | MB_ICONERROR);
        return;
    }
    const wchar_t* result_label = comparison.complete
        ? (comparison.assessed ? (comparison.passed
            ? (vietnamese ? L"ĐẠT" : L"PASS")
            : (vietnamese ? L"KHÔNG ĐẠT" : L"FAIL"))
            : (vietnamese ? L"CHƯA ĐÁNH GIÁ" : L"NOT ASSESSED"))
        : (vietnamese ? L"CHƯA ĐỦ DỮ LIỆU" : L"INCOMPLETE");
    std::wstring summary = vietnamese
        ? L"Đã lưu báo cáo so sánh Measurement Run.\n\nKết quả: "
        : L"Measurement Run comparison report saved.\n\nResult: ";
    summary += result_label;
    summary += L"\n" + std::wstring(vietnamese ? L"Patch khớp: " : L"Matched patches: ") +
        std::to_wstring(comparison.matched);
    summary += L"\n" + std::wstring(vietnamese ? L"Thiếu / thừa: " : L"Missing / extra: ") +
        std::to_wstring(comparison.missing) + L" / " +
        std::to_wstring(comparison.extra);
    if (comparison.matched != 0) {
        summary += L"\nΔE00 mean / P95 / max: " +
            utf8_to_wide(std::to_string(comparison.mean_delta_e) + " / " +
                std::to_string(comparison.p95_delta_e) + " / " +
                std::to_string(comparison.maximum_delta_e));
    }
    summary += L"\n\n" + output_path.filename().wstring();
    summary += vietnamese
        ? L"\n\nĐây là so sánh run theo patch ID và điều kiện khai báo; không phải chứng nhận contract proof."
        : L"\n\nThis compares runs by patch ID and declared conditions; it is not contract-proof certification.";
    MessageBoxW(hwnd_, summary.c_str(),
        vietnamese ? L"Báo cáo ΔE00" : L"Delta E00 report",
        MB_OK | (comparison.assessed && !comparison.passed
            ? MB_ICONWARNING : MB_ICONINFORMATION));
    status_msg_ = comparison.assessed
        ? (comparison.passed ? "Measurement comparison passed supplied Delta E limits"
                             : "Measurement comparison exceeded supplied Delta E limits")
        : "Measurement comparison saved · pass/fail not assessed";
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
}  // namespace pikdraw::desktop
