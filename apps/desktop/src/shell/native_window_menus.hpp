#pragma once

#include "pikdraw/geometry/operations/align.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "native_window_document_color.hpp"

#include <array>
#include <optional>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace pikdraw::desktop {

enum class MenuId {
    None = -1,
    File = 0,
    Edit,
    View,
    Layout,
    Object,
    Effects,
    Bitmaps,
    Text,
    Tools,
    Window,
    Settings,
    Help,
    Count
};

enum class MenuActionId {
    None = 0,
    // File
    FileNew,
    FileOpen,
    FileImport,
    FileRecoverAutosave,
    FileSave,
    FileSaveAs,
    FileExport,
    FileExportRaster,
    FileTestPrint,
    FilePrintTestSheet,
    FileImportMeasurementRun,
    FileCompareMeasurementRuns,
    FileToggleGuidelines,
    FileClearGuidelines,
    FileProperties,
    FileExit,
    // Edit
    EditUndo,
    EditRedo,
    EditCut,
    EditCopy,
    EditPaste,
    EditPasteInPlace,
    EditPasteInFront,
    EditPasteInBack,
    EditDelete,
    EditDuplicate,
    EditDuplicateAtOrigin,
    EditStepAndRepeat,
    EditSelectAll,
    EditDeselect,
    EditSelectSameFillColor,
    EditSelectSameStrokeColor,
    EditSelectSameFillAndStroke,
    EditSelectSameColorSpace,
    EditSelectAllCmyk,
    EditSelectAllRgb,
    ColorConvertSelectionToCmyk,
    ColorConvertSelectionToRgb,
    ColorAssignSelectionProfile,
    ColorConvertSelectionProfile,
    ColorConvertActivePageToCmyk,
    ColorConvertActivePageToRgb,
    ColorConvertDocumentToCmyk,
    ColorConvertDocumentToRgb,
    ColorConvertActivePageProfile,
    ColorConvertDocumentProfile,
    // View
    ViewZoomIn,
    ViewZoomOut,
    ViewZoom100,
    ViewFitPage,
    ViewPageFocus,
    ViewArtboardOverview,
    ViewFitArtboards,
    ViewToggleGuidelines,
    ViewClearGuidelines,
    ViewSnapGrid,
    ViewSnapObjects,
    ViewProofColors,
    ViewGamutWarning,
    ViewOverprintPreview,
    // Layout
    LayoutPageSetup,
    LayoutPagePrevious,
    LayoutPageNext,
    LayoutPageAdd,
    LayoutPageDuplicate,
    LayoutPageDelete,
    LayoutArtboardAdd,
    LayoutArtboardDuplicate,
    LayoutArtboardDelete,
    LayoutArtboardExport,
    LayoutOrientationLandscape,
    LayoutOrientationPortrait,
    LayoutPageA4,
    LayoutPageA3,
    LayoutPageBanner,
    // Object
    ObjectGroup,
    ObjectUngroup,
    ObjectPowerClipMake,
    ObjectPowerClipRelease,
    ObjectPowerClipPlaceInside,
    ObjectPowerClipFitCenter,
    ObjectPowerClipFitProp,
    ObjectPowerClipFillProp,
    ObjectPowerClipStretch,
    ObjectPowerClipSelectContents,
    ObjectPowerClipEditFrame, ObjectPowerClipExtract, ObjectPowerClipCopyFrom,
    ObjectPowerClipEmpty, ObjectPowerClipRemoveFrame, ObjectPowerClipAppearance,
    ObjectProperties,
    ObjectConvertToCurves,
    ObjectAlignLeft,
    ObjectAlignCenter,
    ObjectAlignRight,
    ObjectAlignTop,
    ObjectAlignMiddle,
    ObjectAlignBottom,
    ObjectCenterPage,
    ObjectDistributeHorizontal,
    ObjectDistributeVertical,
    ObjectDistributeLeftEdges,
    ObjectDistributeCentersHorizontal,
    ObjectDistributeRightEdges,
    ObjectDistributeTopEdges,
    ObjectDistributeCentersVertical,
    ObjectDistributeBottomEdges,
    ObjectBooleanUnion,
    ObjectBooleanDifference,
    ObjectBooleanIntersection,
    ObjectBooleanExclude,
    ObjectBooleanReverseDifference,
    ObjectToggleKeepSource,
    ObjectToggleKeepTarget,
    ObjectFlipHorizontal,
    ObjectFlipVertical,
    ObjectRotate90CW,
    ObjectRotate90CCW,
    ObjectRotate180,
    ObjectFillGradient,
    ObjectPowerClipToggleLock,
    ObjectOrderToFront,
    ObjectOrderToBack,
    ObjectOrderForwardOne,
    ObjectOrderBackwardOne,
    ObjectOrderReverse,
    ObjectLock,
    ObjectUnlock,
    ObjectHide,
    ObjectIsolationEnter,
    ObjectIsolationExit,
    // Effects
    EffectsGaussianBlur,
    EffectsContour,
    EffectsBreakContourApart,
    EffectsDropShadow,
    EffectsExtrude,
    EffectsBevel,
    EffectsClear,
    EffectsAppearanceFocus,
    EffectsFeather,
    EffectsInnerShadow,
    EffectsOuterGlow,
    EffectsInnerGlow,
    EffectsColorOverlay,
    EffectsClearSelected,
    EffectsClearAll,
    // Bitmaps
    BitmapsConvert,
    BitmapsEdit,
    BitmapsExportMaster,
    BitmapsExportCmykMaster,
    BitmapsAssignProfile,
    BitmapsConvertProfileToSrgb,
    BitmapsCrop,
    BitmapsExposure,
    BitmapsLight,
    BitmapsColorBalance,
    BitmapsChannelMixer,
    BitmapsLocalExposureMask,
    BitmapsAdjustmentMask,
    BitmapsLevels,
    BitmapsHsl,
    BitmapsCurves,
    BitmapsWhiteBalance,
    BitmapsVibrance,
    BitmapsSelectiveHsl,
    BitmapsCloneStamp,
    BitmapsFlipHorizontal,
    BitmapsFlipVertical,
    BitmapsTrace,
    BitmapsColorMask,
    // Text
    TextEdit,
    TextFont,
    TextBold,
    TextItalic,
    TextUnderline,
    TextStrikethrough,
    TextAlignLeft,
    TextAlignCenter,
    TextAlignRight,
    TextAlignJustify,
    TextChangeCase,
    TextCaseSentence,
    TextCaseLower,
    TextCaseUpper,
    TextCaseTitle,
    TextCaseToggle,
    TextBullets,
    TextNumbering,
    TextNonPrinting,
    TextConvertArtisticParagraph,
    TextConvertToCurves,
    TextLinkFrames,
    TextUnlinkFrames,
    TextCombine,
    TextSplitLines,
    TextFitToPath,
    TextStraighten,
    TextReversePath,
    TextDocker,
    // Tools
    ToolPick,
    ToolNode,
    ToolRectangle,
    ToolEllipse,
    ToolText,
    ToolKnife,
    ToolLine,
    ToolPolyline,
    ToolPen,
    ToolPolygon,
    ToolGradient,
    ToolEyedropper,
    ToolArtboard,
    ToolCustomizeToolbox,
    // Window
    WindowProperties,
    WindowAlignDistributePanel,
    WindowTabs,
    WindowFullscreen,
    WindowResetWorkspace,
    // Settings
    SettingsPreferences,
    SettingsAiConnections,
    SettingsThemeDark,
    SettingsThemeLight,
    SettingsLangVi,
    SettingsLangEn,
    // Help
    HelpTopics,
    HelpShortcuts,
    HelpAiAutomation,
    HelpAbout,
    // Separator placeholder
    Separator,
    LayoutUnitMillimeter,
    LayoutUnitCentimeter,
    LayoutUnitMeter,
    LayoutUnitInch,
    LayoutUnitFoot,
    LayoutUnitYard,
    LayoutUnitPoint,
    LayoutUnitPica,
    EditSelectSameAnyPaintColor,
    ViewSnapSmartGuides,
    ViewSnapEqualSpacing,
    ViewSnapMaster,
    ViewSnapProperties,
    ViewSnapPage,
    ViewSnapGuides
};

