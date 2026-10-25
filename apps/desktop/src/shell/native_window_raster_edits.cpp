#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/raster/edit_preview_service.hpp"
#include "pikdraw/raster/edit_geometry.hpp"

#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/document/commands/raster_edit_commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {
namespace {

#ifdef _WIN32
constexpr int kCropXId = 301;
constexpr int kCropYId = 302;
constexpr int kCropWidthId = 303;
constexpr int kCropHeightId = 304;
constexpr int kExposureId = 305;
constexpr int kEditHelpId = 306;
constexpr int kLevelsChannelId = 307;
constexpr int kLevelsBlackInId = 308;
constexpr int kLevelsWhiteInId = 309;
constexpr int kLevelsGammaId = 310;
constexpr int kLevelsBlackOutId = 311;
constexpr int kLevelsWhiteOutId = 312;
constexpr int kHslHueId = 313;
constexpr int kHslSaturationId = 314;
constexpr int kHslLightnessId = 315;
constexpr int kCurvesChannelId = 316;
constexpr int kCurvesPoint0Id = 317;
constexpr int kWhiteBalanceTemperatureId = 322;
constexpr int kWhiteBalanceTintId = 323;
constexpr int kVibranceAmountId = 324;
constexpr int kSelectiveHslRangeId = 325;
constexpr int kSelectiveHslHueId = 326;
constexpr int kSelectiveHslSaturationId = 327;
constexpr int kSelectiveHslLightnessId = 328;
constexpr int kSelectiveHslFeatherId = 329;
constexpr int kLightExposureId = 330;
constexpr int kLightContrastId = 331;
constexpr int kLightHighlightsId = 332;
constexpr int kLightShadowsId = 333;
constexpr int kLightWhitesId = 334;
constexpr int kLightBlacksId = 335;
constexpr int kChannelMixerCoefficient0Id = 336;
constexpr int kCloneSourceXId = 345;
constexpr int kCloneSourceYId = 346;
constexpr int kCloneTargetXId = 347;
constexpr int kCloneTargetYId = 348;
constexpr int kCloneRadiusId = 349;
constexpr int kCloneHardnessId = 350;
constexpr int kCloneOpacityId = 351;
constexpr int kColorBalanceRangeId = 352;
constexpr int kColorBalanceRedCyanId = 353;
constexpr int kColorBalanceGreenMagentaId = 354;
constexpr int kColorBalanceBlueYellowId = 355;
constexpr int kColorBalancePreserveLuminosityId = 356;

struct RasterEditDialogState {
    bool vietnamese{false};
    bool crop{false};
    bool levels{false};
    bool hsl{false};
    bool curves{false};
    bool white_balance{false};
    bool vibrance{false};
    bool selective_hsl{false};
    bool light{false};
    bool color_balance{false};
    bool channel_mixer{false};
    bool clone_stamp{false};
    bool accepted{false};
    double x{0.0};
    double y{0.0};
    double width{1.0};
    double height{1.0};
    double exposure_ev{0.0};
    document::RasterLevelsParams levels_params{};
    document::RasterHslParams hsl_params{};
    document::RasterCurvesParams curves_params{};
    document::RasterWhiteBalanceParams white_balance_params{};
    document::RasterVibranceParams vibrance_params{};
    document::RasterSelectiveHslParams selective_hsl_params{};
    document::RasterLightParams light_params{};
    document::RasterColorBalanceParams color_balance_params{};
    document::RasterChannelMixerParams channel_mixer_params{};
    document::RasterCloneStampParams clone_stamp_params{};
    uint32_t clone_width_px{0};
    uint32_t clone_height_px{0};
    double clone_source_x_px{0.0};
    double clone_source_y_px{0.0};
    double clone_target_x_px{0.0};
    double clone_target_y_px{0.0};
    double clone_radius_px{1.0};
    std::array<document::RasterSelectiveHslParams, 6> selective_hsl_by_range{};
    int selective_hsl_active_range{0};
    std::array<document::RasterColorBalanceParams, 3> color_balance_by_range{};
    std::array<bool, 3> color_balance_enabled_by_range{true, true, true};
    int color_balance_active_range{1};
};

HWND add_control(HWND dialog, const wchar_t* type, const std::wstring& text,
                 DWORD style, int x, int y, int width, int height, int id,
                 HFONT font) {
    const HWND control = CreateWindowExW(0, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control && font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

void set_number(HWND dialog, int id, double value) {
    const auto text = std::to_wstring(value);
    SetDlgItemTextW(dialog, id, text.c_str());
}

bool read_number(HWND dialog, int id, double& value) {
    wchar_t text[128]{};
    const int length = GetDlgItemTextW(dialog, id, text,
                                       static_cast<int>(std::size(text)));
    if (length <= 0) return false;
    wchar_t* end = nullptr;
    value = std::wcstod(text, &end);
    while (end && *end && std::iswspace(*end)) ++end;
    return end != text && end && *end == L'\0' && std::isfinite(value);
}

void populate_raster_edit_dialog(HWND dialog, RasterEditDialogState& state) {
    const auto label = [&](const wchar_t* vi, const wchar_t* en) {
        return std::wstring(state.vietnamese ? vi : en);
    };
    SetWindowTextW(dialog, state.crop
        ? (state.vietnamese ? L"Cắt ảnh" : L"Crop Image")
        : state.levels
            ? (state.vietnamese ? L"Levels ảnh" : L"Image Levels")
            : state.hsl
                ? (state.vietnamese ? L"HSL ảnh" : L"Image HSL")
                : state.curves
                    ? (state.vietnamese ? L"Đường cong ảnh" : L"Image Curves")
                    : state.white_balance
                        ? (state.vietnamese ? L"Cân bằng trắng ảnh" : L"Image White Balance")
                    : state.vibrance
                        ? (state.vietnamese ? L"Tăng cường màu ảnh" : L"Image Vibrance")
                    : state.selective_hsl
                        ? (state.vietnamese ? L"HSL theo dải màu" : L"Selective HSL")
                    : state.light
                        ? (state.vietnamese ? L"Ánh sáng ảnh" : L"Image Light")
                    : state.color_balance
                        ? (state.vietnamese ? L"Cân bằng màu ảnh" : L"Image Color Balance")
                    : state.clone_stamp
                        ? (state.vietnamese ? L"Clone Stamp · một nét" : L"Clone Stamp · one dab")
                    : state.channel_mixer
                        ? (state.vietnamese ? L"Bộ trộn kênh RGB" : L"RGB Channel Mixer")
                    : (state.vietnamese ? L"Phơi sáng" : L"Exposure"));
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (state.crop) {
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"X bắt đầu (0–1)", L"Left X (0–1)", kCropXId, state.x},
            {L"Y bắt đầu (0–1)", L"Top Y (0–1)", kCropYId, state.y},
            {L"Chiều rộng (0–1)", L"Width (0–1)", kCropWidthId, state.width},
            {L"Chiều cao (0–1)", L"Height (0–1)", kCropHeightId, state.height}};
        int y = 12;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 3, 132, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                156, y, 198, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 30;
        }
        add_control(dialog, L"STATIC",
            label(L"Tọa độ và kích thước tương đối với ảnh trước nút cắt này.",
                  L"Position and size are relative to the image before this crop node."),
            SS_LEFT | SS_NOPREFIX, 16, 134, 340, 28, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 170, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 170, 78, 26, IDCANCEL, font);
    } else if (state.levels) {
        add_control(dialog, L"STATIC", label(L"Kênh", L"Channel"),
            SS_LEFT | WS_GROUP, 16, 14, 132, 22, kLevelsChannelId + 100, font);
        const HWND channel = add_control(dialog, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            156, 11, 198, 110, kLevelsChannelId, font);
        const wchar_t* channel_names[] = {
            state.vietnamese ? L"RGB tổng hợp" : L"Composite RGB",
            state.vietnamese ? L"Đỏ" : L"Red",
            state.vietnamese ? L"Lục" : L"Green",
            state.vietnamese ? L"Lam" : L"Blue"};
        for (const auto* name : channel_names)
            SendMessageW(channel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(channel, CB_SETCURSEL,
            static_cast<WPARAM>(state.levels_params.channel), 0);
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Đen đầu vào (0–1)", L"Input black (0–1)", kLevelsBlackInId,
                state.levels_params.black_in},
            {L"Trắng đầu vào (0–1)", L"Input white (0–1)", kLevelsWhiteInId,
                state.levels_params.white_in},
            {L"Gamma (0.1–10)", L"Gamma (0.1–10)", kLevelsGammaId,
                state.levels_params.gamma},
            {L"Đen đầu ra (0–1)", L"Output black (0–1)", kLevelsBlackOutId,
                state.levels_params.black_out},
            {L"Trắng đầu ra (0–1)", L"Output white (0–1)", kLevelsWhiteOutId,
                state.levels_params.white_out}};
        int y = 43;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 3, 132, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                156, y, 198, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 27;
        }
        add_control(dialog, L"STATIC",
            label(L"Áp dụng trên giá trị sRGB đã mã hóa; alpha giữ nguyên.",
                  L"Applied to encoded sRGB values; alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 181, 340, 28, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 218, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 218, 78, 26, IDCANCEL, font);
    } else if (state.hsl) {
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Hue / Sắc màu (−180…180°)", L"Hue (−180…180°)", kHslHueId,
                state.hsl_params.hue_degrees},
            {L"Saturation / Bão hòa (−100…100%)", L"Saturation (−100…100%)",
                kHslSaturationId, state.hsl_params.saturation_percent},
            {L"Lightness / Độ sáng HSL (−100…100%)",
                L"HSL Lightness (−100…100%)", kHslLightnessId,
                state.hsl_params.lightness_percent}};
        int y = 18;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y, 338, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                16, y + 20, 338, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 51;
        }
        add_control(dialog, L"STATIC",
            label(L"Tính trên sRGB đã mã hóa; màu xám không đổi Hue; alpha giữ nguyên.",
                  L"Applied to encoded sRGB; gray pixels keep their hue; alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 173, 340, 32, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 215, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 215, 78, 26, IDCANCEL, font);
    } else if (state.curves) {
        add_control(dialog, L"STATIC", label(L"Kênh", L"Channel"),
            SS_LEFT | WS_GROUP, 16, 14, 132, 22, kCurvesChannelId + 100, font);
        const HWND channel = add_control(dialog, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            156, 11, 198, 110, kCurvesChannelId, font);
        const wchar_t* channel_names[] = {
            state.vietnamese ? L"RGB tổng hợp" : L"Composite RGB",
            state.vietnamese ? L"Đỏ" : L"Red",
            state.vietnamese ? L"Lục" : L"Green",
            state.vietnamese ? L"Lam" : L"Blue"};
        for (const auto* name : channel_names)
            SendMessageW(channel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(channel, CB_SETCURSEL,
            static_cast<WPARAM>(state.curves_params.channel), 0);
        const wchar_t* vi_labels[] = {
            L"Đầu ra bóng (0–100%)", L"Đầu ra 25% (0–100%)",
            L"Đầu ra trung gian (0–100%)", L"Đầu ra 75% (0–100%)",
            L"Đầu ra vùng sáng (0–100%)"};
        const wchar_t* en_labels[] = {
            L"Shadow output (0–100%)", L"25% output (0–100%)",
            L"Midtone output (0–100%)", L"75% output (0–100%)",
            L"Highlight output (0–100%)"};
        for (size_t index = 0; index < state.curves_params.output_points.size(); ++index) {
            const int field_id = kCurvesPoint0Id + static_cast<int>(index);
            const int y = 43 + static_cast<int>(index) * 27;
            add_control(dialog, L"STATIC", label(vi_labels[index], en_labels[index]),
                SS_LEFT | WS_GROUP, 16, y + 3, 132, 20, field_id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                156, y, 198, 24, field_id, font);
            set_number(dialog, field_id,
                state.curves_params.output_points[index] * 100.0);
        }
        add_control(dialog, L"STATIC",
            label(L"Nội suy tuyến tính giữa năm điểm trên sRGB mã hóa; alpha giữ nguyên.",
                  L"Piecewise linear through five encoded-sRGB points; alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 181, 340, 28, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 218, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 218, 78, 26, IDCANCEL, font);
    } else if (state.white_balance) {
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Nhiệt độ tương đối (−100…100%)", L"Relative temperature (−100…100%)",
                kWhiteBalanceTemperatureId,
                state.white_balance_params.temperature_shift_percent},
            {L"Tint xanh lá–đỏ tía (−100…100%)", L"Green–magenta tint (−100…100%)",
                kWhiteBalanceTintId, state.white_balance_params.tint_percent}};
        int y = 28;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y, 338, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                16, y + 21, 338, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 54;
        }
        add_control(dialog, L"STATIC",
            label(L"Chỉnh tương đối trên sRGB tuyến tính; không phải số Kelvin. Alpha giữ nguyên.",
                  L"Relative linear-light sRGB correction, not Kelvin; alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 142, 340, 32, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 184, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 184, 78, 26, IDCANCEL, font);
    } else if (state.vibrance) {
        add_control(dialog, L"STATIC",
            label(L"Mức tăng cường màu (−100…100%)", L"Vibrance amount (−100…100%)"),
            SS_LEFT | WS_GROUP, 16, 27, 338, 20, kVibranceAmountId + 100, font);
        add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
            16, 50, 338, 24, kVibranceAmountId, font);
        set_number(dialog, kVibranceAmountId, state.vibrance_params.amount_percent);
        add_control(dialog, L"STATIC",
            label(L"Màu xám giữ nguyên; màu ít bão hòa tăng mạnh hơn. Không nhận diện tông da; giữ alpha.",
                  L"Neutral grays stay unchanged; muted colors increase more. No skin-tone detection; alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 88, 340, 42, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 144, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 144, 78, 26, IDCANCEL, font);
    } else if (state.light) {
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Phơi sáng (−5…5 EV)", L"Exposure (−5…5 EV)", kLightExposureId, state.light_params.exposure_ev},
            {L"Tương phản (−100…100%)", L"Contrast (−100…100%)", kLightContrastId, state.light_params.contrast_percent},
            {L"Vùng sáng (−100…100%)", L"Highlights (−100…100%)", kLightHighlightsId, state.light_params.highlights_percent},
            {L"Vùng tối (−100…100%)", L"Shadows (−100…100%)", kLightShadowsId, state.light_params.shadows_percent},
            {L"Trắng (−100…100%)", L"Whites (−100…100%)", kLightWhitesId, state.light_params.whites_percent},
            {L"Đen (−100…100%)", L"Blacks (−100…100%)", kLightBlacksId, state.light_params.blacks_percent}};
        int y = 10;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 4, 152, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                176, y, 178, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 29;
        }
        add_control(dialog, L"STATIC",
            label(L"Tính trên sRGB tuyến tính; vùng sáng dương làm tối, vùng tối dương nâng sáng. Alpha giữ nguyên.",
                  L"Linear-light sRGB; positive Highlights darken and positive Shadows lift. Alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 190, 340, 35, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 237, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 237, 78, 26, IDCANCEL, font);
    } else if (state.color_balance) {
        add_control(dialog, L"STATIC", label(L"Dải sắc độ", L"Tonal range"),
            SS_LEFT | WS_GROUP, 16, 14, 152, 22, kColorBalanceRangeId + 100, font);
        const HWND range = add_control(dialog, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            176, 11, 178, 92, kColorBalanceRangeId, font);
        const wchar_t* range_names[] = {
            state.vietnamese ? L"Vùng tối" : L"Shadows",
            state.vietnamese ? L"Trung gian" : L"Midtones",
            state.vietnamese ? L"Vùng sáng" : L"Highlights"};
        for (const auto* name : range_names)
            SendMessageW(range, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(range, CB_SETCURSEL,
            static_cast<WPARAM>(state.color_balance_active_range), 0);
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Cyan ↔ Đỏ (−100…100%)", L"Cyan ↔ Red (−100…100%)",
                kColorBalanceRedCyanId, state.color_balance_params.red_cyan_percent},
            {L"Đỏ tía ↔ Lục (−100…100%)", L"Magenta ↔ Green (−100…100%)",
                kColorBalanceGreenMagentaId,
                state.color_balance_params.green_magenta_percent},
            {L"Vàng ↔ Lam (−100…100%)", L"Yellow ↔ Blue (−100…100%)",
                kColorBalanceBlueYellowId,
                state.color_balance_params.blue_yellow_percent}};
        int y = 49;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 4, 152, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                176, y, 178, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 31;
        }
        const HWND preserve = add_control(dialog, L"BUTTON",
            label(L"Bảo toàn độ sáng", L"Preserve luminosity"),
            BS_AUTOCHECKBOX | WS_TABSTOP, 16, 148, 338, 22,
            kColorBalancePreserveLuminosityId, font);
        SendMessageW(preserve, BM_SETCHECK,
            state.color_balance_params.preserve_luminosity
                ? BST_CHECKED : BST_UNCHECKED, 0);
        add_control(dialog, L"STATIC",
            label(L"Áp dụng cho dải đã chọn trên RGB sRGB tuyến tính; dương tăng màu phía phải. Alpha giữ nguyên.",
                  L"Applies to the selected range in linear-light sRGB; positive values move toward the right-hand color. Alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 178, 338, 35, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 222, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 222, 78, 26, IDCANCEL, font);
    } else if (state.clone_stamp) {
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Nguồn X (px)", L"Source X (px)", kCloneSourceXId, state.clone_source_x_px},
            {L"Nguồn Y (px)", L"Source Y (px)", kCloneSourceYId, state.clone_source_y_px},
            {L"Đích X (px)", L"Target X (px)", kCloneTargetXId, state.clone_target_x_px},
            {L"Đích Y (px)", L"Target Y (px)", kCloneTargetYId, state.clone_target_y_px},
            {L"Bán kính (px)", L"Radius (px)", kCloneRadiusId, state.clone_radius_px},
            {L"Độ cứng (0–100%)", L"Hardness (0–100%)", kCloneHardnessId,
                state.clone_stamp_params.hardness_percent},
            {L"Opacity (0–100%)", L"Opacity (0–100%)", kCloneOpacityId,
                state.clone_stamp_params.opacity_percent}};
        int y = 8;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 4, 154, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                176, y, 178, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 29;
        }
        add_control(dialog, L"STATIC",
            label(L"Tọa độ lấy tâm pixel (0…W−1 / 0…H−1). Nguồn được chụp trước nét; vùng cọ tối đa 25% cạnh ngắn.",
                  L"Coordinates are pixel centers (0…W−1 / 0…H−1). Source is frozen before the dab; radius is capped at 25% of the shorter edge."),
            SS_LEFT | SS_NOPREFIX, 16, 214, 340, 40, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng nét", L"Apply Dab"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 260, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 260, 78, 26, IDCANCEL, font);
    } else if (state.channel_mixer) {
        const wchar_t* vi_labels[] = {
            L"Đỏ đầu ra ← Đỏ vào", L"Đỏ đầu ra ← Lục vào", L"Đỏ đầu ra ← Lam vào",
            L"Lục đầu ra ← Đỏ vào", L"Lục đầu ra ← Lục vào", L"Lục đầu ra ← Lam vào",
            L"Lam đầu ra ← Đỏ vào", L"Lam đầu ra ← Lục vào", L"Lam đầu ra ← Lam vào"};
        const wchar_t* en_labels[] = {
            L"Output Red ← Input Red", L"Output Red ← Input Green", L"Output Red ← Input Blue",
            L"Output Green ← Input Red", L"Output Green ← Input Green", L"Output Green ← Input Blue",
            L"Output Blue ← Input Red", L"Output Blue ← Input Green", L"Output Blue ← Input Blue"};
        for (size_t index = 0; index < state.channel_mixer_params.coefficients.size(); ++index) {
            const int id = kChannelMixerCoefficient0Id + static_cast<int>(index);
            const int y = 10 + static_cast<int>(index) * 28;
            add_control(dialog, L"STATIC", label(vi_labels[index], en_labels[index]),
                SS_LEFT | WS_GROUP, 16, y + 4, 190, 20, id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                216, y, 138, 24, id, font);
            set_number(dialog, id, state.channel_mixer_params.coefficients[index]);
        }
        add_control(dialog, L"STATIC",
            label(L"Hệ số −2…2 trên RGB tuyến tính; giá trị đầu bảng là ma trận identity. Alpha giữ nguyên.",
                  L"Coefficients −2…2 in linear-light RGB; the default matrix is identity. Alpha is preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 270, 340, 34, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 314, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 314, 78, 26, IDCANCEL, font);
    } else if (state.selective_hsl) {
        add_control(dialog, L"STATIC", label(L"Dải màu", L"Hue range"),
            SS_LEFT | WS_GROUP, 16, 14, 132, 22, kSelectiveHslRangeId + 100, font);
        const HWND range = add_control(dialog, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            156, 11, 198, 130, kSelectiveHslRangeId, font);
        const wchar_t* range_names[] = {
            state.vietnamese ? L"Đỏ" : L"Reds",
            state.vietnamese ? L"Vàng" : L"Yellows",
            state.vietnamese ? L"Lục" : L"Greens",
            state.vietnamese ? L"Lục lam" : L"Cyans",
            state.vietnamese ? L"Lam" : L"Blues",
            state.vietnamese ? L"Đỏ tía" : L"Magentas"};
        for (const auto* name : range_names)
            SendMessageW(range, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(range, CB_SETCURSEL,
            static_cast<WPARAM>(state.selective_hsl_params.range), 0);
        struct Field { const wchar_t* vi; const wchar_t* en; int id; double value; };
        const Field fields[] = {
            {L"Hue (−30…30°)", L"Hue (−30…30°)", kSelectiveHslHueId,
                state.selective_hsl_params.hue_degrees},
            {L"Bão hòa (−100…100%)", L"Saturation (−100…100%)",
                kSelectiveHslSaturationId,
                state.selective_hsl_params.saturation_percent},
            {L"Độ sáng HSL (−100…100%)", L"HSL lightness (−100…100%)",
                kSelectiveHslLightnessId,
                state.selective_hsl_params.lightness_percent},
            {L"Feather (0…100%)", L"Feather (0…100%)", kSelectiveHslFeatherId,
                state.selective_hsl_params.feather_percent}};
        int y = 46;
        for (const auto& field : fields) {
            add_control(dialog, L"STATIC", label(field.vi, field.en),
                SS_LEFT | WS_GROUP, 16, y + 3, 188, 20, field.id + 100, font);
            add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
                214, y, 140, 24, field.id, font);
            set_number(dialog, field.id, field.value);
            y += 28;
        }
        add_control(dialog, L"STATIC",
            label(L"Mỗi dải phủ 60°; Feather làm mềm biên. Tính trên sRGB mã hóa; xám và alpha giữ nguyên.",
                  L"Each range spans 60°; feather softens its edge. Encoded sRGB; gray and alpha are preserved."),
            SS_LEFT | SS_NOPREFIX, 16, 166, 340, 38, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 215, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 215, 78, 26, IDCANCEL, font);
    } else {
        add_control(dialog, L"STATIC", label(L"Phơi sáng (EV)", L"Exposure (EV)"),
            SS_LEFT | WS_GROUP, 16, 25, 132, 22, kExposureId + 100, font);
        add_control(dialog, L"EDIT", L"", ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
            156, 22, 198, 26, kExposureId, font);
        set_number(dialog, kExposureId, state.exposure_ev);
        add_control(dialog, L"STATIC",
            label(L"Giá trị -10 đến +10 EV. Tính trên ánh sáng tuyến tính sRGB.",
                  L"Range -10 to +10 EV. Applied in linear-light sRGB."),
            SS_LEFT | SS_NOPREFIX, 16, 62, 340, 32, kEditHelpId, font);
        add_control(dialog, L"BUTTON", label(L"Áp dụng", L"Apply"),
            BS_DEFPUSHBUTTON | WS_TABSTOP, 190, 108, 78, 26, IDOK, font);
        add_control(dialog, L"BUTTON", label(L"Hủy", L"Cancel"),
            BS_PUSHBUTTON | WS_TABSTOP, 276, 108, 78, 26, IDCANCEL, font);
    }
    SetFocus(GetDlgItem(dialog, state.crop ? kCropXId :
        state.levels ? kLevelsBlackInId : state.hsl ? kHslHueId :
        state.curves ? kCurvesPoint0Id : state.white_balance
            ? kWhiteBalanceTemperatureId : state.vibrance
                ? kVibranceAmountId : state.selective_hsl
                    ? kSelectiveHslHueId : state.light ? kLightExposureId :
                state.clone_stamp ? kCloneSourceXId :
        state.channel_mixer ? kChannelMixerCoefficient0Id :
        state.color_balance ? kColorBalanceRangeId : kExposureId));
}

INT_PTR CALLBACK raster_edit_dialog_proc(HWND dialog, UINT message,
                                         WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RasterEditDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<RasterEditDialogState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        populate_raster_edit_dialog(dialog, *state);
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND &&
        LOWORD(wparam) == kColorBalanceRangeId &&
        HIWORD(wparam) == CBN_SELCHANGE && state->color_balance) {
        const LRESULT selected_range = SendDlgItemMessageW(
            dialog, kColorBalanceRangeId, CB_GETCURSEL, 0, 0);
        if (selected_range < 0 || selected_range > 2) return TRUE;
        auto current = state->color_balance_params;
        const bool current_fields_valid =
            read_number(dialog, kColorBalanceRedCyanId,
                        current.red_cyan_percent) &&
            read_number(dialog, kColorBalanceGreenMagentaId,
                        current.green_magenta_percent) &&
            read_number(dialog, kColorBalanceBlueYellowId,
                        current.blue_yellow_percent) &&
            std::abs(current.red_cyan_percent) <= 100.0 &&
            std::abs(current.green_magenta_percent) <= 100.0 &&
            std::abs(current.blue_yellow_percent) <= 100.0;
        if (!current_fields_valid) {
            SendDlgItemMessageW(dialog, kColorBalanceRangeId, CB_SETCURSEL,
                static_cast<WPARAM>(state->color_balance_active_range), 0);
            SetDlgItemTextW(dialog, kEditHelpId, state->vietnamese
                ? L"Sửa các giá trị hiện tại về đúng phạm vi trước khi đổi dải sắc độ."
                : L"Correct the current values before changing the tonal range.");
            return TRUE;
        }
        current.range = static_cast<document::RasterTonalRange>(
            state->color_balance_active_range);
        current.preserve_luminosity = SendDlgItemMessageW(
            dialog, kColorBalancePreserveLuminosityId,
            BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->color_balance_by_range[
            static_cast<size_t>(state->color_balance_active_range)] = current;
        state->color_balance_params = state->color_balance_by_range[
            static_cast<size_t>(selected_range)];
        state->color_balance_active_range = static_cast<int>(selected_range);
        state->color_balance_params.range =
            static_cast<document::RasterTonalRange>(selected_range);
        set_number(dialog, kColorBalanceRedCyanId,
            state->color_balance_params.red_cyan_percent);
        set_number(dialog, kColorBalanceGreenMagentaId,
            state->color_balance_params.green_magenta_percent);
        set_number(dialog, kColorBalanceBlueYellowId,
            state->color_balance_params.blue_yellow_percent);
        SendDlgItemMessageW(dialog, kColorBalancePreserveLuminosityId,
            BM_SETCHECK, state->color_balance_params.preserve_luminosity
                ? BST_CHECKED : BST_UNCHECKED, 0);
        SetDlgItemTextW(dialog, kEditHelpId, state->vietnamese
            ? L"Ba dải sắc độ có giá trị riêng; dương dịch về đỏ, lục và lam."
            : L"Each tonal range keeps its own values; positive shifts toward red, green and blue.");
        return TRUE;
    }
    if (message == WM_COMMAND &&
        LOWORD(wparam) == kSelectiveHslRangeId &&
        HIWORD(wparam) == CBN_SELCHANGE && state->selective_hsl) {
        const LRESULT selected_range = SendDlgItemMessageW(
            dialog, kSelectiveHslRangeId, CB_GETCURSEL, 0, 0);
        if (selected_range < 0 || selected_range > 5) return TRUE;
        auto current = state->selective_hsl_params;
        const bool current_fields_valid =
            read_number(dialog, kSelectiveHslHueId, current.hue_degrees) &&
            read_number(dialog, kSelectiveHslSaturationId,
                        current.saturation_percent) &&
            read_number(dialog, kSelectiveHslLightnessId,
                        current.lightness_percent) &&
            read_number(dialog, kSelectiveHslFeatherId, current.feather_percent) &&
            std::abs(current.hue_degrees) <= 30.0 &&
            std::abs(current.saturation_percent) <= 100.0 &&
            std::abs(current.lightness_percent) <= 100.0 &&
            current.feather_percent >= 0.0 && current.feather_percent <= 100.0;
        if (!current_fields_valid) {
            SendDlgItemMessageW(dialog, kSelectiveHslRangeId, CB_SETCURSEL,
                static_cast<WPARAM>(state->selective_hsl_active_range), 0);
            SetDlgItemTextW(dialog, kEditHelpId, state->vietnamese
                ? L"Sửa các giá trị hiện tại về đúng phạm vi trước khi đổi dải màu."
                : L"Correct the current values before changing the hue range.");
            return TRUE;
        }
        current.range = static_cast<document::RasterHueRange>(
            state->selective_hsl_active_range);
        state->selective_hsl_by_range[
            static_cast<size_t>(state->selective_hsl_active_range)] = current;
        state->selective_hsl_params =
            state->selective_hsl_by_range[static_cast<size_t>(selected_range)];
        state->selective_hsl_active_range = static_cast<int>(selected_range);
        set_number(dialog, kSelectiveHslHueId,
            state->selective_hsl_params.hue_degrees);
        set_number(dialog, kSelectiveHslSaturationId,
            state->selective_hsl_params.saturation_percent);
        set_number(dialog, kSelectiveHslLightnessId,
            state->selective_hsl_params.lightness_percent);
        set_number(dialog, kSelectiveHslFeatherId,
            state->selective_hsl_params.feather_percent);
        SetDlgItemTextW(dialog, kEditHelpId, state->vietnamese
            ? L"Mỗi dải phủ 60°; Feather làm mềm biên; màu xám và alpha giữ nguyên."
            : L"Each range spans 60°; feather softens its edge; gray and alpha are preserved.");
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        bool valid = false;
        if (state->crop) {
            valid = read_number(dialog, kCropXId, state->x) &&
                read_number(dialog, kCropYId, state->y) &&
                read_number(dialog, kCropWidthId, state->width) &&
                read_number(dialog, kCropHeightId, state->height) &&
                state->x >= 0.0 && state->y >= 0.0 && state->width > 0.0 &&
                state->height > 0.0 && state->x + state->width <= 1.0 &&
                state->y + state->height <= 1.0;
        } else if (state->levels) {
            const LRESULT channel = SendDlgItemMessageW(
                dialog, kLevelsChannelId, CB_GETCURSEL, 0, 0);
            valid = channel >= 0 && channel <= 3 &&
                read_number(dialog, kLevelsBlackInId, state->levels_params.black_in) &&
                read_number(dialog, kLevelsWhiteInId, state->levels_params.white_in) &&
                read_number(dialog, kLevelsGammaId, state->levels_params.gamma) &&
                read_number(dialog, kLevelsBlackOutId, state->levels_params.black_out) &&
                read_number(dialog, kLevelsWhiteOutId, state->levels_params.white_out) &&
                state->levels_params.black_in >= 0.0 &&
                state->levels_params.white_in <= 1.0 &&
                state->levels_params.black_in < state->levels_params.white_in &&
                state->levels_params.gamma >= 0.1 && state->levels_params.gamma <= 10.0 &&
                state->levels_params.black_out >= 0.0 &&
                state->levels_params.white_out <= 1.0 &&
                state->levels_params.black_out <= state->levels_params.white_out;
            if (valid)
                state->levels_params.channel =
                    static_cast<document::RasterToneChannel>(channel);
        } else if (state->hsl) {
            valid = read_number(dialog, kHslHueId, state->hsl_params.hue_degrees) &&
                read_number(dialog, kHslSaturationId,
                            state->hsl_params.saturation_percent) &&
                read_number(dialog, kHslLightnessId,
                            state->hsl_params.lightness_percent) &&
                std::abs(state->hsl_params.hue_degrees) <= 180.0 &&
                std::abs(state->hsl_params.saturation_percent) <= 100.0 &&
                std::abs(state->hsl_params.lightness_percent) <= 100.0;
        } else if (state->curves) {
            const LRESULT channel = SendDlgItemMessageW(
                dialog, kCurvesChannelId, CB_GETCURSEL, 0, 0);
            valid = channel >= 0 && channel <= 3;
            std::array<double, 5> values{};
            for (size_t index = 0; valid && index < values.size(); ++index) {
                valid = read_number(dialog, kCurvesPoint0Id + static_cast<int>(index),
                                    values[index]) &&
                    values[index] >= 0.0 && values[index] <= 100.0;
                values[index] /= 100.0;
            }
            if (valid) {
                state->curves_params.channel =
                    static_cast<document::RasterToneChannel>(channel);
                state->curves_params.output_points = values;
            }
        } else if (state->white_balance) {
            valid = read_number(dialog, kWhiteBalanceTemperatureId,
                    state->white_balance_params.temperature_shift_percent) &&
                read_number(dialog, kWhiteBalanceTintId,
                    state->white_balance_params.tint_percent) &&
                std::abs(state->white_balance_params.temperature_shift_percent) <= 100.0 &&
                std::abs(state->white_balance_params.tint_percent) <= 100.0;
        } else if (state->vibrance) {
            valid = read_number(dialog, kVibranceAmountId,
                    state->vibrance_params.amount_percent) &&
                std::abs(state->vibrance_params.amount_percent) <= 100.0;
        } else if (state->light) {
            auto& value = state->light_params;
            valid = read_number(dialog, kLightExposureId, value.exposure_ev) &&
                read_number(dialog, kLightContrastId, value.contrast_percent) &&
                read_number(dialog, kLightHighlightsId, value.highlights_percent) &&
                read_number(dialog, kLightShadowsId, value.shadows_percent) &&
                read_number(dialog, kLightWhitesId, value.whites_percent) &&
                read_number(dialog, kLightBlacksId, value.blacks_percent) &&
                std::abs(value.exposure_ev) <= 5.0 &&
                std::abs(value.contrast_percent) <= 100.0 &&
                std::abs(value.highlights_percent) <= 100.0 &&
                std::abs(value.shadows_percent) <= 100.0 &&
                std::abs(value.whites_percent) <= 100.0 &&
                std::abs(value.blacks_percent) <= 100.0;
        } else if (state->color_balance) {
            auto& value = state->color_balance_params;
            const LRESULT range = SendDlgItemMessageW(
                dialog, kColorBalanceRangeId, CB_GETCURSEL, 0, 0);
            valid = range >= 0 && range <= 2 &&
                read_number(dialog, kColorBalanceRedCyanId,
                            value.red_cyan_percent) &&
                read_number(dialog, kColorBalanceGreenMagentaId,
                            value.green_magenta_percent) &&
                read_number(dialog, kColorBalanceBlueYellowId,
                            value.blue_yellow_percent) &&
                std::abs(value.red_cyan_percent) <= 100.0 &&
                std::abs(value.green_magenta_percent) <= 100.0 &&
                std::abs(value.blue_yellow_percent) <= 100.0;
            if (valid) {
                value.range = static_cast<document::RasterTonalRange>(range);
                value.preserve_luminosity = SendDlgItemMessageW(
                    dialog, kColorBalancePreserveLuminosityId,
                    BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->color_balance_active_range = static_cast<int>(range);
                state->color_balance_by_range[static_cast<size_t>(range)] = value;
            }
        } else if (state->clone_stamp) {
            valid = read_number(dialog, kCloneSourceXId, state->clone_source_x_px) &&
                read_number(dialog, kCloneSourceYId, state->clone_source_y_px) &&
                read_number(dialog, kCloneTargetXId, state->clone_target_x_px) &&
                read_number(dialog, kCloneTargetYId, state->clone_target_y_px) &&
                read_number(dialog, kCloneRadiusId, state->clone_radius_px) &&
                read_number(dialog, kCloneHardnessId,
                    state->clone_stamp_params.hardness_percent) &&
                read_number(dialog, kCloneOpacityId,
                    state->clone_stamp_params.opacity_percent) &&
                state->clone_width_px > 0 && state->clone_height_px > 0 &&
                state->clone_source_x_px >= 0.0 &&
                state->clone_source_x_px <= state->clone_width_px - 1.0 &&
                state->clone_source_y_px >= 0.0 &&
                state->clone_source_y_px <= state->clone_height_px - 1.0 &&
                state->clone_target_x_px >= 0.0 &&
                state->clone_target_x_px <= state->clone_width_px - 1.0 &&
                state->clone_target_y_px >= 0.0 &&
                state->clone_target_y_px <= state->clone_height_px - 1.0 &&
                state->clone_radius_px > 0.0 &&
                state->clone_radius_px <= 0.25 *
                    (std::min)(state->clone_width_px, state->clone_height_px) &&
                state->clone_stamp_params.hardness_percent >= 0.0 &&
                state->clone_stamp_params.hardness_percent <= 100.0 &&
                state->clone_stamp_params.opacity_percent >= 0.0 &&
                state->clone_stamp_params.opacity_percent <= 100.0;
            if (valid) {
                const double short_edge = (std::min)(state->clone_width_px,
                                                     state->clone_height_px);
                state->clone_stamp_params.source_x = state->clone_width_px > 1
                    ? state->clone_source_x_px / (state->clone_width_px - 1.0) : 0.5;
                state->clone_stamp_params.source_y = state->clone_height_px > 1
                    ? state->clone_source_y_px / (state->clone_height_px - 1.0) : 0.5;
                state->clone_stamp_params.target_x = state->clone_width_px > 1
                    ? state->clone_target_x_px / (state->clone_width_px - 1.0) : 0.5;
                state->clone_stamp_params.target_y = state->clone_height_px > 1
                    ? state->clone_target_y_px / (state->clone_height_px - 1.0) : 0.5;
                state->clone_stamp_params.radius_percent =
                    state->clone_radius_px / short_edge * 100.0;
                valid = state->clone_stamp_params.radius_percent <= 25.0;
            }
        } else if (state->channel_mixer) {
            valid = true;
            for (size_t index = 0; index < state->channel_mixer_params.coefficients.size(); ++index) {
                auto& coefficient = state->channel_mixer_params.coefficients[index];
                valid = valid && read_number(dialog,
                    kChannelMixerCoefficient0Id + static_cast<int>(index), coefficient) &&
                    coefficient >= -2.0 && coefficient <= 2.0;
            }
        } else if (state->selective_hsl) {
            const LRESULT range = SendDlgItemMessageW(
                dialog, kSelectiveHslRangeId, CB_GETCURSEL, 0, 0);
            valid = range >= 0 && range <= 5 &&
                read_number(dialog, kSelectiveHslHueId,
                    state->selective_hsl_params.hue_degrees) &&
                read_number(dialog, kSelectiveHslSaturationId,
                    state->selective_hsl_params.saturation_percent) &&
                read_number(dialog, kSelectiveHslLightnessId,
                    state->selective_hsl_params.lightness_percent) &&
                read_number(dialog, kSelectiveHslFeatherId,
                    state->selective_hsl_params.feather_percent) &&
                std::abs(state->selective_hsl_params.hue_degrees) <= 30.0 &&
                std::abs(state->selective_hsl_params.saturation_percent) <= 100.0 &&
                std::abs(state->selective_hsl_params.lightness_percent) <= 100.0 &&
                state->selective_hsl_params.feather_percent >= 0.0 &&
                state->selective_hsl_params.feather_percent <= 100.0;
            if (valid)
                state->selective_hsl_params.range =
                    static_cast<document::RasterHueRange>(range);
            if (valid) {
                state->selective_hsl_active_range = static_cast<int>(range);
                state->selective_hsl_by_range[static_cast<size_t>(range)] =
                    state->selective_hsl_params;
            }
        } else {
            valid = read_number(dialog, kExposureId, state->exposure_ev) &&
                state->exposure_ev >= -10.0 && state->exposure_ev <= 10.0;
        }
        if (!valid) {
            SetDlgItemTextW(dialog, kEditHelpId, state->crop
                ? (state->vietnamese
                    ? L"Nhập số hữu hạn trong khoảng hợp lệ; vùng cắt phải nằm trong ảnh."
                    : L"Enter finite values in range; the crop rectangle must stay inside the image.")
                : state->levels
                    ? (state->vietnamese
                        ? L"Kiểm tra các giá trị [0–1], gamma 0.1–10 và đen đầu vào nhỏ hơn trắng."
                        : L"Use [0–1] values, gamma 0.1–10, and input black below input white.")
                    : state->hsl
                    ? (state->vietnamese
                        ? L"Hue phải nằm trong ±180°; Saturation và Lightness trong ±100%."
                        : L"Hue must be within ±180°; Saturation and Lightness within ±100%.")
                    : state->curves
                        ? (state->vietnamese
                            ? L"Chọn kênh và nhập năm giá trị hữu hạn trong khoảng 0–100%."
                            : L"Choose a channel and enter five finite values from 0 to 100%.")
                    : state->white_balance
                        ? (state->vietnamese
                            ? L"Nhiệt độ và tint phải là số hữu hạn trong khoảng ±100%."
                            : L"Temperature and tint must be finite values within ±100%.")
                    : state->vibrance
                        ? (state->vietnamese
                            ? L"Mức tăng cường phải là số hữu hạn trong khoảng ±100%."
                            : L"Vibrance must be a finite value within ±100%.")
                    : state->light
                        ? (state->vietnamese
                            ? L"EV phải trong ±5; mỗi điều khiển sắc độ phải là số hữu hạn trong ±100%."
                            : L"EV must be within ±5; each tonal control must be finite and within ±100%.")
                    : state->color_balance
                        ? (state->vietnamese
                            ? L"Chọn đúng một dải sắc độ; mỗi trục màu phải là số hữu hạn trong ±100%."
                            : L"Choose a tonal range; each color axis must be finite and within ±100%.")
                    : state->clone_stamp
                        ? (state->vietnamese
                            ? L"Tâm nguồn/đích phải trong ảnh; bán kính dương không quá 25% cạnh ngắn; hardness và opacity 0–100%."
                            : L"Source/target centers must be inside the image; radius must be positive and at most 25% of the shorter edge; hardness and opacity are 0–100%.")
                    : state->selective_hsl
                        ? (state->vietnamese
                            ? L"Chọn dải màu; Hue ±30°, bão hòa/độ sáng ±100%, Feather 0–100%."
                            : L"Choose a hue range; Hue ±30°, saturation/lightness ±100%, feather 0–100%.")
                    : (state->vietnamese
                    ? L"Nhập số EV hữu hạn trong khoảng -10 đến +10."
                    : L"Enter a finite EV value between -10 and +10."));
            return TRUE;
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

bool show_raster_edit_dialog(HWND owner, RasterEditDialogState& state) {
    struct EmptyDialog {
        DLGTEMPLATE dialog{};
        WORD menu{0};
        WORD klass{0};
        WORD title{0};
    } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
        DS_CENTER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.dwExtendedStyle = 0;
    layout.dialog.cdit = 0;
    layout.dialog.x = 0;
    layout.dialog.y = 0;
    layout.dialog.cx = 380;
    layout.dialog.cy = state.clone_stamp ? 312 : state.channel_mixer ? 370 : state.crop ? 208 : state.light ? 302 : state.color_balance ? 262 : state.levels ? 256 :
        state.hsl ? 260 : state.curves ? 256 : state.white_balance ? 224 :
        state.vibrance ? 184 : state.selective_hsl ? 260 : 146;
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog,
        owner, raster_edit_dialog_proc, reinterpret_cast<LPARAM>(&state)) == IDOK &&
        state.accepted;
}
#endif

std::optional<document::ObjectId> selected_bitmap_id(const NativeWindow& window) {
    const auto& selected = window.root().vector_controller().selection().selected_ids();
    if (selected.size() != 1) return std::nullopt;
    return selected.front();
}

document::RasterEditNodeId raster_node_id(
    const document::RasterEditGraph& graph, bool crop, bool levels = false,
    bool hsl = false, bool curves = false,
    document::RasterToneChannel curves_channel =
        document::RasterToneChannel::Composite, bool white_balance = false,
    bool vibrance = false, bool light = false, bool channel_mixer = false,
    bool color_balance = false,
    document::RasterTonalRange color_balance_range =
        document::RasterTonalRange::Midtones) {
    uint64_t maximum = 0;
    std::optional<document::RasterEditNodeId> existing;
    for (auto node = graph.nodes.rbegin(); node != graph.nodes.rend(); ++node) {
        bool matches = false;
        if (crop) {
            matches = std::holds_alternative<document::RasterCropParams>(
                node->parameters);
        } else if (levels) {
            matches = std::holds_alternative<document::RasterLevelsParams>(
                node->parameters);
        } else if (hsl) {
            matches = std::holds_alternative<document::RasterHslParams>(
                node->parameters);
        } else if (curves) {
            const auto* parameters = std::get_if<document::RasterCurvesParams>(
                &node->parameters);
            matches = parameters && parameters->channel == curves_channel;
        } else if (white_balance) {
            matches = std::holds_alternative<document::RasterWhiteBalanceParams>(
                node->parameters);
        } else if (vibrance) {
            matches = std::holds_alternative<document::RasterVibranceParams>(
                node->parameters);
        } else if (light) {
            matches = std::holds_alternative<document::RasterLightParams>(
                node->parameters);
        } else if (channel_mixer) {
            matches = std::holds_alternative<document::RasterChannelMixerParams>(
                node->parameters);
        } else if (color_balance) {
            const auto* parameters =
                std::get_if<document::RasterColorBalanceParams>(&node->parameters);
            matches = parameters && parameters->range == color_balance_range;
        } else {
            matches = std::holds_alternative<document::RasterExposureParams>(
                node->parameters);
        }
        if (matches && !existing) existing = node->id;
        maximum = (std::max)(maximum, node->id.value);
    }
    if (existing) return *existing;
    if (maximum == std::numeric_limits<uint64_t>::max()) return {};
    return {maximum + 1};
}

document::RasterEditNodeId selective_hsl_node_id(
    const document::RasterEditGraph& graph, document::RasterHueRange range) {
    uint64_t maximum = 0;
    std::optional<document::RasterEditNodeId> existing;
    for (auto node = graph.nodes.rbegin(); node != graph.nodes.rend(); ++node) {
        if (const auto* parameters =
                std::get_if<document::RasterSelectiveHslParams>(&node->parameters);
            parameters && parameters->range == range && !existing)
            existing = node->id;
        maximum = (std::max)(maximum, node->id.value);
    }
    if (existing) return *existing;
    if (maximum == std::numeric_limits<uint64_t>::max()) return {};
    return {maximum + 1};
}

document::RasterEditNodeId next_raster_node_id(
    const document::RasterEditGraph& graph) {
    uint64_t maximum = 0;
    for (const auto& node : graph.nodes)
        maximum = (std::max)(maximum, node.id.value);
    if (maximum == std::numeric_limits<uint64_t>::max()) return {};
    return {maximum + 1};
}

}  // namespace

bool NativeWindow::can_edit_selected_bitmap(bool require_srgb_preview) const {
    if (root().document().is_content_locked() || raster_profile_conversion_active()) return false;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image || image->locked)
        return false;
    const auto high_precision_pixels = image->rgba16le_pixels();
    if (!image->asset_source && image->source.original_data().empty() &&
        high_precision_pixels.empty())
        return false;
    if (image->width_px <= 0 || image->height_px <= 0) return false;
    const uint64_t pixel_count = static_cast<uint64_t>(image->width_px) *
                                 static_cast<uint64_t>(image->height_px);
    if (pixel_count > SIZE_MAX / sizeof(uint32_t) ||
        image->rgba_pixels().size() != pixel_count)
        return false;
    if (!high_precision_pixels.empty() &&
        (pixel_count > std::numeric_limits<uint32_t>::max() / 8u ||
         high_precision_pixels.size() != pixel_count * 8u))
        return false;
    if (require_srgb_preview &&
        (image->source.preview_pixel_color_basis ==
             document::RasterPixelColorBasis::EmbeddedProfileUnapplied ||
         image->source.preview_pixel_color_basis ==
             document::RasterPixelColorBasis::LegacyUnknown))
        return false;
    return true;
}

std::optional<double> NativeWindow::selected_bitmap_exposure_ev() const {
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return std::nullopt;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return std::nullopt;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* light = std::get_if<document::RasterLightParams>(
                &node->parameters))
            return light->exposure_ev;
        if (const auto* exposure = std::get_if<document::RasterExposureParams>(
                &node->parameters))
            return exposure->exposure_ev;
    }
    return 0.0;
}

bool NativeWindow::set_selected_bitmap_exposure_ev(double exposure_ev) {
    if (!std::isfinite(exposure_ev) || exposure_ev < -10.0 || exposure_ev > 10.0 ||
        !can_edit_selected_bitmap(true))
        return false;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    document::RasterEditNode updated;
    bool found = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (auto* light = std::get_if<document::RasterLightParams>(
                &node->parameters)) {
            if (std::abs(exposure_ev) > 5.0) return false;
            updated = *node;
            auto parameters = *light;
            parameters.exposure_ev = exposure_ev;
            updated.parameters = parameters;
            found = true;
            break;
        }
        if (std::holds_alternative<document::RasterExposureParams>(node->parameters)) {
            updated = *node;
            updated.parameters = document::RasterExposureParams{exposure_ev};
            found = true;
            break;
        }
    }
    if (!found) {
        const auto id = next_raster_node_id(image->edit_graph);
        if (!id.valid()) return false;
        updated = document::RasterEditNode{id, true,
            document::RasterExposureParams{exposure_ev}};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, std::move(updated))).success;
}

document::RasterColorBalanceParams
NativeWindow::selected_bitmap_color_balance() const {
    auto parameters = document::RasterColorBalanceParams{};
    parameters.range = raster_color_balance_active_range_;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return parameters;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return parameters;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* existing =
            std::get_if<document::RasterColorBalanceParams>(&node->parameters);
        if (existing && existing->range == raster_color_balance_active_range_)
            return *existing;
    }
    return parameters;
}

