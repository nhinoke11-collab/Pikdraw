#pragma once

#include <string_view>

namespace pikdraw::desktop::actions {

struct ActionId {
    // File domain (CorelDRAW Reference)
    static constexpr std::string_view kFileNew = "action.file.new";
    static constexpr std::string_view kFileNewFromTemplate = "action.file.new_from_template";
    static constexpr std::string_view kFileOpen = "action.file.open";
    static constexpr std::string_view kFileRecoverAutosave = "action.file.recover_autosave";
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
    static constexpr std::string_view kFileExportRaster = "action.file.export_raster";
    static constexpr std::string_view kFileExportFor = "action.file.export_for";
    static constexpr std::string_view kFilePublishPdf = "action.file.publish_pdf";
    static constexpr std::string_view kFilePrint = "action.file.print";
    static constexpr std::string_view kFilePrintPreview = "action.file.print_preview";
    static constexpr std::string_view kFileTestPrint = "action.file.test_print";
    static constexpr std::string_view kFilePrintTestSheet = "action.file.print_test_sheet";
    static constexpr std::string_view kFileImportMeasurementRun = "action.file.import_measurement_run";
    static constexpr std::string_view kFileCompareMeasurementRuns = "action.file.compare_measurement_runs";
    static constexpr std::string_view kFileProperties = "action.file.properties";
    static constexpr std::string_view kFileExit = "action.file.exit";

    // Edit domain
    static constexpr std::string_view kEditUndo = "action.edit.undo";
    static constexpr std::string_view kEditRedo = "action.edit.redo";
    static constexpr std::string_view kEditCut = "action.edit.cut";
    static constexpr std::string_view kEditCopy = "action.edit.copy";
    static constexpr std::string_view kEditPaste = "action.edit.paste";
    static constexpr std::string_view kEditPasteInPlace = "action.edit.paste_in_place";
    static constexpr std::string_view kEditPasteInFront = "action.edit.paste_in_front";
    static constexpr std::string_view kEditPasteInBack = "action.edit.paste_in_back";
    static constexpr std::string_view kEditDuplicate = "action.edit.duplicate";
    static constexpr std::string_view kEditDuplicateAtOrigin = "action.edit.duplicate_at_origin";
    static constexpr std::string_view kEditStepAndRepeat = "action.edit.step_and_repeat";
    static constexpr std::string_view kEditDelete = "action.edit.delete";
    static constexpr std::string_view kEditSelectAll = "action.edit.select_all";

    // View & Home domain
    static constexpr std::string_view kViewHome = "action.view.home";
    static constexpr std::string_view kViewZoomIn = "action.view.zoom_in";
    static constexpr std::string_view kViewZoomOut = "action.view.zoom_out";
    static constexpr std::string_view kViewZoom100 = "action.view.zoom_100";
    static constexpr std::string_view kViewFitAll = "action.view.fit_all";
    static constexpr std::string_view kViewReloadUi = "action.view.reload_ui";
    static constexpr std::string_view kViewProofColors = "action.view.proof_colors";
    static constexpr std::string_view kViewGamutWarning = "action.view.gamut_warning";
    static constexpr std::string_view kViewOverprintPreview = "action.view.overprint_preview";
    static constexpr std::string_view kColorAssignProfile = "action.color.assign_profile";
    static constexpr std::string_view kColorConvertProfile = "action.color.convert_profile";
    static constexpr std::string_view kColorConvertDocumentProfile = "action.color.convert_document_profile";
    static constexpr std::string_view kColorAssignSelectionProfile = "action.color.assign_selection_profile";
    static constexpr std::string_view kColorConvertSelectionProfile = "action.color.convert_selection_profile";