struct MenuItem {
    MenuActionId action;
    const wchar_t* label_en;
    const wchar_t* label_vi;
    const wchar_t* shortcut;
    bool is_separator{false};
    bool is_disabled{false};
    bool is_heading{false};
    bool is_checkable{false};
    bool has_submenu{false};
};

enum class ObjectContextAction {
    AlignLeft,
    AlignCenterHorizontal,
    AlignRight,
    AlignTop,
    AlignCenterVertical,
    AlignBottom,
    CenterPage,
    DistributeHorizontal,
    DistributeVertical,
    Group,
    FlipHorizontal,
    FlipVertical,
    Rotate90CW,
    Rotate90CCW,
    BooleanUnion,
    BooleanDifference,
    BooleanIntersection,
    BooleanExclude,
    BooleanReverseDifference
};

struct ObjectContextButton {
    ObjectContextAction action;
    int left;
    int width{28};
    bool starts_group{false};
};

inline std::vector<ObjectContextButton> get_multi_object_context_buttons(
    bool can_align,
    bool can_distribute,
    bool can_group,
    const std::array<bool, 5>& boolean_actions,
    int max_right = std::numeric_limits<int>::max(),
    int start_x = 580
) {
    const bool has_boolean = std::any_of(boolean_actions.begin(), boolean_actions.end(), [](bool b) { return b; });
    if (!can_align && !can_distribute && !can_group && !has_boolean) {
        return {};
    }

    std::vector<ObjectContextButton> buttons;
    int x = start_x;
    auto add = [&](ObjectContextAction action, bool starts_group = false) {
        buttons.push_back({action, x, 28, starts_group});
        x += 32;
    };

    if (can_align) {
        add(ObjectContextAction::AlignLeft);
        add(ObjectContextAction::AlignCenterHorizontal);
        add(ObjectContextAction::AlignRight);
        add(ObjectContextAction::AlignTop);
        add(ObjectContextAction::AlignCenterVertical);
        add(ObjectContextAction::AlignBottom);
    }
    if (can_align) {
        x += 4;
        add(ObjectContextAction::CenterPage, true);
    }
    if (can_distribute) {
        x += 4;
        add(ObjectContextAction::DistributeHorizontal, true);
        add(ObjectContextAction::DistributeVertical);
    }
    if (can_group) {
        x += 4;
        add(ObjectContextAction::Group, true);
    }
    constexpr ObjectContextAction shape_actions[] = {
        ObjectContextAction::BooleanUnion,
        ObjectContextAction::BooleanDifference,
        ObjectContextAction::BooleanIntersection,
        ObjectContextAction::BooleanExclude,
        ObjectContextAction::BooleanReverseDifference
    };
    bool began_shaping = false;
    for (size_t index = 0; index < boolean_actions.size(); ++index) {
        if (!boolean_actions[index]) continue;
        const bool starts_group = !began_shaping;
        if (!began_shaping) {
            x += 4;
            began_shaping = true;
        }
        add(shape_actions[index], starts_group);
    }
    x += 4;
    add(ObjectContextAction::FlipHorizontal, true);
    add(ObjectContextAction::FlipVertical);
    add(ObjectContextAction::Rotate90CW);
    add(ObjectContextAction::Rotate90CCW);
    while (!buttons.empty() && buttons.back().left + buttons.back().width > max_right)
        buttons.pop_back();
    return buttons;
}

template <typename Workflow>
inline std::vector<ObjectContextButton> get_multi_object_context_buttons(
    const Workflow& workflow,
    bool can_group,
    int max_right = std::numeric_limits<int>::max(),
    int start_x = 580
) {
    return get_multi_object_context_buttons(
        workflow.can_align_selected(geometry::AlignMode::Left),
        workflow.can_distribute_selected(geometry::DistributeMode::SpacingHorizontal) &&
            workflow.can_distribute_selected(geometry::DistributeMode::SpacingVertical),
        can_group,
        {{
            workflow.can_boolean_operation_selected(geometry::BooleanOp::Union),
            workflow.can_boolean_operation_selected(geometry::BooleanOp::Difference),
            workflow.can_boolean_operation_selected(geometry::BooleanOp::Intersection),
            workflow.can_boolean_operation_selected(geometry::BooleanOp::Xor),
            workflow.can_boolean_operation_selected(geometry::BooleanOp::BackMinusFront)
        }},
        max_right,
        start_x
    );
}

