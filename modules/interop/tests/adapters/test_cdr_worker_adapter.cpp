#include "pikdraw/interop/adapters/cdr_worker_adapter.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "../../src/adapters/cdr_worker_adapter_geometry.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace pikdraw::interop;

int main() {
    {
        // The SVG rect path arrives TL, TR, BR, BL after the page Y flip.
        // This source rect is rotated 90 degrees counter-clockwise.
        const std::array<pikdraw::geometry::Point2D, 4> rotated_corners{
            pikdraw::geometry::Point2D{-20, 0}, {-20, 20}, {-10, 20}, {-10, 0}};
        double width = 0.0, height = 0.0;
        std::array<double, 6> matrix{};
        assert(pikdraw::interop::detail::CdrImagePlacementFromPageCorners(
            rotated_corners, width, height, matrix));
        assert(std::abs(width - 20.0) < 1e-12 && std::abs(height - 10.0) < 1e-12);
        const pikdraw::geometry::Transform2D transform{
            matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]};
        const auto assert_point = [](pikdraw::geometry::Point2D actual,
                                     pikdraw::geometry::Point2D expected) {
            assert(std::hypot(actual.x - expected.x, actual.y - expected.y) < 1e-12);
        };
        assert_point(transform.map_point({0, 0}), rotated_corners[3]);
        assert_point(transform.map_point({width, 0}), rotated_corners[2]);
        assert_point(transform.map_point({width, height}), rotated_corners[1]);
        assert_point(transform.map_point({0, height}), rotated_corners[0]);

        const std::array<pikdraw::geometry::Point2D, 4> unrotated_svg_corners{
            pikdraw::geometry::Point2D{0, 10}, {20, 10}, {20, 0}, {0, 0}};
        assert(pikdraw::interop::detail::CdrImagePlacementFromPageCorners(
            unrotated_svg_corners, width, height, matrix));
        assert(std::abs(width - 20.0) < 1e-12 && std::abs(height - 10.0) < 1e-12);
        const pikdraw::geometry::Transform2D unrotated_transform{
            matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]};
        assert_point(unrotated_transform.map_point({0, 0}), unrotated_svg_corners[3]);
        assert_point(unrotated_transform.map_point({width, 0}), unrotated_svg_corners[2]);
        assert_point(unrotated_transform.map_point({width, height}), unrotated_svg_corners[1]);
        assert_point(unrotated_transform.map_point({0, height}), unrotated_svg_corners[0]);
        assert(unrotated_transform.determinant() > 0.0);
    }

    std::vector<CanonicalImportRecord> records;
    std::string diagnostic;
    assert(!BuildCdrImportRecords({}, records, diagnostic));
    assert(records.empty() && !diagnostic.empty());