    // Bitmap editing (one stable action ID per user-facing operation).
    static constexpr std::string_view kBitmapCrop = "action.bitmap.crop";
    static constexpr std::string_view kBitmapExportMaster = "action.bitmap.export_master";
    static constexpr std::string_view kBitmapExportCmykMaster = "action.bitmap.export_cmyk_master";
    static constexpr std::string_view kBitmapAssignProfile = "action.bitmap.assign_profile";
    static constexpr std::string_view kBitmapConvertProfileToSrgb = "action.bitmap.convert_profile_to_srgb";
    static constexpr std::string_view kBitmapAdjustExposure = "action.bitmap.adjust.exposure";
    static constexpr std::string_view kBitmapAdjustLight = "action.bitmap.adjust.light";
    static constexpr std::string_view kBitmapAdjustColorBalance = "action.bitmap.adjust.color_balance";
    static constexpr std::string_view kBitmapAdjustChannelMixer = "action.bitmap.adjust.channel_mixer";
    static constexpr std::string_view kBitmapLocalExposureMask = "action.bitmap.adjust.local_exposure_mask";
    static constexpr std::string_view kBitmapAdjustLevels = "action.bitmap.adjust.levels";
    static constexpr std::string_view kBitmapAdjustHsl = "action.bitmap.adjust.hsl";
    static constexpr std::string_view kBitmapAdjustCurves = "action.bitmap.adjust.curves";
    static constexpr std::string_view kBitmapAdjustWhiteBalance = "action.bitmap.adjust.white_balance";
    static constexpr std::string_view kBitmapAdjustVibrance = "action.bitmap.adjust.vibrance";
    static constexpr std::string_view kBitmapAdjustSelectiveHsl = "action.bitmap.adjust.selective_hsl";
    static constexpr std::string_view kBitmapCloneStamp = "action.bitmap.retouch.clone_stamp";
    static constexpr std::string_view kBitmapFlipHorizontal = "action.bitmap.flip_horizontal";
    static constexpr std::string_view kBitmapFlipVertical = "action.bitmap.flip_vertical";
    static constexpr std::string_view kPageUnitsMillimeter = "action.page.units.millimeter";
    static constexpr std::string_view kPageUnitsCentimeter = "action.page.units.centimeter";
    static constexpr std::string_view kPageUnitsMeter = "action.page.units.meter";
    static constexpr std::string_view kPageUnitsInch = "action.page.units.inch";
    static constexpr std::string_view kPageUnitsFoot = "action.page.units.foot";
    static constexpr std::string_view kPageUnitsYard = "action.page.units.yard";
    static constexpr std::string_view kPageUnitsPoint = "action.page.units.point";
    static constexpr std::string_view kPageUnitsPica = "action.page.units.pica";
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
    static constexpr std::string_view kToolKnife = "action.tool.knife";
    static constexpr std::string_view kToolVectorEraser = "action.tool.vector_eraser";
    static constexpr std::string_view kToolScissors = "action.tool.scissors";
    static constexpr std::string_view kToolTrimObjects = "action.tool.trim_objects";
    static constexpr std::string_view kToolSmartTrim = "action.tool.smart_trim";
    static constexpr std::string_view kToolTrim = "action.tool.trim";
    static constexpr std::string_view kToolSmartFill = "action.tool.smart_fill";
    static constexpr std::string_view kToolShapeBuilder = "action.tool.shape_builder";
    static constexpr std::string_view kToolRasterEraser = "action.tool.raster_eraser";
    static constexpr std::string_view kObjectTrimSourceTarget = "action.object.trim_source_target";
    static constexpr std::string_view kToolContour = "action.tool.contour";
    static constexpr std::string_view kEffectsContourFocus = "action.effects.contour.focus";
    static constexpr std::string_view kObjectContourBreakApart = "action.object.contour.break_apart";

    // Effects domain
    static constexpr std::string_view kEffectsAppearanceFocus = "action.effects.appearance.focus";
    static constexpr std::string_view kEffectsGaussianBlur = "action.effects.gaussian_blur";
    static constexpr std::string_view kEffectsFeather = "action.effects.feather";
    static constexpr std::string_view kEffectsDropShadow = "action.effects.drop_shadow";
    static constexpr std::string_view kEffectsInnerShadow = "action.effects.inner_shadow";
    static constexpr std::string_view kEffectsOuterGlow = "action.effects.outer_glow";
    static constexpr std::string_view kEffectsInnerGlow = "action.effects.inner_glow";
    static constexpr std::string_view kEffectsBlockShadow = "action.effects.block_shadow";
    static constexpr std::string_view kEffectsOffsetPath = "action.effects.offset_path";
    static constexpr std::string_view kEffectsTransformRepeat = "action.effects.transform_repeat";
    static constexpr std::string_view kEffectsRoughen = "action.effects.roughen";
    static constexpr std::string_view kEffectsZigzag = "action.effects.zigzag";
    static constexpr std::string_view kEffectsColorOverlay = "action.effects.color_overlay";
    static constexpr std::string_view kEffectsBlend = "action.effects.blend";
    static constexpr std::string_view kEffectsEnvelope = "action.effects.envelope";
    static constexpr std::string_view kEffectsDistort = "action.effects.distort";
    static constexpr std::string_view kEffectsBevel = "action.effects.bevel";
    static constexpr std::string_view kEffectsExtrude = "action.effects.extrude";
    static constexpr std::string_view kEffectsClearSelected = "action.effects.clear_selected";
    static constexpr std::string_view kEffectsClearAll = "action.effects.clear_all_effects";

