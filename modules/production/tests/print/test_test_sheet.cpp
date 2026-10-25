#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/production/print/test_sheet.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace pikdraw::production::print;

namespace {
std::shared_ptr<const pikdraw::color::ProfileResource> system_srgb() {
    const auto result = pikdraw::color::ProfileResource::load_file(
        pikdraw::color::IccPreview::system_srgb_profile(),
        pikdraw::color::ColorSpace::Rgb);
    assert(result && "Test requires an installed, validated system sRGB ICC profile");
    return result.resource;
}

TestSheetRequest base_request() {
    TestSheetRequest request;
    request.run_id = "run-0001";
    request.source = {"document-0001", 12, 84, 3, 2400, 1800};
    request.condition.id = "rgb-export-default";
    request.condition.revision = 4;
    request.condition.profile = system_srgb();
    request.page_width_mm = 210.0;
    request.page_height_mm = 297.0;
    request.margin_mm = 10.0;
    request.patch_size_mm = 10.0;
    request.patch_gap_mm = 2.0;
    return request;
}
}

int main() {
    {
        auto request = base_request();
        request.kind = TestSheetKind::CropAndDiagnostic;
        request.crop = {100, 200, 600, 450};
        const auto result = plan_test_sheet(request);
        assert(result);
        const auto& plan = *result.plan;
        const auto crop = std::find_if(plan.elements.begin(), plan.elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::RasterCrop; });
        assert(crop != plan.elements.end() && crop->source_crop == request.crop);
        assert(std::abs(crop->bounds_mm.width - 50.8) < 1e-9);
        assert(std::abs(crop->bounds_mm.height - 38.1) < 1e-9);
        const auto ruler = std::find_if(plan.elements.begin(), plan.elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::CalibrationRuler; });
        assert(ruler != plan.elements.end() && ruler->bounds_mm.width == 100.0);
        const auto grays = std::count_if(plan.elements.begin(), plan.elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::GrayPatch; });
        const auto colors = std::count_if(plan.elements.begin(), plan.elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::ProcessColorPatch; });
        assert(grays == 11 && colors == 8);
        assert(plan.condition.profile->identity().checksum_sha256 ==
               request.condition.profile->identity().checksum_sha256);
    }

    {
        auto request = base_request();
        request.kind = TestSheetKind::VariationGrid;
        request.variation_columns = 3;
        request.variation_rows = 3;
        request.x_axis = {"exposure", "EV", {-0.2, 0.0, 0.2}};
        request.y_axis = {"tint", "%", {-5.0, 0.0, 5.0}};
        const auto result = plan_test_sheet(request);
        assert(result);
        const auto cells = std::count_if(result.plan->elements.begin(), result.plan->elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::VariationCell; });
        const auto baseline = std::find_if(result.plan->elements.begin(), result.plan->elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::VariationCell && item.is_baseline; });
        assert(cells == 9 && baseline != result.plan->elements.end());
        assert(baseline->row == 1 && baseline->column == 1 &&
               baseline->x_value == 0.0 && baseline->y_value == 0.0);
    }

    {
        auto request = base_request();
        request.kind = TestSheetKind::VariationGrid;
        request.variation_columns = 5;
        request.variation_rows = 1;
        request.x_axis = {"saturation", "%", {-20.0, -10.0, 0.0, 10.0, 20.0}};
        const auto result = plan_test_sheet(request);
        assert(result);
        const auto baseline = std::find_if(result.plan->elements.begin(), result.plan->elements.end(),
            [](const auto& item) { return item.kind == TestSheetElementKind::VariationCell && item.is_baseline; });
        assert(baseline != result.plan->elements.end() && baseline->column == 2);
    }

    {
        auto request = base_request();
        request.kind = TestSheetKind::OneToOneCrop;
        request.crop = {2300, 1700, 200, 100};
        assert(plan_test_sheet(request).error == TestSheetPlanError::InvalidCrop);
        request.crop = {100, 100, 600, 450};
        request.condition.profile.reset();
        assert(plan_test_sheet(request).error == TestSheetPlanError::InvalidOutputCondition);
    }

    {
        auto request = base_request();
        request.kind = TestSheetKind::VariationGrid;
        request.x_axis = {"exposure", "EV", {-0.2, 0.1, 0.2}};
        assert(plan_test_sheet(request).error == TestSheetPlanError::InvalidVariationGrid);
        request.x_axis = {"exposure", "EV", {-0.2, 0.0, 0.2}};
        request.y_axis = {"exposure", "EV", {-5.0, 0.0, 5.0}};
        request.variation_rows = 3;
        assert(plan_test_sheet(request).error == TestSheetPlanError::InvalidVariationGrid);
    }

    {
        auto condition = base_request().condition;
        condition.route = PrintRoute::DriverManagedRgb;
        condition.device_id = "printer-unknown-01";
        assert(validate_output_condition(condition).error ==
               OutputConditionError::DriverColorOwnerUnknown);
        condition.driver_color_state = DriverColorState::Managed;
        assert(validate_output_condition(condition));

        condition.route = PrintRoute::SystemManagedRgb;
        condition.driver_color_state = DriverColorState::Unknown;
        assert(validate_output_condition(condition).error ==
               OutputConditionError::DriverColorOwnerUnknown);
        condition.driver_color_state = DriverColorState::Disabled;
        assert(validate_output_condition(condition));
        condition.require_destination_profile_identity = true;
        assert(validate_output_condition(condition).error ==
               OutputConditionError::MissingDestinationProfileIdentity);
        condition.destination_profile_sha256 = std::string(64, 'a');
        assert(validate_output_condition(condition));
        condition.destination_profile_sha256 = "not-a-profile-hash";
        assert(validate_output_condition(condition).error ==
               OutputConditionError::MissingDestinationProfileIdentity);
        condition.destination_profile_sha256.clear();
        condition.require_destination_profile_identity = false;
        condition.driver_color_state = DriverColorState::Managed;
        assert(!validate_output_condition(condition));
    }

    std::cout << "P4 print output condition and test-sheet planning tests passed\n";
}