#ifdef _WIN32
    const wchar_t* fixture_directory = _wgetenv(L"PIKDRAW_CDR_REAL_FIXTURE_DIR");
    if (fixture_directory && *fixture_directory) {
        size_t failures = 0;
        size_t fixture_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(fixture_directory)) {
            if (!entry.is_regular_file()) continue;
            auto extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (extension != L".cdr") continue;
            ++fixture_count;
            const auto encoded_name = entry.path().filename().u8string();
            const std::string fixture_name(reinterpret_cast<const char*>(encoded_name.data()),
                                           encoded_name.size());
            std::ifstream input(entry.path(), std::ios::binary);
            std::vector<uint8_t> source((std::istreambuf_iterator<char>(input)), {});
            records.clear();
            diagnostic.clear();
            size_t memory_admission_calls = 0;
            uint64_t largest_memory_admission = 0;
            if (!input.good() && !input.eof()) {
                std::cerr << "Could not read CDR corpus fixture [" << fixture_name << "]\n";
                ++failures;
                continue;
            }
            const bool converted = BuildCdrImportRecordsAndReleaseSource(
                source, records, diagnostic,
                [&](uint64_t bytes, std::string&) {
                    ++memory_admission_calls;
                    largest_memory_admission = (std::max)(largest_memory_admission, bytes);
                    return true;
                });
            assert(source.empty());
            if (!converted) {
                std::cerr << "CDR adapter rejected [" << fixture_name << "]: " << diagnostic << '\n';
                ++failures;
                continue;
            }
            size_t page_count = 0, layer_count = 0, object_count = 0, image_count = 0;
            size_t image_chunk_count = 0, source_chunk_count = 0;
            double page_width_mm = 0.0, page_height_mm = 0.0;
            size_t arc_element_count = 0;
            uint32_t max_image_width = 0, max_image_height = 0;
            uint64_t max_image_pixel_count = 0;
            uint64_t total_decoded_image_bytes = 0;
            double min_x = std::numeric_limits<double>::infinity();
            double min_y = std::numeric_limits<double>::infinity();
            double max_x = -std::numeric_limits<double>::infinity();
            double max_y = -std::numeric_limits<double>::infinity();
            double path_min_x = std::numeric_limits<double>::infinity();
            double path_min_y = std::numeric_limits<double>::infinity();
            double path_max_x = -std::numeric_limits<double>::infinity();
            double path_max_y = -std::numeric_limits<double>::infinity();
            double content_min_x = std::numeric_limits<double>::infinity();
            double content_min_y = std::numeric_limits<double>::infinity();
            double content_max_x = -std::numeric_limits<double>::infinity();
            double content_max_y = -std::numeric_limits<double>::infinity();
            double image_min_x = std::numeric_limits<double>::infinity();
            double image_min_y = std::numeric_limits<double>::infinity();
            double image_max_x = -std::numeric_limits<double>::infinity();
            double image_max_y = -std::numeric_limits<double>::infinity();
            const auto include_bounds = [](double left, double bottom, double right, double top,
                                           double& min_left, double& min_bottom,
                                           double& max_right, double& max_top) {
                min_left = std::min(min_left, left);
                min_bottom = std::min(min_bottom, bottom);
                max_right = std::max(max_right, right);
                max_top = std::max(max_top, top);
            };
            for (const auto& record : records) {
                const auto decoded = DeserializeImportRecord(record.type, record.payload);
                assert(decoded);
                if (const auto* page = std::get_if<ImportPageRecord>(&*decoded)) {
                    ++page_count;
                    page_width_mm = page->width_mm;
                    page_height_mm = page->height_mm;
                }
                else if (std::holds_alternative<ImportLayerRecord>(*decoded)) ++layer_count;
                else if (std::holds_alternative<ImportRasterImageChunkRecord>(*decoded)) ++image_chunk_count;
                else if (std::holds_alternative<ImportRasterSourceChunkRecord>(*decoded)) ++source_chunk_count;
                else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) {
                    ++object_count;
                    pikdraw::geometry::Path2D geometry;
                    for (const auto& element : path->elements) {
                        const auto& v = element.values;
                        switch (element.verb) {
                        case ImportPathVerb::Move: geometry.move_to(v[0], v[1]); break;
                        case ImportPathVerb::Line: geometry.line_to(v[0], v[1]); break;
                        case ImportPathVerb::Quad: geometry.quad_to(v[2], v[3], v[0], v[1]); break;
                        case ImportPathVerb::Cubic: geometry.cubic_to(v[2], v[3], v[4], v[5], v[0], v[1]); break;
                        case ImportPathVerb::Arc:
                            ++arc_element_count;
                            geometry.arc_to(v[6], v[7], v[8],
                                (element.flags & 1) != 0, (element.flags & 2) != 0, v[0], v[1]);
                            break;
                        case ImportPathVerb::Close: geometry.close(); break;
                        }
                    }
                    const auto bounds = geometry.bounds();
                    include_bounds(bounds.x, bounds.y, bounds.right(), bounds.bottom(),
                                   path_min_x, path_min_y, path_max_x, path_max_y);
                    include_bounds(bounds.x, bounds.y, bounds.right(), bounds.bottom(),
                                   content_min_x, content_min_y, content_max_x, content_max_y);
                    if (path->has_fill || (path->has_stroke && path->stroke_width_mm > 0.0)) {
                        include_bounds(bounds.x, bounds.y, bounds.right(), bounds.bottom(),
                                       min_x, min_y, max_x, max_y);
                    }
                } else if (std::holds_alternative<ImportTextRecord>(*decoded) ||
                           std::holds_alternative<ImportGroupRecord>(*decoded) ||
                           std::holds_alternative<ImportRasterImageRecord>(*decoded)) {
                    ++object_count;
                    if (const auto* image = std::get_if<ImportRasterImageRecord>(&*decoded)) {
                        ++image_count;
                        assert(image->width_px > 0 && image->height_px > 0);
                        assert(image->decoded_pixel_bytes ==
                            static_cast<uint64_t>(image->width_px) * image->height_px * 4);
                        assert(image->source_data_local_id != 0 && image->source_bytes_size != 0);
                        const uint64_t pixel_count =
                            static_cast<uint64_t>(image->width_px) * image->height_px;
                        if (pixel_count > max_image_pixel_count) {
                            max_image_pixel_count = pixel_count;
                            max_image_width = image->width_px;
                            max_image_height = image->height_px;
                        }
                        total_decoded_image_bytes += image->decoded_pixel_bytes;
                        pikdraw::geometry::Transform2D placement;
                        if (image->has_placement_transform) {
                            const auto& m = image->placement_transform;
                            placement = {m[0], m[1], m[2], m[3], m[4], m[5]};
                        } else {
                            placement = pikdraw::geometry::Transform2D::translation(
                                image->x_mm, image->y_mm);
                        }
                        const double width = image->width_mm > 0.0
                            ? image->width_mm
                            : static_cast<double>(image->width_px) * 25.4 / image->dpi_x;
                        const double height = image->height_mm > 0.0
                            ? image->height_mm
                            : static_cast<double>(image->height_px) * 25.4 / image->dpi_y;
                        const std::array<pikdraw::geometry::Point2D, 4> corners{
                            placement.map_point({0.0, 0.0}), placement.map_point({width, 0.0}),
                            placement.map_point({width, height}), placement.map_point({0.0, height})};
                        double left = corners.front().x, right = left;
                        double bottom = corners.front().y, top = bottom;
                        for (const auto& point : corners) {
                            left = std::min(left, point.x);
                            right = std::max(right, point.x);
                            bottom = std::min(bottom, point.y);
                            top = std::max(top, point.y);
                        }
                        include_bounds(left, bottom, right, top, image_min_x, image_min_y,
                                       image_max_x, image_max_y);
                        include_bounds(left, bottom, right, top, content_min_x, content_min_y,
                                       content_max_x, content_max_y);
                    }
                }
            }
            if (page_count != 1 || layer_count == 0 || object_count == 0 || records.empty() ||
                records.back().type != ImportRecordType::FidelityReport) {
                std::cerr << "CDR adapter returned an incomplete editable proposal ["
                          << fixture_name << "]\n";
                ++failures;
                continue;
            }
            std::cout << "CDR adapter accepted [" << fixture_name << "]: "
                      << "page " << page_width_mm << 'x' << page_height_mm << " mm, "
                      << object_count << " editable objects, " << layer_count << " layers, "
                      << image_count << " embedded images, " << image_chunk_count
                      << " image-data chunks, " << arc_element_count << " arc segments, max raster " << max_image_width << 'x'
                      << max_image_height << " px, " << total_decoded_image_bytes
                      << " decoded bytes across placements, RAM admission calls="
                      << memory_admission_calls << ", largest request="
                      << largest_memory_admission << " bytes";
            if (std::isfinite(min_x) && std::isfinite(min_y) &&
                std::isfinite(max_x) && std::isfinite(max_y)) {
                std::cout << ", painted path bounds [" << min_x << ',' << min_y << "]..["
                          << max_x << ',' << max_y << "] mm";
            }
            if (std::isfinite(content_min_x) && std::isfinite(content_min_y) &&
                std::isfinite(content_max_x) && std::isfinite(content_max_y)) {
                std::cout << ", all path/image bounds [" << content_min_x << ',' << content_min_y
                          << "]..[" << content_max_x << ',' << content_max_y << "] mm";
            }
            if (std::isfinite(path_min_x) && std::isfinite(path_min_y) &&
                std::isfinite(path_max_x) && std::isfinite(path_max_y)) {
                std::cout << ", all path-only bounds [" << path_min_x << ',' << path_min_y
                          << "]..[" << path_max_x << ',' << path_max_y << "] mm";
            }
            if (std::isfinite(image_min_x) && std::isfinite(image_min_y) &&
                std::isfinite(image_max_x) && std::isfinite(image_max_y)) {
                std::cout << ", placed image bounds [" << image_min_x << ',' << image_min_y
                          << "]..[" << image_max_x << ',' << image_max_y << "] mm";
            }
            std::cout << ".\n";
            if (image_count != 0) {
                assert(image_chunk_count != 0);
                assert(source_chunk_count != 0);
                assert(memory_admission_calls > 0 && largest_memory_admission > 0);
            }
        }
        assert(fixture_count > 0);
        assert(failures == 0);
        return 0;
    }

    const wchar_t* fixture = _wgetenv(L"PIKDRAW_CDR_REAL_FIXTURE");
    if (fixture && *fixture) {
        std::ifstream input(std::filesystem::path(fixture), std::ios::binary);
        assert(input.good());
        std::vector<uint8_t> source((std::istreambuf_iterator<char>(input)), {});
        assert(!source.empty());
        const bool converted = BuildCdrImportRecordsAndReleaseSource(
            source, records, diagnostic);
        assert(source.empty());
        if (!converted) std::cerr << "Real CDR adapter rejected fixture: " << diagnostic << '\n';
        assert(converted);
        size_t page_count = 0, layer_count = 0, path_count = 0, text_count = 0;
        bool saw_layer_names = false, saw_fill = false, saw_stroke = false;
        bool saw_merged_text_runs = false;
        for (const auto& record : records) {
            const auto decoded = DeserializeImportRecord(record.type, record.payload);
            assert(decoded);
            if (std::holds_alternative<ImportPageRecord>(*decoded)) ++page_count;
            else if (const auto* layer = std::get_if<ImportLayerRecord>(&*decoded)) {
                ++layer_count;
                saw_layer_names |= layer->name.starts_with("Layer ");
            } else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) {
                ++path_count;
                saw_fill |= path->has_fill;
                saw_stroke |= path->has_stroke && path->stroke_width_mm > 0.0;
            } else if (const auto* text = std::get_if<ImportTextRecord>(&*decoded)) {
                ++text_count;
                saw_merged_text_runs |= text->utf8_text.find("km/hW=1111") != std::string::npos;
            }
            else assert(!std::holds_alternative<ImportRasterImageRecord>(*decoded));
        }
        assert(page_count == 1 && layer_count >= 2 && path_count > 0 && text_count > 0);
        assert(saw_layer_names && saw_fill && saw_stroke && saw_merged_text_runs);
        assert(records.back().type == ImportRecordType::FidelityReport);
        const auto report_value = DeserializeImportRecord(records.back().type, records.back().payload);
        const auto* report = report_value ? std::get_if<ImportFidelityReportRecord>(&*report_value) : nullptr;
        assert(report && report->coverage_profile == "cdr-libcdr-svg-v2");
        assert(report->coverage == ImportCoverage::Partial &&
               report->editability == ImportEditability::NativeEditable && report->issue_count > 0);
        std::cout << "Real CDR converted to " << path_count << " paths, " << text_count
                  << " text objects, and " << layer_count << " editable layers.\n";
    } else {
        std::cout << "PIKDRAW_CDR_REAL_FIXTURE is unset; real CDR conversion was not run.\n";
    }
#endif
    return 0;
}
