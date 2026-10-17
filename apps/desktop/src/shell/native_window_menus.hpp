#pragma once

#include "pikdraw/geometry/operations/align.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"

#include <array>
#include <optional>
#include <limits>
#include <span>
#include <string_view>
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
    FileSave,
    FileSaveAs,
    FileExportSvg,
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
    // View
    ViewZoomIn,
    ViewZoomOut,
    ViewZoom100,
    ViewFitPage,
    ViewToggleGuidelines,
    ViewClearGuidelines,
    ViewSnapGrid,
    ViewSnapObjects,
    // Layout
    LayoutPageSetup,
    LayoutOrientationLandscape,
    LayoutOrientationPortrait,
    LayoutPageA4,
    LayoutPageA3,
    LayoutPageBanner,
    // Object
    ObjectGroup,
    ObjectUngroup,
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
    // Effects
    EffectsGaussianBlur,
    EffectsContour,
    EffectsDropShadow,
    EffectsExtrude,
    EffectsBevel,
    EffectsClear,
    // Bitmaps
    BitmapsConvert,
    BitmapsEdit,
    BitmapsTrace,
    BitmapsColorMask,
    // Text
    TextEdit,
    TextBold,
    TextItalic,
    TextConvertToCurves,
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
    Separator
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

// 1. File Menu
inline constexpr std::array<MenuItem, 8> kFileMenuItems{{
    {MenuActionId::FileNew, L"New Document", L"Bản vẽ mới", L"Ctrl+N"},
    {MenuActionId::FileOpen, L"Open PikDraw File…", L"Mở tệp PikDraw…", L"Ctrl+O"},
    {MenuActionId::FileSave, L"Save", L"Lưu", L"Ctrl+S"},
    {MenuActionId::FileSaveAs, L"Save As…", L"Lưu thành…", L"Ctrl+Shift+S"},
    {MenuActionId::FileExportSvg, L"Export SVG…", L"Xuất SVG…", L"Ctrl+E"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::FileProperties, L"Document Properties…", L"Thuộc tính tài liệu…", L"", false, true},
    {MenuActionId::FileExit, L"Exit PikDraw", L"Thoát PikDraw", L"Alt+F4"}
}};

// 2. Edit Menu
inline constexpr std::array<MenuItem, 17> kEditMenuItems{{
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
    {MenuActionId::EditDeselect, L"Deselect", L"Bỏ chọn", L"Esc"}
}};

// 3. View Menu
inline constexpr std::array<MenuItem, 10> kViewMenuItems{{
    {MenuActionId::ViewZoomIn, L"Zoom In", L"Phóng to", L""},
    {MenuActionId::ViewZoomOut, L"Zoom Out", L"Thu nhỏ", L""},
    {MenuActionId::ViewZoom100, L"Zoom 100%", L"Tỷ lệ 100%", L"1"},
    {MenuActionId::ViewFitPage, L"Fit Page to Window", L"Vừa khung nhìn", L"F"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ViewToggleGuidelines, L"Toggle Guidelines", L"Ẩn/Hiện đường gióng", L"Ctrl+;"},
    {MenuActionId::ViewClearGuidelines, L"Clear All Guidelines", L"Xóa hết đường gióng", L"Ctrl+Alt+;"},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::ViewSnapGrid, L"Snap to Grid", L"Hít vào lưới", L"", false, true},
    {MenuActionId::ViewSnapObjects, L"Snap to Objects", L"Hít vào đối tượng", L"", false, true}
}};

// 4. Layout Menu
inline constexpr std::array<MenuItem, 8> kLayoutMenuItems{{
    {MenuActionId::LayoutPageSetup, L"Page Setup…", L"Thiết lập trang…", L"", false, true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::LayoutOrientationLandscape, L"Landscape Orientation", L"Khổ ngang", L""},
    {MenuActionId::LayoutOrientationPortrait, L"Portrait Orientation", L"Khổ dọc", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::LayoutPageA4, L"A4 (210 × 297 mm)", L"Khổ A4 (210 × 297 mm)", L""},
    {MenuActionId::LayoutPageA3, L"A3 (297 × 420 mm)", L"Khổ A3 (297 × 420 mm)", L""},
    {MenuActionId::LayoutPageBanner, L"Banner 3 × 1 m", L"Băng rôn 3 × 1 m", L""}
}};

