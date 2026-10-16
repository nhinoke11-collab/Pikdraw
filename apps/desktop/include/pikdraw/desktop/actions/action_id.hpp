#pragma once

#include <string_view>

namespace pikdraw::desktop::actions {

struct ActionId {
    // File domain (CorelDRAW Reference)
    static constexpr std::string_view kFileNew = "action.file.new";
    static constexpr std::string_view kFileNewFromTemplate = "action.file.new_from_template";
    static constexpr std::string_view kFileOpen = "action.file.open";
    static constexpr std::string_view kFileOpenCloud = "action.file.open_cloud";
    static constexpr std::string_view kFileOpenRecent = "action.file.open_recent";
    static constexpr std::string_view kFileClose = "action.file.close";
    static constexpr std::string_view kFileCloseAll = "action.file.close_all";
    static constexpr std::string_view kFileSave = "action.file.save";
    static constexpr std::string_view kFileSaveAs = "action.file.save_as";
    static constexpr std::string_view kFileSaveCloud = "action.file.save_cloud";
    static constexpr std::string_view kFileSaveTemplate = "action.file.save_template";
    static constexpr std::string_view kFileRevert = "action.file.revert";
    static constexpr std::string_view kFileAcquireImage = "action.file.acquire_image";
    static constexpr std::string_view kFileImport = "action.file.import";
    static constexpr std::string_view kFileExport = "action.file.export";
    static constexpr std::string_view kFileExportFor = "action.file.export_for";
    static constexpr std::string_view kFilePublishPdf = "action.file.publish_pdf";
    static constexpr std::string_view kFilePrint = "action.file.print";
    static constexpr std::string_view kFilePrintPreview = "action.file.print_preview";
    static constexpr std::string_view kFileProperties = "action.file.properties";
    static constexpr std::string_view kFileExit = "action.file.exit";

    // Edit domain
    static constexpr std::string_view kEditUndo = "action.edit.undo";
    static constexpr std::string_view kEditRedo = "action.edit.redo";
    static constexpr std::string_view kEditCut = "action.edit.cut";
    static constexpr std::string_view kEditCopy = "action.edit.copy";
    static constexpr std::string_view kEditPaste = "action.edit.paste";
    static constexpr std::string_view kEditDelete = "action.edit.delete";
    static constexpr std::string_view kEditSelectAll = "action.edit.select_all";

    // View & Home domain
    static constexpr std::string_view kViewHome = "action.view.home";
    static constexpr std::string_view kViewZoomIn = "action.view.zoom_in";
    static constexpr std::string_view kViewZoomOut = "action.view.zoom_out";
    static constexpr std::string_view kViewZoom100 = "action.view.zoom_100";
    static constexpr std::string_view kViewFitAll = "action.view.fit_all";
    static constexpr std::string_view kViewReloadUi = "action.view.reload_ui";

    // Tool domain
    static constexpr std::string_view kToolSelect = "action.tool.select";
    static constexpr std::string_view kToolNodeEdit = "action.tool.node_edit";
    static constexpr std::string_view kToolPen = "action.tool.pen";
    static constexpr std::string_view kToolRectangle = "action.tool.rectangle";
    static constexpr std::string_view kToolEllipse = "action.tool.ellipse";
    static constexpr std::string_view kToolText = "action.tool.text";
    static constexpr std::string_view kToolLine = "action.tool.line";
    static constexpr std::string_view kToolPolyline = "action.tool.polyline";
    static constexpr std::string_view kToolSnapPanel = "action.tool.snap_panel";

    // Object domain
    static constexpr std::string_view kObjectGroup = "action.object.group";
    static constexpr std::string_view kObjectUngroup = "action.object.ungroup";
    static constexpr std::string_view kObjectToCurves = "action.object.to_curves";
    static constexpr std::string_view kObjectBooleanUnion = "action.object.boolean_union";
    static constexpr std::string_view kObjectBooleanDifference = "action.object.boolean_difference";

    [[nodiscard]] static bool is_valid_action_id(std::string_view id) noexcept;
};

} // namespace pikdraw::desktop::actions
