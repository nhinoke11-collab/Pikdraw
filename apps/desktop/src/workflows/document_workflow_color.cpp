#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/color_commands.hpp"

namespace pikdraw::desktop {
bool DocumentWorkflow::convert_document_color_mode(color::DocumentColorMode mode,
    std::shared_ptr<const color::IccPreview> icc) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::ConvertDocumentColorModeCommand>(
            mode, color::default_document_profile(mode), std::move(icc)));
    if (result.success && !result.is_no_change) {
        cancel_effect_draft();
        clear_text_preview();
    }
    sync_status_bar();
    return result.success;
}
bool DocumentWorkflow::set_document_raster_dpi(int32_t dpi) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetDocumentRasterDpiCommand>(dpi));
    if (result.success && !result.is_no_change) cancel_effect_draft();
    sync_status_bar();
    return result.success;
}
} // namespace pikdraw::desktop