bool NativeWindow::set_selected_bitmap_color_balance_value(
    NumericFieldId field, double value) {
    if (!std::isfinite(value) || std::abs(value) > 100.0 ||
        !can_edit_selected_bitmap(true))
        return false;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    auto parameters = selected_bitmap_color_balance();
    switch (field) {
    case NumericFieldId::RasterColorBalanceRedCyan:
        parameters.red_cyan_percent = value;
        break;
    case NumericFieldId::RasterColorBalanceGreenMagenta:
        parameters.green_magenta_percent = value;
        break;
    case NumericFieldId::RasterColorBalanceBlueYellow:
        parameters.blue_yellow_percent = value;
        break;
    default:
        return false;
    }
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, false, false, false, false,
        true, parameters.range);
    if (!id.valid()) return false;
    std::optional<document::RasterAdjustmentMask> existing_mask;
    bool existing_enabled = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* current =
            std::get_if<document::RasterColorBalanceParams>(&node->parameters);
        if (current && current->range == parameters.range) {
            existing_mask = node->mask;
            existing_enabled = node->enabled;
            break;
        }
    }
    document::RasterEditNode updated{id, existing_enabled, parameters};
    updated.mask = std::move(existing_mask);
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, std::move(updated))).success;
}

bool NativeWindow::toggle_selected_bitmap_color_balance_preserve_luminosity() {
    if (!can_edit_selected_bitmap(true)) return false;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    auto parameters = selected_bitmap_color_balance();
    parameters.preserve_luminosity = !parameters.preserve_luminosity;
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, false, false, false, false,
        true, parameters.range);
    if (!id.valid()) return false;
    std::optional<document::RasterAdjustmentMask> existing_mask;
    bool existing_enabled = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* current =
            std::get_if<document::RasterColorBalanceParams>(&node->parameters);
        if (current && current->range == parameters.range) {
            existing_mask = node->mask;
            existing_enabled = node->enabled;
            break;
        }
    }
    document::RasterEditNode updated{id, existing_enabled, parameters};
    updated.mask = std::move(existing_mask);
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, std::move(updated))).success;
}