    // Object domain
    static constexpr std::string_view kObjectProperties = "action.object.properties.focus";
    static constexpr std::string_view kObjectGroup = "action.object.group";
    static constexpr std::string_view kObjectUngroup = "action.object.ungroup";
    static constexpr std::string_view kObjectPowerClipPlaceInside = "action.object.powerclip.place_inside";
    static constexpr std::string_view kObjectPowerClipMake = "action.object.powerclip.make";
    static constexpr std::string_view kObjectPowerClipRelease = "action.object.powerclip.release";
    static constexpr std::string_view kObjectPowerClipEdit = "action.object.powerclip.edit";
    static constexpr std::string_view kObjectPowerClipFinish = "action.object.powerclip.finish";
    static constexpr std::string_view kObjectPowerClipLock = "action.object.powerclip.lock";
    static constexpr std::string_view kObjectPowerClipFitCenter = "action.object.powerclip.fit_center";
    static constexpr std::string_view kObjectPowerClipFitProp = "action.object.powerclip.fit_prop";
    static constexpr std::string_view kObjectPowerClipFillProp = "action.object.powerclip.fill_prop";
    static constexpr std::string_view kObjectPowerClipStretch = "action.object.powerclip.stretch";
    static constexpr std::string_view kObjectToCurves = "action.object.to_curves";
    static constexpr std::string_view kObjectAlignLeft = "action.object.align_left";
    static constexpr std::string_view kObjectAlignCenterHorizontal = "action.object.align_center_horizontal";
    static constexpr std::string_view kObjectAlignRight = "action.object.align_right";
    static constexpr std::string_view kObjectAlignTop = "action.object.align_top";
    static constexpr std::string_view kObjectAlignCenterVertical = "action.object.align_center_vertical";
    static constexpr std::string_view kObjectAlignBottom = "action.object.align_bottom";
    static constexpr std::string_view kObjectCenterPage = "action.object.center_page";
    static constexpr std::string_view kObjectDistributeSpacingHorizontal = "action.object.distribute_spacing_horizontal";
    static constexpr std::string_view kObjectDistributeSpacingVertical = "action.object.distribute_spacing_vertical";
    static constexpr std::string_view kObjectBooleanUnion = "action.object.boolean_union";
    static constexpr std::string_view kObjectBooleanDifference = "action.object.boolean_difference";
    static constexpr std::string_view kObjectBooleanIntersect = "action.object.boolean_intersect";
    static constexpr std::string_view kObjectBooleanExclude = "action.object.boolean_exclude";
    static constexpr std::string_view kObjectBooleanBackMinus = "action.object.boolean_back_minus";
    static constexpr std::string_view kObjectBooleanBoundary = "action.object.boolean_boundary";
    static constexpr std::string_view kObjectBooleanAutoWeld = "action.object.boolean_auto_weld";
    static constexpr std::string_view kObjectFlipHorizontal = "action.object.flip_horizontal";
    static constexpr std::string_view kObjectFlipVertical = "action.object.flip_vertical";
    static constexpr std::string_view kObjectRotate90CW = "action.object.rotate_90_cw";
    static constexpr std::string_view kObjectRotate90CCW = "action.object.rotate_90_ccw";
    static constexpr std::string_view kObjectRotate180 = "action.object.rotate_180";
    static constexpr std::string_view kObjectOrderToFront = "action.object.order_to_front";
    static constexpr std::string_view kObjectOrderToBack = "action.object.order_to_back";
    static constexpr std::string_view kObjectOrderForwardOne = "action.object.order_forward_one";
    static constexpr std::string_view kObjectOrderBackOne = "action.object.order_back_one";
    static constexpr std::string_view kObjectNudgeUp = "action.object.nudge_up";
    static constexpr std::string_view kObjectNudgeDown = "action.object.nudge_down";
    static constexpr std::string_view kObjectNudgeLeft = "action.object.nudge_left";
    static constexpr std::string_view kObjectNudgeRight = "action.object.nudge_right";
    static constexpr std::string_view kObjectSuperNudgeUp = "action.object.super_nudge_up";
    static constexpr std::string_view kObjectSuperNudgeDown = "action.object.super_nudge_down";
    static constexpr std::string_view kObjectSuperNudgeLeft = "action.object.super_nudge_left";
    static constexpr std::string_view kObjectSuperNudgeRight = "action.object.super_nudge_right";
    static constexpr std::string_view kObjectMicroNudgeUp = "action.object.micro_nudge_up";
    static constexpr std::string_view kObjectMicroNudgeDown = "action.object.micro_nudge_down";
    static constexpr std::string_view kObjectMicroNudgeLeft = "action.object.micro_nudge_left";
    static constexpr std::string_view kObjectMicroNudgeRight = "action.object.micro_nudge_right";

    // Settings and help
    static constexpr std::string_view kSettingsAiConnections = "action.settings.ai_connections";
    static constexpr std::string_view kSettingsAiConnectionsCopyEndpoint =
        "action.settings.ai_connections.copy_endpoint";
    static constexpr std::string_view kSettingsAiConnectionsCopyAccessKey =
        "action.settings.ai_connections.copy_access_key";
    static constexpr std::string_view kHelpAiAutomation = "action.help.ai_automation";

    [[nodiscard]] static bool is_valid_action_id(std::string_view id) noexcept;
};

} // namespace pikdraw::desktop::actions
