#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/unit_commands.hpp"

namespace pikdraw::desktop {

bool DocumentWorkflow::set_page_display_length_unit(document::PageId page_id,
    geometry::units::LengthUnit unit) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetPageDisplayLengthUnitCommand>(page_id, unit));
    sync_status_bar();
    return result.success;
}

} // namespace pikdraw::desktop