void NativeWindow::edit_selected_bitmap_crop() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh có nguồn gốc được giữ lại và không bị khóa để cắt"
            : "Select one unlocked image with a preserved source to crop";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.crop = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* crop = std::get_if<document::RasterCropParams>(
                &node->parameters)) {
            state.x = crop->x;
            state.y = crop->y;
            state.width = crop->width;
            state.height = crop->height;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true,
            document::RasterCropParams{state.x, state.y, state.width, state.height}});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật node cắt · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Crop node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật node cắt ảnh" : "Could not update the crop node");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_exposure() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(1);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    std::optional<document::RasterEditNode> existing_node;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* exposure = std::get_if<document::RasterExposureParams>(
                &node->parameters)) {
            state.exposure_ev = exposure->exposure_ev;
            existing_node = *node;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    document::RasterEditNode updated = existing_node.value_or(
        document::RasterEditNode{id, true,
            document::RasterExposureParams{state.exposure_ev}});
    updated.parameters = document::RasterExposureParams{state.exposure_ev};
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, std::move(updated));
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật phơi sáng · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Exposure node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật phơi sáng ảnh" : "Could not update image exposure");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_light() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(1);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.light = true;
    std::optional<document::RasterAdjustmentMask> existing_mask;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* light = std::get_if<document::RasterLightParams>(
                &node->parameters)) {
            state.light_params = *light;
            existing_mask = node->mask;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, false,
        false, document::RasterToneChannel::Composite, false, false, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    document::RasterEditNode updated{id, true, state.light_params};
    updated.mask = std::move(existing_mask);
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, std::move(updated));
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật ánh sáng · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Light node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật ánh sáng ảnh" : "Could not update image Light");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_color_balance() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh RGB sRGB có nguồn được giữ lại"
            : "Select one sRGB RGB image with a preserved source";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(4);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.color_balance = true;
    for (size_t index = 0; index < state.color_balance_by_range.size(); ++index)
        state.color_balance_by_range[index].range =
            static_cast<document::RasterTonalRange>(index);
    std::array<bool, 3> loaded_range{};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* parameters =
            std::get_if<document::RasterColorBalanceParams>(&node->parameters);
        if (!parameters) continue;
        const size_t range = static_cast<size_t>(parameters->range);
        if (range >= state.color_balance_by_range.size() || loaded_range[range])
            continue;
        state.color_balance_by_range[range] = *parameters;
        state.color_balance_enabled_by_range[range] = node->enabled;
        loaded_range[range] = true;
    }
    state.color_balance_active_range = 1;
    state.color_balance_params = state.color_balance_by_range[1];
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    raster_color_balance_active_range_ = state.color_balance_params.range;
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, false, false, false, false,
        true, state.color_balance_params.range);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    std::optional<document::RasterAdjustmentMask> existing_mask;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* parameters =
            std::get_if<document::RasterColorBalanceParams>(&node->parameters);
        if (parameters && parameters->range == state.color_balance_params.range) {
            existing_mask = node->mask;
            break;
        }
    }
    const size_t range = static_cast<size_t>(state.color_balance_params.range);
    if (range >= state.color_balance_enabled_by_range.size()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Dải Cân bằng màu không hợp lệ" : "Invalid Color Balance range";
        return;
    }
    document::RasterEditNode updated{
        id, state.color_balance_enabled_by_range[range], state.color_balance_params};
    updated.mask = std::move(existing_mask);
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, std::move(updated)));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Cân bằng màu · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Color Balance updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật Cân bằng màu ảnh"
            : "Could not update image Color Balance");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_channel_mixer() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh RGB sRGB có nguồn được giữ lại"
            : "Select one sRGB RGB image with a preserved source";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(6);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.channel_mixer = true;
    std::optional<document::RasterAdjustmentMask> existing_mask;
    bool existing_enabled = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* mixer = std::get_if<document::RasterChannelMixerParams>(
                &node->parameters)) {
            state.channel_mixer_params = *mixer;
            existing_mask = node->mask;
            existing_enabled = node->enabled;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, false, false, false, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    document::RasterEditNode updated{id, existing_enabled,
        state.channel_mixer_params};
    updated.mask = std::move(existing_mask);
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(*selected, std::move(updated)));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật bộ trộn kênh RGB · Ctrl+Z hoàn tác"
            : "RGB Channel Mixer updated · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật bộ trộn kênh" : "Could not update Channel Mixer");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_levels() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(3);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.levels = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* levels = std::get_if<document::RasterLevelsParams>(
                &node->parameters)) {
            state.levels_params = *levels;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.levels_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Levels · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Levels node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật Levels ảnh" : "Could not update image Levels");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_hsl() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(2, 0);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.hsl = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* hsl = std::get_if<document::RasterHslParams>(
                &node->parameters)) {
            state.hsl_params = *hsl;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.hsl_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật HSL · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "HSL node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật HSL ảnh" : "Could not update image HSL");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_curves() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(5);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.curves = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* curves = std::get_if<document::RasterCurvesParams>(
                &node->parameters)) {
            state.curves_params = *curves;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, false, true,
                                   state.curves_params.channel);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.curves_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Curves · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Curves node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật Curves ảnh" : "Could not update image Curves");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_white_balance() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(2, 0);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.white_balance = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* white_balance =
                std::get_if<document::RasterWhiteBalanceParams>(&node->parameters)) {
            state.white_balance_params = *white_balance;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.white_balance_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật cân bằng trắng · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "White Balance node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật cân bằng trắng ảnh"
            : "Could not update image White Balance");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_vibrance() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(2, 0);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.vibrance = true;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* vibrance =
                std::get_if<document::RasterVibranceParams>(&node->parameters)) {
            state.vibrance_params = *vibrance;
            break;
        }
    }
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = raster_node_id(image->edit_graph, false, false, false, false,
        document::RasterToneChannel::Composite, false, true);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.vibrance_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Vibrance · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Vibrance node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật Vibrance ảnh"
            : "Could not update image Vibrance");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_selective_hsl() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    focus_raster_adjustment_in_dock(2, 1);
    if (hwnd_ != nullptr) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.selective_hsl = true;
    for (size_t index = 0; index < state.selective_hsl_by_range.size(); ++index) {
        state.selective_hsl_by_range[index].range =
            static_cast<document::RasterHueRange>(index);
    }
    std::array<bool, 6> loaded_range{};
    bool loaded_active_range = false;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        const auto* parameters =
            std::get_if<document::RasterSelectiveHslParams>(&node->parameters);
        if (!parameters) continue;
        const auto index = static_cast<size_t>(parameters->range);
        if (index >= state.selective_hsl_by_range.size() || loaded_range[index])
            continue;
        state.selective_hsl_by_range[index] = *parameters;
        loaded_range[index] = true;
        if (!loaded_active_range) {
            state.selective_hsl_params = *parameters;
            state.selective_hsl_active_range = static_cast<int>(index);
            loaded_active_range = true;
        }
    }
    state.selective_hsl_params = state.selective_hsl_by_range[
        static_cast<size_t>(state.selective_hsl_active_range)];
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    const auto id = selective_hsl_node_id(
        image->edit_graph, state.selective_hsl_params.range);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{
            id, true, state.selective_hsl_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã cập nhật HSL theo dải màu · đang dựng xem trước · Ctrl+Z hoàn tác"
            : "Selective HSL node updated · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật HSL theo dải màu"
            : "Could not update selective HSL");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::edit_selected_bitmap_clone_stamp() {
#ifdef _WIN32
    if (!can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh sRGB có nguồn được giữ lại; profile chưa áp dụng cần được xử lý trước"
            : "Select one sRGB image with a preserved source; resolve unapplied profiles first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    if (image->edit_graph.nodes.size() >= document::RasterEditGraph::kMaxNodes) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đạt giới hạn số bước sửa ảnh" : "The raster edit graph has reached its node limit";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto [width, height] = raster::raster_edited_pixel_dimensions(*image);
    if (width == 0 || height == 0) return;

    RasterEditDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.clone_stamp = true;
    state.clone_width_px = width;
    state.clone_height_px = height;
    state.clone_stamp_params = {0.25, 0.25, 0.75, 0.75, 2.0, 75.0, 100.0};
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* previous =
                std::get_if<document::RasterCloneStampParams>(&node->parameters)) {
            state.clone_stamp_params = *previous;
            break;
        }
    }
    state.clone_source_x_px = state.clone_stamp_params.source_x * (width - 1.0);
    state.clone_source_y_px = state.clone_stamp_params.source_y * (height - 1.0);
    state.clone_target_x_px = state.clone_stamp_params.target_x * (width - 1.0);
    state.clone_target_y_px = state.clone_stamp_params.target_y * (height - 1.0);
    state.clone_radius_px = state.clone_stamp_params.radius_percent * 0.01 *
        (std::min)(width, height);
    if (!show_raster_edit_dialog(hwnd_, state)) return;
    if (state.clone_stamp_params.opacity_percent == 0.0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Opacity bằng 0 nên không thêm nét Clone Stamp"
            : "Opacity is zero; no Clone Stamp dab was added";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto id = next_raster_node_id(image->edit_graph);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, state.clone_stamp_params});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? "Đã thêm nét Clone Stamp · nguồn được chụp trước nét · Ctrl+Z hoàn tác"
            : "Clone Stamp dab added · source was frozen before the dab · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể thêm nét Clone Stamp" : "Could not add Clone Stamp dab");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::flip_selected_bitmap(bool horizontal) {
    if (!can_edit_selected_bitmap()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh có nguồn được giữ lại và không bị khóa để lật"
            : "Select one unlocked image with a preserved source to flip";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    if (!selected || !snapshot) return;
    const auto* object = snapshot->objects().lookup_ref(*selected);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    if (image->edit_graph.nodes.size() >= document::RasterEditGraph::kMaxNodes) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đạt giới hạn số bước sửa ảnh" : "The raster edit graph has reached its node limit";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    const auto id = next_raster_node_id(image->edit_graph);
    if (!id.valid()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn mã node ảnh khả dụng" : "No raster edit node ID is available";
        return;
    }
    document::RasterFlipParams flip;
    flip.horizontal = horizontal;
    flip.vertical = !horizontal;
    auto command = std::make_unique<document::SetRasterEditNodeCommand>(
        *selected, document::RasterEditNode{id, true, flip});
    const auto result = root().document().execute_command(std::move(command));
    status_msg_ = result.success
        ? (language_ == UILanguage::Vietnamese
            ? std::string("Đã lật ảnh ") + (horizontal ? "ngang" : "dọc") +
                " · đang dựng xem trước · Ctrl+Z hoàn tác"
            : std::string("Image flipped ") + (horizontal ? "horizontally" : "vertically") +
                " · preview is rendering · Ctrl+Z to undo")
        : (language_ == UILanguage::Vietnamese
            ? "Không thể cập nhật phép lật ảnh" : "Could not update the raster flip");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::handle_raster_preview_worker_message() {
    if (raster_preview_service_) {
        if (const auto diagnostic = raster_preview_service_->take_diagnostic()) {
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Không thể dựng xem trước ảnh: "
                : "Raster preview unavailable: ") + *diagnostic;
        }
    }
#ifdef _WIN32
    canvas_cache_valid_ = false;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

}  // namespace pikdraw::desktop