struct MenuBarEntry {
    MenuId id;
    const wchar_t* label_en;
    const wchar_t* label_vi;
};

inline constexpr std::array<MenuBarEntry, 12> kMenuBarEntries{{
    {MenuId::File, L"File", L"Tệp"},
    {MenuId::Edit, L"Edit", L"Chỉnh sửa"},
    {MenuId::View, L"View", L"Hiển thị"},
    {MenuId::Layout, L"Layout", L"Bố cục"},
    {MenuId::Object, L"Object", L"Đối tượng"},
    {MenuId::Effects, L"Effects", L"Hiệu ứng"},
    {MenuId::Bitmaps, L"Bitmaps", L"Bitmap"},
    {MenuId::Text, L"Text", L"Văn bản"},
    {MenuId::Tools, L"Tools", L"Công cụ"},
    {MenuId::Window, L"Window", L"Cửa sổ"},
    {MenuId::Settings, L"Settings", L"Cài đặt"},
    {MenuId::Help, L"Help", L"Trợ giúp"}
}};

inline constexpr int kMenuRowHeight = 27;
inline constexpr int kMenuSeparatorHeight = 9;
inline constexpr int kMenuPadding = 4;
inline constexpr int kMenuPopupWidth = 300;

struct AlignPanelMetrics {
    static constexpr int dock_width = 280;
    static constexpr int content_left = 14;
    static constexpr int content_width = dock_width - 2 * content_left;
    static constexpr int section_tabs_top = 138;
    static constexpr int section_tabs_height = 24;
    static constexpr int reference_top = 185;
    static constexpr int reference_height = 26;
    static constexpr int align_top = 238;
    static constexpr int align_button_height = 26;
    static constexpr int align_row_gap = 6;
    static constexpr int horizontal_top = 340;
    static constexpr int vertical_top = 420;
    static constexpr int distribute_button_height = 26;
    static constexpr int distribute_row_gap = 4;
    static constexpr int gap_field_top = 501;
    static constexpr int gap_field_height = 26;
    static constexpr int apply_top = 535;
    static constexpr int apply_height = 26;
};

struct PageUnitPropertiesMetrics {
    static constexpr int row_top_in_content = 28;
    static constexpr int row_height = 28;
    static constexpr int menu_gap = 4;
    static constexpr int item_height = 24;
    static constexpr int menu_width = AlignPanelMetrics::content_width;
    static constexpr int field_left_in_content = 118;
};