// 5. Object Menu
inline constexpr std::array<MenuItem, 39> kObjectMenuItems{{
    {MenuActionId::ObjectGroup, L"Group Objects", L"Nhóm đối tượng", L"Ctrl+G"},
    {MenuActionId::ObjectUngroup, L"Ungroup", L"Rã nhóm", L"Ctrl+Shift+G"},
    {MenuActionId::ObjectProperties, L"Properties…", L"Thuộc tính…", L""},
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
inline constexpr std::array<MenuItem, 7> kEffectsMenuItems{{
    {MenuActionId::EffectsGaussianBlur, L"Gaussian Blur", L"Làm mờ Gaussian", L""},
    {MenuActionId::EffectsDropShadow, L"Drop Shadow", L"Đổ bóng", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::EffectsContour, L"Contour (M2)…", L"Tạo viền Contour (M2)…", L"", false, true},
    {MenuActionId::EffectsExtrude, L"Extrude 3D (M2)…", L"Khối nổi Extrude 3D (M2)…", L"", false, true},
    {MenuActionId::EffectsBevel, L"Bevel (M2)…", L"Vát cạnh Bevel (M2)…", L"", false, true},
    {MenuActionId::EffectsClear, L"Clear Effects", L"Xóa hiệu ứng", L"", false, false}
}};

// 7. Bitmaps Menu
inline constexpr std::array<MenuItem, 5> kBitmapsMenuItems{{
    {MenuActionId::BitmapsConvert, L"Convert to Bitmap…", L"Chuyển sang ảnh Bitmap…", L"", false, true},
    {MenuActionId::BitmapsEdit, L"Edit Bitmap…", L"Chỉnh sửa ảnh…", L"", false, true},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::BitmapsTrace, L"Quick Vectorize / Trace…", L"Vector hóa ảnh (Trace)…", L"", false, true},
    {MenuActionId::BitmapsColorMask, L"Bitmap Color Mask…", L"Mặt nạ màu ảnh…", L"", false, true}
}};

// 8. Text Menu
inline constexpr std::array<MenuItem, 5> kTextMenuItems{{
    {MenuActionId::TextEdit, L"Edit Text", L"Sửa văn bản", L""},
    {MenuActionId::Separator, L"", L"", L"", true},
    {MenuActionId::TextBold, L"Bold Font", L"Chữ đậm", L"Ctrl+B"},
    {MenuActionId::TextItalic, L"Italic Font", L"Chữ nghiêng", L"Ctrl+I"},
    {MenuActionId::TextConvertToCurves, L"Convert Text to Curves", L"Chuyển chữ thành đường cong", L"Ctrl+Q"}
}};

// 9. Tools Menu
inline constexpr std::array<MenuItem, 10> kToolsMenuItems{{
    {MenuActionId::ToolPick, L"Pick Tool (Selection)", L"Công cụ chọn (Pick)", L"V"},
    {MenuActionId::ToolNode, L"Node Edit Tool", L"Chỉnh sửa điểm (Node)", L"", false, true},
    {MenuActionId::ToolRectangle, L"Rectangle Tool", L"Vẽ hình chữ nhật", L"R"},
    {MenuActionId::ToolEllipse, L"Ellipse Tool", L"Vẽ hình elip / tròn", L"E"},
    {MenuActionId::ToolPolygon, L"Polygon Tool", L"Vẽ đa giác đều", L""},
    {MenuActionId::ToolText, L"Text Tool", L"Nhập văn bản", L"A / T"},
    {MenuActionId::ToolKnife, L"Knife Cutting Tool", L"Dao cắt hình", L"K"},
    {MenuActionId::ToolLine, L"Line Tool", L"Đoạn thẳng", L"L"},
    {MenuActionId::ToolPolyline, L"Polyline Tool", L"Đường gấp khúc", L"Shift+L"},
    {MenuActionId::ToolPen, L"Pen Tool", L"Công cụ Pen", L""}
}};

// 10. Window Menu
inline constexpr std::array<MenuItem, 6> kWindowMenuItems{{
    {MenuActionId::WindowProperties, L"Properties Inspector", L"Bảng thuộc tính", L""},
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

inline int get_menu_height(MenuId id) noexcept {
    const auto items = get_menu_items(id);
    if (items.empty()) return 0;
    int h = 2 * kMenuPadding;
    for (const auto& item : items) {
        h += item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
    }
    return h;
}

inline const MenuItem* get_menu_item_at(MenuId id, int offset_y) noexcept {
    const auto items = get_menu_items(id);
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

inline std::optional<MenuActionId> get_menu_action_at(MenuId id, int offset_y) noexcept {
    const auto* item = get_menu_item_at(id, offset_y);
    if (!item || item->is_disabled) return std::nullopt;
    return item->action;
}

template <typename IsEnabled>
inline std::optional<MenuActionId> get_adjacent_menu_action(
    MenuId id, int current_action, int direction, IsEnabled&& is_enabled) noexcept {
    const auto items = get_menu_items(id);
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

inline std::optional<MenuActionId> get_adjacent_menu_action(
    MenuId id, int current_action, int direction) noexcept {
    return get_adjacent_menu_action(id, current_action, direction, [](MenuActionId) { return true; });
}

} // namespace pikdraw::desktop