// 1. File Menu
inline constexpr std::array<MenuItem, 15> kFileMenuItems{{
    {MenuActionId::FileNew, L"New Document", L"Bản vẽ mới", L"Ctrl+N"},
    {MenuActionId::FileOpen, L"Open…", L"Mở…", L"Ctrl+O"},
    {MenuActionId::FileImport, L"Import…", L"Nhập…", L"Ctrl+I"},
    {MenuActionId::FileRecoverAutosave, L"Recover Autosave…", L"Phục hồi bản tự lưu…", L""},
    {MenuActionId::FileSave, L"Save", L"Lưu", L"Ctrl+S"},
    {MenuActionId::FileSaveAs, L"Save As…", L"Lưu thành…", L"Ctrl+Shift+S"},
    {MenuActionId::FileExport, L"Export SVG…", L"Xuất SVG…", L"Ctrl+E"},
    {MenuActionId::FileExportRaster, L"Export Raster…", L"Xuất Raster…", L""},
    {MenuActionId::FileTestPrint, L"Export Test Sheet…", L"Xuất bảng in thử…", L""},
    {MenuActionId::FilePrintTestSheet, L"Print Test Sheet…", L"In bảng thử trực tiếp…", L""},
    {MenuActionId::FileImportMeasurementRun, L"Import Measurement Run…", L"Nhập số đo màu…", L""},
    {MenuActionId::FileCompareMeasurementRuns, L"Compare Measurement Runs…", L"So sánh số đo màu…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::FileProperties, L"Document Properties…", L"Thuộc tính tài liệu…", L"", false, true},
    {MenuActionId::FileExit, L"Exit PikDraw", L"Thoát PikDraw", L"Alt+F4"}
}};

// 2. Edit Menu
inline constexpr std::array<MenuItem, 39> kEditMenuItems{{
    {MenuActionId::EditUndo, L"Undo", L"Hoàn tác", L"Ctrl+Z"},
    {MenuActionId::EditRedo, L"Redo", L"Làm lại", L"Ctrl+Y"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EditCut, L"Cut", L"Cắt", L"Ctrl+X"},
    {MenuActionId::EditCopy, L"Copy", L"Sao chép", L"Ctrl+C"},
    {MenuActionId::EditPaste, L"Paste", L"Dán", L"Ctrl+V"},
    {MenuActionId::EditPasteInPlace, L"Paste in Place", L"Dán tại chỗ", L"Ctrl+Shift+V"},
    {MenuActionId::EditPasteInFront, L"Paste in Front", L"Dán lên trước", L"Ctrl+F"},
    {MenuActionId::EditPasteInBack, L"Paste in Back", L"Dán ra sau", L"Ctrl+B"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EditDelete, L"Delete", L"Xóa", L"Del"},
    {MenuActionId::EditDuplicate, L"Duplicate", L"Nhân bản", L"Ctrl+D"},
    {MenuActionId::EditDuplicateAtOrigin, L"Duplicate at Origin", L"Nhân bản tại chỗ", L"+"},
    {MenuActionId::EditStepAndRepeat, L"Step and Repeat…", L"Nhân bản lặp lại…", L"Ctrl+Shift+D"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EditSelectAll, L"Select All", L"Chọn tất cả", L"Ctrl+A"},
    {MenuActionId::EditDeselect, L"Deselect", L"Bỏ chọn", L"Esc"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EditSelectSameFillColor, L"Select Same Fill Color", L"Chọn cùng màu tô", L""},
    {MenuActionId::EditSelectSameStrokeColor, L"Select Same Stroke Color", L"Chọn cùng màu viền", L""},
    {MenuActionId::EditSelectSameFillAndStroke, L"Select Same Fill & Stroke", L"Chọn cùng màu tô & viền", L""},
    {MenuActionId::EditSelectSameAnyPaintColor, L"Select Same Fill or Stroke Color", L"Chọn cùng màu tô hoặc viền", L""},
    {MenuActionId::EditSelectSameColorSpace, L"Select Same Color Space", L"Chọn cùng hệ màu", L""},
    {MenuActionId::EditSelectAllCmyk, L"Select All CMYK Objects", L"Chọn tất cả đối tượng CMYK", L""},
    {MenuActionId::EditSelectAllRgb, L"Select All RGB Objects", L"Chọn tất cả đối tượng RGB", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ColorConvertSelectionToCmyk, L"Convert Selection to CMYK", L"Chuyển đối tượng chọn sang CMYK", L""},
    {MenuActionId::ColorConvertSelectionToRgb, L"Convert Selection to RGB", L"Chuyển đối tượng chọn sang RGB", L""},
    {MenuActionId::ColorAssignSelectionProfile, L"Assign ICC to Selection…", L"Gán ICC cho đối tượng chọn…", L""},
    {MenuActionId::ColorConvertSelectionProfile, L"Convert Selection ICC…", L"Chuyển ICC màu đối tượng chọn…", L""},
    {MenuActionId::ColorConvertActivePageToCmyk, L"Convert Active Page to CMYK", L"Chuyển trang hiện tại sang CMYK", L""},
    {MenuActionId::ColorConvertActivePageToRgb, L"Convert Active Page to RGB", L"Chuyển trang hiện tại sang RGB", L""},
    {MenuActionId::ColorConvertActivePageProfile, L"Convert Active Page ICC Profile…", L"Chuyển profile ICC trang hiện tại…", L""},
    {MenuActionId::ColorConvertDocumentProfile, L"Convert Document ICC Profile…", L"Chuyển profile ICC toàn tài liệu…", L""},
    {MenuActionId::ColorConvertDocumentToCmyk, L"Convert Document to CMYK", L"Chuyển toàn bộ file sang CMYK", L""},
    {MenuActionId::ColorConvertDocumentToRgb, L"Convert Document to RGB", L"Chuyển toàn bộ file sang RGB", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {static_cast<MenuActionId>(document_color_ui::kModeMenu), L"Document Color Mode", L"Hệ màu tài liệu", L"▸"},
    {static_cast<MenuActionId>(document_color_ui::kDpiMenu), L"Raster Effects DPI", L"DPI hiệu ứng raster", L"▸"}
}};

// 3. View Menu
inline constexpr std::array<MenuItem, 23> kViewMenuItems{{
    {MenuActionId::ViewZoomIn, L"Zoom In", L"Phóng to", L""},
    {MenuActionId::ViewZoomOut, L"Zoom Out", L"Thu nhỏ", L""},
    {MenuActionId::ViewZoom100, L"Zoom 100%", L"Tỷ lệ 100%", L"1"},
    {MenuActionId::ViewFitPage, L"Fit Page to Window", L"Vừa khung nhìn", L"F"},
    {MenuActionId::ViewPageFocus, L"Page Focus", L"Tập trung theo trang", L"", false, false, false, true},
    {MenuActionId::ViewArtboardOverview, L"Artboard Overview", L"Tổng quan Artboard", L"", false, false, false, true},
    {MenuActionId::ViewFitArtboards, L"Fit All Artboards", L"Vừa tất cả Artboard", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ViewToggleGuidelines, L"Toggle Guidelines", L"Ẩn/Hiện đường gióng", L"Ctrl+;"},
    {MenuActionId::ViewClearGuidelines, L"Clear All Guidelines", L"Xóa hết đường gióng", L"Ctrl+Alt+;"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ViewSnapGrid, L"Snap to Grid", L"Hít vào lưới", L"", false, false, false, true},
    {MenuActionId::ViewSnapObjects, L"Snap to Objects", L"Hít vào đối tượng", L"", false, false, false, true},
    {MenuActionId::ViewSnapSmartGuides, L"Smart Guides", L"Đường gióng thông minh", L"", false, false, false, true},
    {MenuActionId::ViewSnapEqualSpacing, L"Equal Spacing", L"Khoảng cách bằng nhau", L"", false, false, false, true},
    {MenuActionId::ViewSnapPage, L"Snap to Page / Artboard", L"Hít vào trang / Artboard", L"", false, false, false, true},
    {MenuActionId::ViewSnapGuides, L"Snap to Guidelines", L"Hít vào đường gióng", L"", false, false, false, true},
    {MenuActionId::ViewSnapMaster, L"Snap Off / On", L"Tắt / Bật bắt điểm", L"", false, false, false, true},
    {MenuActionId::ViewSnapProperties, L"Snap Properties…", L"Thuộc tính Snap…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ViewProofColors, L"Proof Colors", L"Mô phỏng màu in", L"Ctrl+Alt+Y", false, false, false, true},
    {MenuActionId::ViewGamutWarning, L"Gamut Warning (ICC)", L"Cảnh báo ngoài gamut (ICC)", L"Ctrl+Shift+Y", false, false, false, true},
    {MenuActionId::ViewOverprintPreview, L"Overprint Preview (Unavailable)", L"Xem trước in đè (chưa khả dụng)", L"Alt+Shift+Ctrl+Y", false, false, false, true}
}};

// 4. Layout Menu
inline constexpr std::array<MenuItem, 29> kLayoutMenuItems{{
    {MenuActionId::LayoutPageSetup, L"Page Setup…", L"Thiết lập trang…", L"", false, true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::LayoutPagePrevious, L"Previous Page", L"Trang trước", L"Alt+PageUp"},
    {MenuActionId::LayoutPageNext, L"Next Page", L"Trang sau", L"Alt+PageDown"},
    {MenuActionId::LayoutPageAdd, L"Add Page", L"Thêm trang", L""},
    {MenuActionId::LayoutPageDuplicate, L"Duplicate Page", L"Nhân đôi trang", L""},
    {MenuActionId::LayoutPageDelete, L"Delete Page", L"Xóa trang", L""},
    {MenuActionId::LayoutArtboardAdd, L"Add Artboard", L"Thêm Artboard", L""},
    {MenuActionId::LayoutArtboardDuplicate, L"Duplicate Artboard", L"Nhân đôi Artboard", L""},
    {MenuActionId::LayoutArtboardDelete, L"Delete Artboard", L"Xóa Artboard", L""},
    {MenuActionId::LayoutArtboardExport, L"Export Active Artboard…", L"Xuất Artboard đang chọn…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::LayoutOrientationLandscape, L"Landscape Orientation", L"Khổ ngang", L""},
    {MenuActionId::LayoutOrientationPortrait, L"Portrait Orientation", L"Khổ dọc", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::LayoutPageA4, L"A4 (210 × 297 mm)", L"Khổ A4 (210 × 297 mm)", L""},
    {MenuActionId::LayoutPageA3, L"A3 (297 × 420 mm)", L"Khổ A3 (297 × 420 mm)", L""},
    {MenuActionId::LayoutPageBanner, L"Banner 3 × 1 m", L"Băng rôn 3 × 1 m", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::None, L"PAGE DISPLAY UNITS", L"ĐƠN VỊ HIỂN THỊ TRANG", L"", false, true, true},
    {MenuActionId::LayoutUnitMillimeter, L"Millimeters (mm)", L"Milimét (mm)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitCentimeter, L"Centimeters (cm)", L"Xentimét (cm)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitMeter, L"Meters (m)", L"Mét (m)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitInch, L"Inches (in)", L"Inch (in)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitFoot, L"Feet (ft)", L"Feet (ft)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitYard, L"Yards (yd)", L"Yard (yd)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitPoint, L"Points (pt)", L"Điểm in (pt)", L"", false, false, false, true},
    {MenuActionId::LayoutUnitPica, L"Picas (pc)", L"Pica (pc)", L"", false, false, false, true}
}};

// 5. Object Menu
inline constexpr std::array<MenuItem, 58> kObjectMenuItems{{
    {MenuActionId::ObjectGroup, L"Group Objects", L"Nhóm đối tượng", L"Ctrl+G"},
    {MenuActionId::ObjectUngroup, L"Ungroup", L"Rã nhóm", L"Ctrl+Shift+G"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::None, L"POWERCLIP / CLIPPING MASK", L"LỒNG KHUNG / MẶT NẠ CẮT", L"", false, true, true},
    {MenuActionId::ObjectIsolationEnter, L"Edit Contents", L"Sửa nội dung", L"Enter"},
    {MenuActionId::ObjectIsolationExit, L"Finish Editing", L"Kết thúc sửa", L"Esc"},
    {MenuActionId::ObjectPowerClipSelectContents, L"Select Contents", L"Chọn nội dung", L""},
    {MenuActionId::ObjectPowerClipEditFrame, L"Edit Frame", L"Sửa khung", L""},
    {MenuActionId::ObjectPowerClipExtract, L"Extract Contents", L"Trích nội dung", L""},
    {MenuActionId::ObjectPowerClipCopyFrom, L"Copy Contents From...", L"Chép nội dung từ...", L""},
    {MenuActionId::ObjectPowerClipEmpty, L"Create Empty Frame", L"Tạo khung rỗng", L""},
    {MenuActionId::ObjectPowerClipRemoveFrame, L"Convert to Regular Object", L"Chuyển thành đối tượng thường", L""},
    {MenuActionId::ObjectPowerClipAppearance, L"Keep Frame Fill/Stroke", L"Giữ màu/nét khung", L""},
    {MenuActionId::ObjectPowerClipMake, L"Make Clipping Mask", L"Tạo mặt nạ cắt", L"Ctrl+7"},
    {MenuActionId::ObjectPowerClipRelease, L"Release Clipping Mask", L"Hủy mặt nạ cắt", L"Ctrl+Alt+7"},
    {MenuActionId::ObjectPowerClipPlaceInside, L"Place Inside Frame…", L"Đặt vào trong khung…", L""},
    {MenuActionId::ObjectPowerClipFitCenter, L"Fit: Center Contents", L"Căn giữa nội dung", L""},
    {MenuActionId::ObjectPowerClipFitProp, L"Fit: Fit Proportionally", L"Vừa khít giữ tỉ lệ", L""},
    {MenuActionId::ObjectPowerClipFillProp, L"Fit: Fill Proportionally", L"Phủ đầy khung", L""},
    {MenuActionId::ObjectPowerClipStretch, L"Fit: Stretch to Fill", L"Kéo giãn vừa khung", L""},
    {MenuActionId::ObjectProperties, L"Properties…", L"Thuộc tính…", L""},
    {MenuActionId::ObjectFillGradient, L"Gradient Fill…", L"Tô chuyển sắc…", L"G"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::None, L"TRANSFORM", L"BIẾN ĐỔI", L"", false, true, true},
    {MenuActionId::ObjectFlipHorizontal, L"Flip Horizontal", L"Lật ngang", L"Ctrl+Shift+H"},
    {MenuActionId::ObjectFlipVertical, L"Flip Vertical", L"Lật dọc", L"Ctrl+Shift+V"},
    {MenuActionId::ObjectRotate90CW, L"Rotate 90° CW", L"Xoay 90° thuận chiều kim đồng hồ", L"Ctrl+]"},
    {MenuActionId::ObjectRotate90CCW, L"Rotate 90° CCW", L"Xoay 90° ngược chiều kim đồng hồ", L"Ctrl+["},
    {MenuActionId::ObjectRotate180, L"Rotate 180°", L"Xoay 180°", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ObjectConvertToCurves, L"Convert to Curves", L"Chuyển thành đường cong", L"Ctrl+Q"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::None, L"ALIGN & DISTRIBUTE", L"CĂN CHỈNH & PHÂN BỔ", L"", false, true, true},
    {MenuActionId::ObjectAlignLeft, L"Align Left", L"Căn lề trái", L""},
    {MenuActionId::ObjectAlignCenter, L"Align Center Horizontally", L"Căn giữa ngang", L""},
    {MenuActionId::ObjectAlignRight, L"Align Right", L"Căn lề phải", L""},
    {MenuActionId::ObjectAlignTop, L"Align Top", L"Căn đỉnh", L""},
    {MenuActionId::ObjectAlignMiddle, L"Align Center Vertically", L"Căn giữa dọc", L""},
    {MenuActionId::ObjectAlignBottom, L"Align Bottom", L"Căn đáy", L""},
    {MenuActionId::ObjectCenterPage, L"Center to Page", L"Căn giữa trang", L""},
    {MenuActionId::ObjectDistributeHorizontal, L"Distribute Horizontal Spacing", L"Phân bổ khoảng cách ngang", L""},
    {MenuActionId::ObjectDistributeVertical, L"Distribute Vertical Spacing", L"Phân bổ khoảng cách dọc", L""},
    {MenuActionId::ObjectDistributeCentersHorizontal, L"Distribute Horizontal Centers", L"Phân bổ tâm ngang", L""},
    {MenuActionId::ObjectDistributeLeftEdges, L"Distribute Left Edges", L"Phân bổ cạnh trái", L""},
    {MenuActionId::ObjectDistributeRightEdges, L"Distribute Right Edges", L"Phân bổ cạnh phải", L""},
    {MenuActionId::ObjectDistributeCentersVertical, L"Distribute Vertical Centers", L"Phân bổ tâm dọc", L""},
    {MenuActionId::ObjectDistributeTopEdges, L"Distribute Top Edges", L"Phân bổ cạnh trên", L""},
    {MenuActionId::ObjectDistributeBottomEdges, L"Distribute Bottom Edges", L"Phân bổ cạnh dưới", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::None, L"SHAPING", L"TẠO HÌNH", L"", false, true, true},
    {MenuActionId::ObjectBooleanUnion, L"Weld / Union", L"Hàn / Hợp nhất", L""},
    {MenuActionId::ObjectBooleanDifference, L"Difference (Target − Source)", L"Hiệu (Đích − Nguồn)", L""},
    {MenuActionId::ObjectBooleanIntersection, L"Intersect", L"Lấy phần giao", L""},
    {MenuActionId::ObjectBooleanExclude, L"Exclude (XOR)", L"Loại trừ (XOR)", L""},
    {MenuActionId::ObjectBooleanReverseDifference, L"Reverse Difference (Source − Target)", L"Hiệu ngược (Nguồn − Đích)", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ObjectToggleKeepSource, L"Keep Source", L"Giữ đối tượng nguồn", L"", false, false, false, true},
    {MenuActionId::ObjectToggleKeepTarget, L"Keep Target", L"Giữ đối tượng đích", L"", false, false, false, true}
}};

// 6. Effects Menu
inline constexpr std::array<MenuItem, 15> kEffectsMenuItems{{
    {MenuActionId::EffectsAppearanceFocus, L"Appearance Properties…", L"Thuộc tính diện mạo…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EffectsGaussianBlur, L"Gaussian Blur…", L"Làm mờ Gaussian…", L""},
    {MenuActionId::EffectsFeather, L"Feather…", L"Làm mềm biên (Feather)…", L""},
    {MenuActionId::EffectsDropShadow, L"Drop Shadow…", L"Đổ bóng (Drop Shadow)…", L""},
    {MenuActionId::EffectsInnerShadow, L"Inner Shadow…", L"Bóng trong (Inner Shadow)…", L""},
    {MenuActionId::EffectsOuterGlow, L"Outer Glow…", L"Phát sáng ngoài (Outer Glow)…", L""},
    {MenuActionId::EffectsInnerGlow, L"Inner Glow…", L"Phát sáng trong (Inner Glow)…", L""},
    {MenuActionId::EffectsColorOverlay, L"Color Overlay…", L"Phủ màu (Color Overlay)…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EffectsContour, L"Contour", L"Tạo viền Contour", L"Ctrl+F9", false, false},
    {MenuActionId::EffectsBreakContourApart, L"Break Contour Apart", L"Rã khối viền Contour", L"Ctrl+K", false, false},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EffectsClear, L"Clear Live Effects", L"Xóa hiệu ứng diện mạo", L"", false, false},
    {MenuActionId::EffectsClearAll, L"Clear All Effects…", L"Xóa tất cả hiệu ứng (kèm Contour)…", L"", false, false}
}};

// 7. Bitmaps Menu
inline constexpr std::array<MenuItem, 24> kBitmapsMenuItems{{
    {MenuActionId::BitmapsConvert, L"Convert to Bitmap…", L"Chuyển sang ảnh Bitmap…", L"", false, false},
    {MenuActionId::BitmapsExportMaster, L"Export Bitmap Master (TIFF)…", L"Xuất bản gốc ảnh (TIFF)…", L"", false, false},
    {MenuActionId::BitmapsExportCmykMaster, L"Export Bitmap Master to CMYK16 TIFF…", L"Xuất ảnh gốc thành TIFF CMYK16…", L"", false, false},
    {MenuActionId::BitmapsAssignProfile, L"Assign Bitmap Profile…", L"Gán profile ảnh…", L"", false, false},
    {MenuActionId::BitmapsConvertProfileToSrgb, L"Convert Bitmap Profile to sRGB", L"Chuyển profile ảnh sang sRGB", L"", false, false},
    {MenuActionId::BitmapsCrop, L"Crop…", L"Cắt ảnh…", L"", false, false},
    {MenuActionId::BitmapsExposure, L"Exposure…", L"Phơi sáng…", L"", false, false},
    {MenuActionId::BitmapsLight, L"Light…", L"Ánh sáng…", L"", false, false},
    {MenuActionId::BitmapsColorBalance, L"Color Balance…", L"Cân bằng màu…", L"", false, false},
    {MenuActionId::BitmapsChannelMixer, L"Channel Mixer…", L"Bộ trộn kênh…", L"", false, false},
    {MenuActionId::BitmapsLocalExposureMask, L"Local Exposure Mask…", L"Mặt nạ phơi sáng cục bộ…", L"", false, false},
    {MenuActionId::BitmapsAdjustmentMask, L"Edit Last Adjustment Mask…", L"Sửa mặt nạ chỉnh màu gần nhất…", L"", false, false},
    {MenuActionId::BitmapsLevels, L"Levels…", L"Levels…", L"", false, false},
    {MenuActionId::BitmapsHsl, L"Hue/Saturation/Lightness…", L"Sắc màu/Bão hòa/Độ sáng HSL…", L"", false, false},
    {MenuActionId::BitmapsCurves, L"Curves…", L"Đường cong…", L"", false, false},
    {MenuActionId::BitmapsWhiteBalance, L"White Balance…", L"Cân bằng trắng…", L"", false, false},
    {MenuActionId::BitmapsVibrance, L"Vibrance…", L"Tăng cường màu…", L"", false, false},
    {MenuActionId::BitmapsSelectiveHsl, L"Selective HSL…", L"HSL theo dải màu…", L"", false, false},
    {MenuActionId::BitmapsCloneStamp, L"Clone Stamp Dab…", L"Đóng dấu Clone…", L"", false, false},
    {MenuActionId::BitmapsFlipHorizontal, L"Flip Horizontal", L"Lật ngang", L"", false, false},
    {MenuActionId::BitmapsFlipVertical, L"Flip Vertical", L"Lật dọc", L"", false, false},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::BitmapsTrace, L"Quick Vectorize / Trace…", L"Vector hóa ảnh (Trace)…", L"", false, true},
    {MenuActionId::BitmapsColorMask, L"Bitmap Color Mask…", L"Mặt nạ màu ảnh…", L"", false, true}
}};

// Character and paragraph values live in the contextual bar and Properties.
inline constexpr std::array<MenuItem, 19> kTextMenuItems{{
    {MenuActionId::TextEdit, L"Edit Text…", L"Sửa văn bản…", L"Ctrl+Shift+T"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::TextChangeCase, L"Change Case", L"Đổi hoa / thường", L"Shift+F3", false, false, false, false, true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::TextBullets, L"Bullets", L"Đầu dòng dấu chấm", L""},
    {MenuActionId::TextNumbering, L"Numbering", L"Đầu dòng số thứ tự", L""},
    {MenuActionId::TextNonPrinting, L"Show Non-Printing Characters", L"Hiện ký tự ẩn", L"Ctrl+Shift+8"},
    {MenuActionId::TextConvertArtisticParagraph, L"Convert to Paragraph / Artistic Text", L"Đổi Văn bản Khung / Tự do", L"Ctrl+F8"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::TextLinkFrames, L"Link Text Frames", L"Nối luồng văn bản tràn", L""},
    {MenuActionId::TextUnlinkFrames, L"Unlink Text Frame", L"Ngắt liên kết luồng văn bản", L""},
    {MenuActionId::TextCombine, L"Combine Text Objects", L"Gộp các đoạn văn bản", L"Ctrl+Shift+C"},
    {MenuActionId::TextSplitLines, L"Split Text by Lines", L"Tách văn bản theo từng dòng", L"Ctrl+Shift+S"},
    {MenuActionId::TextFitToPath, L"Fit Text to Path", L"Uốn chữ theo đường dẫn", L""},
    {MenuActionId::TextStraighten, L"Straighten Text", L"Hủy uốn chữ (Thẳng hàng)", L""},
    {MenuActionId::TextReversePath, L"Reverse Text Direction", L"Đảo chiều uốn chữ", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::TextConvertToCurves, L"Convert Text to Curves", L"Chuyển chữ thành đường cong", L"Ctrl+Q"},
    {MenuActionId::TextDocker, L"Text Docker / Properties", L"Bảng điều khiển chữ (Docker)", L"Ctrl+T"}
}};

inline constexpr std::array<MenuItem, 5> kTextCaseSubmenuItems{{
    {MenuActionId::TextCaseSentence, L"Sentence case", L"Viết hoa đầu câu", L""},
    {MenuActionId::TextCaseLower, L"lowercase", L"Chữ thường", L""},
    {MenuActionId::TextCaseUpper, L"UPPERCASE", L"CHỮ HOA", L""},
    {MenuActionId::TextCaseTitle, L"Title Case", L"Viết Hoa Từng Từ", L""},
    {MenuActionId::TextCaseToggle, L"tOGGLE cASE", L"Đảo hoa / thường", L""}
}};

inline std::span<const MenuItem> get_menu_submenu_items(MenuActionId parent) noexcept {
    return parent == MenuActionId::TextChangeCase
        ? std::span<const MenuItem>(kTextCaseSubmenuItems)
        : std::span<const MenuItem>{};
}

// 9. Tools Menu
inline constexpr std::array<MenuItem, 13> kToolsMenuItems{{
    {MenuActionId::ToolPick, L"Pick Tool (Selection)", L"Công cụ chọn (Pick)", L"V"},
    {MenuActionId::ToolNode, L"Node Edit Tool", L"Chỉnh sửa điểm (Node)", L""},
    {MenuActionId::ToolRectangle, L"Rectangle Tool", L"Vẽ hình chữ nhật", L"R"},
    {MenuActionId::ToolEllipse, L"Ellipse Tool", L"Vẽ hình elip / tròn", L"E"},
    {MenuActionId::ToolPolygon, L"Polygon Tool", L"Vẽ đa giác đều", L""},
    {MenuActionId::ToolText, L"Text Tool", L"Nhập văn bản", L"A / T"},
    {MenuActionId::ToolKnife, L"Knife Cutting Tool", L"Dao cắt hình", L"K"},
    {MenuActionId::ToolLine, L"Line Tool", L"Đoạn thẳng", L"L"},
    {MenuActionId::ToolPolyline, L"Polyline Tool", L"Đường gấp khúc", L"Shift+L"},
    {MenuActionId::ToolPen, L"Pen Tool", L"Công cụ Pen", L""},
    {MenuActionId::ToolGradient, L"Interactive Gradient Tool", L"Công cụ chuyển sắc tương tác", L"G"},
    {MenuActionId::ToolEyedropper, L"Color Eyedropper", L"Ống hút màu", L"I"},
    {MenuActionId::ToolArtboard, L"Artboard Tool", L"Công cụ Artboard", L"Shift+O"}
}};

// 10. Window Menu
inline constexpr std::array<MenuItem, 6> kWindowMenuItems{{
    {MenuActionId::WindowProperties, L"Properties Inspector", L"Bảng thuộc tính", L"Ctrl+T"},
    {MenuActionId::WindowAlignDistributePanel, L"Align & Distribute Panel", L"Bảng Căn chỉnh & Phân bổ", L"Ctrl+Shift+A"},
    {MenuActionId::WindowTabs, L"Document Tabs Bar", L"Thanh tab tài liệu", L"", false, true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::WindowFullscreen, L"Toggle Fullscreen", L"Bật/Tắt toàn màn hình", L"", false, true},
    {MenuActionId::WindowResetWorkspace, L"Reset Workspace Layout", L"Khôi phục bố cục mặc định", L"", false, true}
}};

// 11. Settings Menu
inline constexpr std::array<MenuItem, 8> kSettingsMenuItems{{
    {MenuActionId::SettingsPreferences, L"Preferences…", L"Cài đặt hệ thống…", L"Ctrl+J"},
    {MenuActionId::SettingsAiConnections, L"AI Connections…", L"Kết nối AI…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::SettingsThemeDark, L"Theme: Cyber Dark", L"Giao diện: Tối Obsidian", L""},
    {MenuActionId::SettingsThemeLight, L"Theme: Silver Light", L"Giao diện: Sáng Silver", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::SettingsLangVi, L"Language: Vietnamese", L"Ngôn ngữ: Tiếng Việt", L""},
    {MenuActionId::SettingsLangEn, L"Language: English", L"Ngôn ngữ: English", L""}
}};

// 12. Help Menu
inline constexpr std::array<MenuItem, 5> kHelpMenuItems{{
    {MenuActionId::HelpTopics, L"PikDraw Pro Help", L"Hướng dẫn sử dụng PikDraw", L"", false, true},
    {MenuActionId::HelpShortcuts, L"Keyboard Shortcuts Reference", L"Bảng tra cứu phím tắt", L"", false, true},
    {MenuActionId::HelpAiAutomation, L"AI Automation…", L"Tự động hóa bằng AI…", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::HelpAbout, L"About PikDraw Pro…", L"Giới thiệu PikDraw Pro…", L"", false, true}
}};

inline std::span<const MenuItem> get_menu_items(MenuId id) noexcept {
    switch (id) {
    case MenuId::File: return kFileMenuItems;
    case MenuId::Edit: return kEditMenuItems;
    case MenuId::View: return kViewMenuItems;
    case MenuId::Layout: return kLayoutMenuItems;
    case MenuId::Object: return kObjectMenuItems;
    case MenuId::Effects: return kEffectsMenuItems;
    case MenuId::Bitmaps: return kBitmapsMenuItems;
    case MenuId::Text: return kTextMenuItems;
    case MenuId::Tools: return kToolsMenuItems;
    case MenuId::Window: return kWindowMenuItems;
    case MenuId::Settings: return kSettingsMenuItems;
    case MenuId::Help: return kHelpMenuItems;
    default: return {};
    }
}

inline int get_menu_height(std::span<const MenuItem> items) noexcept {
    if (items.empty()) return 0;
    int h = 2 * kMenuPadding;
    for (const auto& item : items) {
        h += item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
    }
    return h;
}

inline int get_menu_height(MenuId id) noexcept {
    return get_menu_height(get_menu_items(id));
}

inline int get_menu_item_top(std::span<const MenuItem> items, MenuActionId action) noexcept {
    int y = kMenuPadding;
    for (const auto& item : items) {
        const int row_height = item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
        if (!item.is_separator && item.action == action) return y;
        y += row_height;
    }
    return -1;
}

inline int get_menu_item_top(MenuId id, MenuActionId action) noexcept {
    return get_menu_item_top(get_menu_items(id), action);
}

inline const MenuItem* get_menu_item_at(std::span<const MenuItem> items, int offset_y) noexcept {
    int y = kMenuPadding;
    for (const auto& item : items) {
        int rh = item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
        if (offset_y >= y && offset_y < y + rh) {
            return item.is_separator ? nullptr : &item;
        }
        y += rh;
    }
    return nullptr;
}

inline const MenuItem* get_menu_item_at(MenuId id, int offset_y) noexcept {
    return get_menu_item_at(get_menu_items(id), offset_y);
}

inline std::optional<MenuActionId> get_menu_action_at(MenuId id, int offset_y) noexcept {
    const auto* item = get_menu_item_at(id, offset_y);
    if (!item || item->is_disabled || item->has_submenu) return std::nullopt;
    return item->action;
}

template <typename IsEnabled>
inline std::optional<MenuActionId> get_adjacent_menu_action(
    std::span<const MenuItem> items, int current_action, int direction,
    IsEnabled&& is_enabled) noexcept {
    if (items.empty()) return std::nullopt;
    int current_index = -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (static_cast<int>(items[static_cast<size_t>(i)].action) == current_action) {
            current_index = i;
            break;
        }
    }
    const int step = direction < 0 ? -1 : 1;
    int index = current_index < 0 ? (step > 0 ? 0 : static_cast<int>(items.size()) - 1)
                                 : current_index + step;
    for (int visited = 0; visited < static_cast<int>(items.size()); ++visited) {
        if (index < 0) index = static_cast<int>(items.size()) - 1;
        if (index >= static_cast<int>(items.size())) index = 0;
        const auto& item = items[static_cast<size_t>(index)];
        if (!item.is_separator && !item.is_disabled && is_enabled(item.action)) return item.action;
        index += step;
    }
    return std::nullopt;
}

template <typename IsEnabled>
inline std::optional<MenuActionId> get_adjacent_menu_action(
    MenuId id, int current_action, int direction, IsEnabled&& is_enabled) noexcept {
    return get_adjacent_menu_action(get_menu_items(id), current_action, direction,
                                    std::forward<IsEnabled>(is_enabled));
}

inline std::optional<MenuActionId> get_adjacent_menu_action(
    std::span<const MenuItem> items, int current_action, int direction) noexcept {
    return get_adjacent_menu_action(items, current_action, direction, [](MenuActionId) { return true; });
}

inline std::optional<MenuActionId> get_adjacent_menu_action(
    MenuId id, int current_action, int direction) noexcept {
    return get_adjacent_menu_action(get_menu_items(id), current_action, direction,
                                    [](MenuActionId) { return true; });
}

} // namespace pikdraw::desktop
