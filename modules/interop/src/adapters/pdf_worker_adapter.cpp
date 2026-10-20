#include "pikdraw/interop/adapters/pdf_worker_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#endif

namespace pikdraw::interop {
namespace {

constexpr int kMaxPageObjects = static_cast<int>(IPC_MAX_OBJECTS);
constexpr int kMaxPathSegmentsPerObject = 32768;
constexpr size_t kMaxProposalRecords = IPC_MAX_OBJECTS;
constexpr uint64_t kMaxDecodedImageBytes = UINT32_MAX;
constexpr uint32_t kMaxImagePixels = UINT32_MAX / 4;
constexpr uint32_t kRasterChunkBytes = 256u * 1024u;
constexpr double kPdfPointsToMillimetres = 25.4 / 72.0;
constexpr double kMatrixTolerance = 1e-5;

bool IsFinitePositive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

#ifdef _WIN32

struct PdfiumApi {
    using InitLibrary = decltype(&FPDF_InitLibrary);
    using DestroyLibrary = decltype(&FPDF_DestroyLibrary);
    using LoadMemDocument64 = decltype(&FPDF_LoadMemDocument64);
    using CloseDocument = decltype(&FPDF_CloseDocument);
    using GetPageCount = decltype(&FPDF_GetPageCount);
    using LoadPage = decltype(&FPDF_LoadPage);
    using ClosePage = decltype(&FPDF_ClosePage);
    using GetPageBoundingBox = decltype(&FPDF_GetPageBoundingBox);
    using GetPageRotation = decltype(&FPDFPage_GetRotation);
    using CountObjects = decltype(&FPDFPage_CountObjects);
    using GetObject = decltype(&FPDFPage_GetObject);
    using GetObjectType = decltype(&FPDFPageObj_GetType);
    using GetObjectMatrix = decltype(&FPDFPageObj_GetMatrix);
    using GetObjectBounds = decltype(&FPDFPageObj_GetBounds);
    using CountFormObjects = decltype(&FPDFFormObj_CountObjects);
    using GetFormObject = decltype(&FPDFFormObj_GetObject);
    using HasObjectTransparency = decltype(&FPDFPageObj_HasTransparency);
    using GetImageMetadata = decltype(&FPDFImageObj_GetImageMetadata);
    using GetRenderedBitmap = decltype(&FPDFImageObj_GetRenderedBitmap);
    using DestroyBitmap = decltype(&FPDFBitmap_Destroy);
    using GetBitmapWidth = decltype(&FPDFBitmap_GetWidth);
    using GetBitmapHeight = decltype(&FPDFBitmap_GetHeight);
    using GetBitmapStride = decltype(&FPDFBitmap_GetStride);
    using GetBitmapFormat = decltype(&FPDFBitmap_GetFormat);
    using GetBitmapBuffer = decltype(&FPDFBitmap_GetBuffer);
    using GetFillColor = decltype(&FPDFPageObj_GetFillColor);
    using GetStrokeColor = decltype(&FPDFPageObj_GetStrokeColor);
    using GetStrokeWidth = decltype(&FPDFPageObj_GetStrokeWidth);
    using GetPathDrawMode = decltype(&FPDFPath_GetDrawMode);
    using CountPathSegments = decltype(&FPDFPath_CountSegments);
    using GetPathSegment = decltype(&FPDFPath_GetPathSegment);
    using GetSegmentPoint = decltype(&FPDFPathSegment_GetPoint);
    using GetSegmentType = decltype(&FPDFPathSegment_GetType);
    using GetSegmentClose = decltype(&FPDFPathSegment_GetClose);
    using GetBezierControlPoints = decltype(&FPDFPath_GetBezierControlPoints);
    using LoadTextPage = decltype(&FPDFText_LoadPage);
    using CloseTextPage = decltype(&FPDFText_ClosePage);
    using GetText = decltype(&FPDFTextObj_GetText);
    using GetTextFontSize = decltype(&FPDFTextObj_GetFontSize);
    using GetTextRenderMode = decltype(&FPDFTextObj_GetTextRenderMode);
    using GetTextFont = decltype(&FPDFTextObj_GetFont);
    using GetFontFamilyName = decltype(&FPDFFont_GetFamilyName);
    using GetFontBaseName = decltype(&FPDFFont_GetBaseFontName);
    using GetFontFlags = decltype(&FPDFFont_GetFlags);
    using GetFontWeight = decltype(&FPDFFont_GetWeight);
    using CountMarks = decltype(&FPDFPageObj_CountMarks);
    using GetMark = decltype(&FPDFPageObj_GetMark);
    using GetMarkName = decltype(&FPDFPageObjMark_GetName);
    using GetMarkParamStringValue = decltype(&FPDFPageObjMark_GetParamStringValue);

    HMODULE module{nullptr};
    bool initialized{false};
    InitLibrary init_library{nullptr};
    DestroyLibrary destroy_library{nullptr};
    LoadMemDocument64 load_mem_document64{nullptr};
    CloseDocument close_document{nullptr};
    GetPageCount get_page_count{nullptr};
    LoadPage load_page{nullptr};
    ClosePage close_page{nullptr};
    GetPageBoundingBox get_page_bounding_box{nullptr};
    GetPageRotation get_page_rotation{nullptr};
    CountObjects count_objects{nullptr};
    GetObject get_object{nullptr};
    GetObjectType get_object_type{nullptr};
    GetObjectMatrix get_object_matrix{nullptr};
    GetObjectBounds get_object_bounds{nullptr};
    CountFormObjects count_form_objects{nullptr};
    GetFormObject get_form_object{nullptr};
    HasObjectTransparency has_object_transparency{nullptr};
    GetImageMetadata get_image_metadata{nullptr};
    GetRenderedBitmap get_rendered_bitmap{nullptr};
    DestroyBitmap destroy_bitmap{nullptr};
    GetBitmapWidth get_bitmap_width{nullptr};
    GetBitmapHeight get_bitmap_height{nullptr};
    GetBitmapStride get_bitmap_stride{nullptr};
    GetBitmapFormat get_bitmap_format{nullptr};
    GetBitmapBuffer get_bitmap_buffer{nullptr};
    GetFillColor get_fill_color{nullptr};
    GetStrokeColor get_stroke_color{nullptr};
    GetStrokeWidth get_stroke_width{nullptr};
    GetPathDrawMode get_path_draw_mode{nullptr};
    CountPathSegments count_path_segments{nullptr};
    GetPathSegment get_path_segment{nullptr};
    GetSegmentPoint get_segment_point{nullptr};
    GetSegmentType get_segment_type{nullptr};
    GetSegmentClose get_segment_close{nullptr};
    GetBezierControlPoints get_bezier_control_points{nullptr};
    LoadTextPage load_text_page{nullptr};
    CloseTextPage close_text_page{nullptr};
    GetText get_text{nullptr};
    GetTextFontSize get_text_font_size{nullptr};
    GetTextRenderMode get_text_render_mode{nullptr};
    GetTextFont get_text_font{nullptr};
    GetFontFamilyName get_font_family_name{nullptr};
    GetFontBaseName get_font_base_name{nullptr};
    GetFontFlags get_font_flags{nullptr};
    GetFontWeight get_font_weight{nullptr};
    CountMarks count_marks{nullptr};
    GetMark get_mark{nullptr};
    GetMarkName get_mark_name{nullptr};
    GetMarkParamStringValue get_mark_param_string_value{nullptr};

    PdfiumApi() noexcept {
        wchar_t executable_path[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable_path,
                                                static_cast<DWORD>(std::size(executable_path)));
        if (length == 0 || length >= std::size(executable_path)) return;
        const auto library_path =
            std::filesystem::path(executable_path).parent_path() / L"pdfium.dll";
        module = LoadLibraryExW(library_path.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return;

#define PIKDRAW_PDFIUM_LOAD(member, symbol) \
    member = reinterpret_cast<decltype(member)>(GetProcAddress(module, symbol)); \
    if (!member) return
        PIKDRAW_PDFIUM_LOAD(init_library, "FPDF_InitLibrary");
        PIKDRAW_PDFIUM_LOAD(destroy_library, "FPDF_DestroyLibrary");
        PIKDRAW_PDFIUM_LOAD(load_mem_document64, "FPDF_LoadMemDocument64");
        PIKDRAW_PDFIUM_LOAD(close_document, "FPDF_CloseDocument");
        PIKDRAW_PDFIUM_LOAD(get_page_count, "FPDF_GetPageCount");
        PIKDRAW_PDFIUM_LOAD(load_page, "FPDF_LoadPage");
        PIKDRAW_PDFIUM_LOAD(close_page, "FPDF_ClosePage");
        PIKDRAW_PDFIUM_LOAD(get_page_bounding_box, "FPDF_GetPageBoundingBox");
        PIKDRAW_PDFIUM_LOAD(get_page_rotation, "FPDFPage_GetRotation");
        PIKDRAW_PDFIUM_LOAD(count_objects, "FPDFPage_CountObjects");
        PIKDRAW_PDFIUM_LOAD(get_object, "FPDFPage_GetObject");
        PIKDRAW_PDFIUM_LOAD(get_object_type, "FPDFPageObj_GetType");
        PIKDRAW_PDFIUM_LOAD(get_object_matrix, "FPDFPageObj_GetMatrix");
        PIKDRAW_PDFIUM_LOAD(get_object_bounds, "FPDFPageObj_GetBounds");
        PIKDRAW_PDFIUM_LOAD(count_form_objects, "FPDFFormObj_CountObjects");
        PIKDRAW_PDFIUM_LOAD(get_form_object, "FPDFFormObj_GetObject");
        PIKDRAW_PDFIUM_LOAD(has_object_transparency, "FPDFPageObj_HasTransparency");
        PIKDRAW_PDFIUM_LOAD(get_image_metadata, "FPDFImageObj_GetImageMetadata");
        PIKDRAW_PDFIUM_LOAD(get_rendered_bitmap, "FPDFImageObj_GetRenderedBitmap");
        PIKDRAW_PDFIUM_LOAD(destroy_bitmap, "FPDFBitmap_Destroy");
        PIKDRAW_PDFIUM_LOAD(get_bitmap_width, "FPDFBitmap_GetWidth");
        PIKDRAW_PDFIUM_LOAD(get_bitmap_height, "FPDFBitmap_GetHeight");
        PIKDRAW_PDFIUM_LOAD(get_bitmap_stride, "FPDFBitmap_GetStride");
        PIKDRAW_PDFIUM_LOAD(get_bitmap_format, "FPDFBitmap_GetFormat");
        PIKDRAW_PDFIUM_LOAD(get_bitmap_buffer, "FPDFBitmap_GetBuffer");
        PIKDRAW_PDFIUM_LOAD(get_fill_color, "FPDFPageObj_GetFillColor");
        PIKDRAW_PDFIUM_LOAD(get_stroke_color, "FPDFPageObj_GetStrokeColor");
        PIKDRAW_PDFIUM_LOAD(get_stroke_width, "FPDFPageObj_GetStrokeWidth");
        PIKDRAW_PDFIUM_LOAD(get_path_draw_mode, "FPDFPath_GetDrawMode");
        PIKDRAW_PDFIUM_LOAD(count_path_segments, "FPDFPath_CountSegments");
        PIKDRAW_PDFIUM_LOAD(get_path_segment, "FPDFPath_GetPathSegment");
        PIKDRAW_PDFIUM_LOAD(get_segment_point, "FPDFPathSegment_GetPoint");
        PIKDRAW_PDFIUM_LOAD(get_segment_type, "FPDFPathSegment_GetType");
        PIKDRAW_PDFIUM_LOAD(get_segment_close, "FPDFPathSegment_GetClose");
        PIKDRAW_PDFIUM_LOAD(get_bezier_control_points, "FPDFPath_GetBezierControlPoints");
        PIKDRAW_PDFIUM_LOAD(load_text_page, "FPDFText_LoadPage");
        PIKDRAW_PDFIUM_LOAD(close_text_page, "FPDFText_ClosePage");
        PIKDRAW_PDFIUM_LOAD(get_text, "FPDFTextObj_GetText");
        PIKDRAW_PDFIUM_LOAD(get_text_font_size, "FPDFTextObj_GetFontSize");
        PIKDRAW_PDFIUM_LOAD(get_text_render_mode, "FPDFTextObj_GetTextRenderMode");
        PIKDRAW_PDFIUM_LOAD(get_text_font, "FPDFTextObj_GetFont");
        PIKDRAW_PDFIUM_LOAD(get_font_family_name, "FPDFFont_GetFamilyName");
        PIKDRAW_PDFIUM_LOAD(get_font_base_name, "FPDFFont_GetBaseFontName");
        PIKDRAW_PDFIUM_LOAD(get_font_flags, "FPDFFont_GetFlags");
        PIKDRAW_PDFIUM_LOAD(get_font_weight, "FPDFFont_GetWeight");
#undef PIKDRAW_PDFIUM_LOAD
        count_marks = reinterpret_cast<CountMarks>(GetProcAddress(module, "FPDFPageObj_CountMarks"));
        get_mark = reinterpret_cast<GetMark>(GetProcAddress(module, "FPDFPageObj_GetMark"));
        get_mark_name = reinterpret_cast<GetMarkName>(GetProcAddress(module, "FPDFPageObjMark_GetName"));
        get_mark_param_string_value = reinterpret_cast<GetMarkParamStringValue>(
            GetProcAddress(module, "FPDFPageObjMark_GetParamStringValue"));
        init_library();
        initialized = true;
    }

    ~PdfiumApi() {
        if (initialized && destroy_library) destroy_library();
        if (module) FreeLibrary(module);
    }

    [[nodiscard]] bool ready() const noexcept { return initialized; }
};

struct DocumentGuard {
    PdfiumApi& api;
    FPDF_DOCUMENT value{nullptr};
    ~DocumentGuard() { if (value) api.close_document(value); }
};

struct PageGuard {
    PdfiumApi& api;
    FPDF_PAGE value{nullptr};
    ~PageGuard() { if (value) api.close_page(value); }
};

struct TextPageGuard {
    PdfiumApi& api;
    FPDF_TEXTPAGE value{nullptr};
    ~TextPageGuard() { if (value) api.close_text_page(value); }
};

struct PdfPageBounds {
    double left{0.0};
    double bottom{0.0};
    double right{0.0};
    double top{0.0};
    uint8_t rotation_quarters_clockwise{0};
};

FS_MATRIX Multiply(const FS_MATRIX& outer, const FS_MATRIX& inner) noexcept {
    return {
        outer.a * inner.a + outer.c * inner.b,
        outer.b * inner.a + outer.d * inner.b,
        outer.a * inner.c + outer.c * inner.d,
        outer.b * inner.c + outer.d * inner.d,
        outer.a * inner.e + outer.c * inner.f + outer.e,
        outer.b * inner.e + outer.d * inner.f + outer.f};
}

Point2D TransformPoint(const FS_MATRIX& matrix, double x, double y) noexcept {
    return {matrix.a * x + matrix.c * y + matrix.e,
            matrix.b * x + matrix.d * y + matrix.f};
}

bool IsSupportedMatrix(const FS_MATRIX& matrix, bool allow_translation,
                       double& uniform_scale) noexcept {
    const double sx = std::hypot(static_cast<double>(matrix.a), static_cast<double>(matrix.b));
    const double sy = std::hypot(static_cast<double>(matrix.c), static_cast<double>(matrix.d));
    const double dot = static_cast<double>(matrix.a) * matrix.c +
                       static_cast<double>(matrix.b) * matrix.d;
    if (!IsFinitePositive(sx) || !IsFinitePositive(sy) ||
        !std::isfinite(matrix.e) || !std::isfinite(matrix.f) ||
        (!allow_translation && (std::abs(matrix.e) > kMatrixTolerance ||
                                std::abs(matrix.f) > kMatrixTolerance)) ||
        std::abs(sx - sy) > kMatrixTolerance * (std::max)(sx, sy) ||
        std::abs(dot) > kMatrixTolerance * sx * sy) {
        return false;
    }
    uniform_scale = (sx + sy) * 0.5;
    return IsFinitePositive(uniform_scale);
}

Point2D PagePointToMillimetres(double page_x, double page_y,
                               const PdfPageBounds& bounds) noexcept {
    const double x = (page_x - bounds.left) * kPdfPointsToMillimetres;
    const double y = (page_y - bounds.bottom) * kPdfPointsToMillimetres;
    const double width = (bounds.right - bounds.left) * kPdfPointsToMillimetres;
    const double height = (bounds.top - bounds.bottom) * kPdfPointsToMillimetres;
    switch (bounds.rotation_quarters_clockwise) {
    case 0: return {x, y};
    case 1: return {y, width - x};
    case 2: return {width - x, height - y};
    case 3: return {height - y, x};
    default:
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
}

Point2D ToMillimetres(const FS_MATRIX& matrix, double x, double y,
                      const PdfPageBounds& bounds) noexcept {
    const Point2D page_point = TransformPoint(matrix, x, y);
    return PagePointToMillimetres(page_point.x, page_point.y, bounds);
}

bool AddRecord(std::vector<CanonicalImportRecord>& records,
               const ImportRecordValue& value) {
    auto encoded = SerializeImportRecord(value);
    if (!encoded || encoded->payload.size() > IPC_V2_MAX_RECORD_BYTES) return false;
    records.push_back(std::move(*encoded));
    return records.size() <= kMaxProposalRecords;
}

ImportColorRecord ReadColor(PdfiumApi& api, FPDF_PAGEOBJECT object,
                            bool stroke, bool& success) {
    unsigned int r = 0, g = 0, b = 0, a = 255;
    success = stroke
        ? api.get_stroke_color(object, &r, &g, &b, &a) != 0
        : api.get_fill_color(object, &r, &g, &b, &a) != 0;
    ImportColorRecord color;
    color.model = ImportColorModel::Rgb;
    color.components = {r / 255.0, g / 255.0, b / 255.0, a / 255.0, 0.0};
    return color;
}

bool AddPathRecord(PdfiumApi& api, FPDF_PAGEOBJECT object,
                   uint32_t local_id, std::string source_id,
                   const PdfPageBounds& bounds,
                   const FS_MATRIX& parent_matrix,
                   ImportPathRecord& record, std::string& diagnostic) {
    FS_MATRIX object_matrix{};
    if (!api.get_object_matrix(object, &object_matrix)) {
        diagnostic = "PDF path transform is not readable";
        return false;
    }
    const FS_MATRIX matrix = Multiply(parent_matrix, object_matrix);
    double transform_scale = 0.0;
    if (!IsSupportedMatrix(matrix, true, transform_scale)) {
        diagnostic = "PDF path has non-uniform scale or shear that PikDraw cannot preserve yet";
        return false;
    }

    int fill_mode = FPDF_FILLMODE_NONE;
    FPDF_BOOL stroke = 0;
    if (!api.get_path_draw_mode(object, &fill_mode, &stroke)) {
        diagnostic = "PDF path paint mode is not readable";
        return false;
    }
    if (fill_mode != FPDF_FILLMODE_NONE &&
        fill_mode != FPDF_FILLMODE_ALTERNATE &&
        fill_mode != FPDF_FILLMODE_WINDING) {
        diagnostic = "PDF path uses an unsupported fill rule";
        return false;
    }

    record.local_id = local_id;
    record.source_id = std::move(source_id);
    record.name = "PDF Path " + std::to_string(local_id);
    record.winding_rule = fill_mode == FPDF_FILLMODE_ALTERNATE ? 1 : 0;
    record.has_fill = fill_mode != FPDF_FILLMODE_NONE;
    record.has_stroke = stroke != 0;

    if (record.has_fill) {
        bool color_ok = false;
        record.fill = ReadColor(api, object, false, color_ok);
        if (!color_ok) {
            diagnostic = "PDF path fill color is not readable";
            return false;
        }
    }
    if (record.has_stroke) {
        bool color_ok = false;
        record.stroke = ReadColor(api, object, true, color_ok);
        float width_pt = 0.0f;
        if (!color_ok || !api.get_stroke_width(object, &width_pt) ||
            !std::isfinite(width_pt) || width_pt < 0.0f) {
            diagnostic = "PDF path stroke style is not readable";
            return false;
        }
        record.stroke_width_mm = width_pt * transform_scale * kPdfPointsToMillimetres;
    }

    const int segment_count = api.count_path_segments(object);
    if (segment_count <= 0 || segment_count > kMaxPathSegmentsPerObject) {
        diagnostic = "PDF path is empty or exceeds the bounded segment profile";
        return false;
    }
    record.elements.reserve(static_cast<size_t>(segment_count) * 2);
    size_t consecutive_bezier_points = 0;
    for (int index = 0; index < segment_count; ++index) {
        const FPDF_PATHSEGMENT segment = api.get_path_segment(object, index);
        float x = 0.0f, y = 0.0f;
        if (!segment || !api.get_segment_point(segment, &x, &y) ||
            !std::isfinite(x) || !std::isfinite(y)) {
            diagnostic = "PDF path segment coordinates are invalid";
            return false;
        }

        ImportPathElementRecord element;
        const int type = api.get_segment_type(segment);
        const Point2D endpoint = ToMillimetres(matrix, x, y, bounds);
        if (!endpoint.IsFinite()) {
            diagnostic = "PDF path transform produced non-finite coordinates";
            return false;
        }
        if (type == FPDF_SEGMENT_MOVETO) {
            consecutive_bezier_points = 0;
            element.verb = ImportPathVerb::Move;
            element.values[0] = endpoint.x;
            element.values[1] = endpoint.y;
        } else if (type == FPDF_SEGMENT_LINETO) {
            consecutive_bezier_points = 0;
            element.verb = ImportPathVerb::Line;
            element.values[0] = endpoint.x;
            element.values[1] = endpoint.y;
        } else if (type == FPDF_SEGMENT_BEZIERTO) {
            ++consecutive_bezier_points;
            if (consecutive_bezier_points % 3 != 0) continue;
            FS_POINTF control1{}, control2{};
            if (!api.get_bezier_control_points(object, static_cast<size_t>(index),
                                               &control1, &control2)) {
                diagnostic = "PDF cubic Bezier control points are not readable";
                return false;
            }
            const Point2D first = ToMillimetres(matrix, control1.x, control1.y, bounds);
            const Point2D second = ToMillimetres(matrix, control2.x, control2.y, bounds);
            if (!first.IsFinite() || !second.IsFinite()) {
                diagnostic = "PDF Bezier transform produced non-finite coordinates";
                return false;
            }
            element.verb = ImportPathVerb::Cubic;
            element.values[0] = endpoint.x;
            element.values[1] = endpoint.y;
            element.values[2] = first.x;
            element.values[3] = first.y;
            element.values[4] = second.x;
            element.values[5] = second.y;
        } else {
            consecutive_bezier_points = 0;
            diagnostic = "PDF contains an unknown path segment type";
            return false;
        }
        record.elements.push_back(element);
        if (api.get_segment_close(segment)) {
            ImportPathElementRecord close;
            close.verb = ImportPathVerb::Close;
            record.elements.push_back(close);
        }
    }
    if (consecutive_bezier_points % 3 != 0) {
        diagnostic = "PDF path ends with an incomplete cubic Bezier segment";
        return false;
    }
    return true;
}

template <typename GetFontName>
std::string ReadFontString(FPDF_FONT font, GetFontName get_name) {
    if (!font) return {};
    const size_t size = get_name(font, nullptr, 0);
    if (size == 0 || size > 1024) return {};
    std::string result(size, '\0');
    const size_t copied = get_name(font, result.data(), result.size());
    if (copied == 0 || copied > result.size()) return {};
    result.resize(copied > 0 && result[copied - 1] == '\0' ? copied - 1 : copied);
    return result;
}

bool Utf16ToUtf8(const FPDF_WCHAR* text, size_t units, std::string& output) {
    output.clear();
    if (!text || units == 0 || units > IPC_V2_MAX_RECORD_BYTES / sizeof(FPDF_WCHAR) ||
        units > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    size_t swapped_units = 0;
    size_t nonzero_low_bytes = 0;
    for (size_t index = 0; index < units; ++index) {
        const uint16_t value = text[index];
        if ((value & 0x00ffu) == 0 && (value & 0xff00u) != 0) ++swapped_units;
        if ((value & 0x00ffu) != 0) ++nonzero_low_bytes;
    }
    const bool swap_bytes = text[0] == 0xfffeu ||
        (swapped_units > 0 && swapped_units > nonzero_low_bytes);
    const auto code_unit = [&](size_t index) {
        const uint16_t value = text[index];
        return swap_bytes ? static_cast<uint16_t>((value >> 8) | (value << 8)) : value;
    };
    size_t index = code_unit(0) == 0xfeffu ? 1u : 0u;
    for (; index < units; ++index) {
        uint32_t codepoint = code_unit(index);
        if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
            if (index + 1 >= units) return false;
            const uint32_t low = code_unit(++index);
            if (low < 0xdc00u || low > 0xdfffu) return false;
            codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
        } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
            return false;
        }
        if (codepoint == 0) return false;
        if (codepoint <= 0x7fu) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffu) {
            output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu) {
            output.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            output.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
        if (output.size() > IPC_V2_MAX_RECORD_BYTES) return false;
    }
    return !output.empty();
}

int PdfHexDigit(unsigned char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool DecodePdfStringBytes(std::span<const uint8_t> bytes, std::string& output) {
    output.clear();
    if (bytes.size() >= 2 &&
        ((bytes[0] == 0xfe && bytes[1] == 0xff) || (bytes[0] == 0xff && bytes[1] == 0xfe))) {
        const bool little_endian = bytes[0] == 0xff;
        if ((bytes.size() - 2) % 2 != 0) return false;
        std::vector<FPDF_WCHAR> units;
        units.reserve((bytes.size() - 2) / 2);
        for (size_t index = 2; index < bytes.size(); index += 2) {
            const uint16_t value = little_endian
                ? static_cast<uint16_t>(bytes[index] | (static_cast<uint16_t>(bytes[index + 1]) << 8))
                : static_cast<uint16_t>((static_cast<uint16_t>(bytes[index]) << 8) | bytes[index + 1]);
            units.push_back(value);
        }
        return Utf16ToUtf8(units.data(), units.size(), output);
    }
    for (const uint8_t byte : bytes) {
        const uint32_t codepoint = byte;
        if (codepoint <= 0x7fu) output.push_back(static_cast<char>(codepoint));
        else {
            output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
    }
    return !output.empty();
}

std::vector<std::string> ExtractPdfOptionalContentNames(std::span<const uint8_t> source) {
    std::vector<std::string> names;
    const std::string_view bytes(reinterpret_cast<const char*>(source.data()), source.size());
    std::unordered_set<std::string> seen;
    size_t cursor = 0;
    while (cursor < bytes.size() && names.size() < IPC_MAX_OBJECTS) {
        const size_t type = bytes.find("/Type", cursor);
        if (type == std::string_view::npos) break;
        cursor = type + 5;
        while (cursor < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[cursor]))) ++cursor;
        if (bytes.substr(cursor, 4) != "/OCG") continue;
        const size_t dictionary_end = bytes.find(">>", cursor + 4);
        const size_t name_key = bytes.find("/Name", cursor + 4);
        if (dictionary_end == std::string_view::npos || dictionary_end - cursor > 4096 ||
            name_key == std::string_view::npos || name_key > dictionary_end) continue;
        size_t value = name_key + 5;
        while (value < dictionary_end && std::isspace(static_cast<unsigned char>(bytes[value]))) ++value;
        std::vector<uint8_t> decoded;
        if (value < dictionary_end && bytes[value] == '<' &&
            (value + 1 >= dictionary_end || bytes[value + 1] != '<')) {
            ++value;
            int high_nibble = -1;
            while (value < dictionary_end && bytes[value] != '>') {
                const unsigned char ch = static_cast<unsigned char>(bytes[value++]);
                if (std::isspace(ch)) continue;
                const int digit = PdfHexDigit(ch);
                if (digit < 0) { decoded.clear(); break; }
                if (high_nibble < 0) high_nibble = digit;
                else {
                    decoded.push_back(static_cast<uint8_t>((high_nibble << 4) | digit));
                    high_nibble = -1;
                }
            }
            if (high_nibble >= 0) decoded.push_back(static_cast<uint8_t>(high_nibble << 4));
        } else if (value < dictionary_end && bytes[value] == '(') {
            ++value;
            size_t depth = 1;
            while (value < dictionary_end && depth > 0) {
                unsigned char ch = static_cast<unsigned char>(bytes[value++]);
                if (ch == '\\' && value < dictionary_end) {
                    const unsigned char escaped = static_cast<unsigned char>(bytes[value++]);
                    if (escaped == 'n') ch = '\n';
                    else if (escaped == 'r') ch = '\r';
                    else if (escaped == 't') ch = '\t';
                    else if (escaped == 'b') ch = '\b';
                    else if (escaped == 'f') ch = '\f';
                    else if (escaped == '\r' || escaped == '\n') continue;
                    else ch = escaped;
                } else if (ch == '(') { ++depth; }
                else if (ch == ')' && --depth == 0) break;
                decoded.push_back(ch);
            }
        }
        std::string name;
        if (DecodePdfStringBytes(decoded, name) && name.size() <= 128 && seen.insert(name).second)
            names.push_back(std::move(name));
    }
    return names;
}

bool ReadPdfMarkName(PdfiumApi& api, FPDF_PAGEOBJECTMARK mark, std::string& name) {
    if (!api.get_mark_name) return false;
    unsigned long required_bytes = 0;
    if (!api.get_mark_name(mark, nullptr, 0, &required_bytes) || required_bytes == 0 ||
        required_bytes % sizeof(FPDF_WCHAR) != 0 || required_bytes > IPC_V2_MAX_RECORD_BYTES) return false;
    std::vector<FPDF_WCHAR> value(required_bytes / sizeof(FPDF_WCHAR));
    unsigned long actual_bytes = 0;
    if (!api.get_mark_name(mark, value.data(), required_bytes, &actual_bytes) ||
        actual_bytes != required_bytes || value.empty() || value.back() != 0) return false;
    return Utf16ToUtf8(value.data(), value.size() - 1, name);
}

bool ReadPdfMarkStringParameter(PdfiumApi& api, FPDF_PAGEOBJECTMARK mark,
                               const char* key, std::string& value) {
    if (!api.get_mark_param_string_value) return false;
    unsigned long required_bytes = 0;
    if (!api.get_mark_param_string_value(mark, key, nullptr, 0, &required_bytes) ||
        required_bytes == 0 || required_bytes % sizeof(FPDF_WCHAR) != 0 ||
        required_bytes > IPC_V2_MAX_RECORD_BYTES) return false;
    std::vector<FPDF_WCHAR> raw(required_bytes / sizeof(FPDF_WCHAR));
    unsigned long actual_bytes = 0;
    if (!api.get_mark_param_string_value(mark, key, raw.data(), required_bytes, &actual_bytes) ||
        actual_bytes != required_bytes || raw.empty() || raw.back() != 0) return false;
    return Utf16ToUtf8(raw.data(), raw.size() - 1, value);
}

std::optional<std::string> ReadPdfObjectOptionalContentName(
    PdfiumApi& api, FPDF_PAGEOBJECT object, bool& has_optional_content) {
    has_optional_content = false;
    if (!api.count_marks || !api.get_mark || !api.get_mark_name) return std::nullopt;
    const int mark_count = api.count_marks(object);
    if (mark_count <= 0 || mark_count > static_cast<int>(IPC_MAX_OBJECTS)) return std::nullopt;
    for (int index = 0; index < mark_count; ++index) {
        FPDF_PAGEOBJECTMARK mark = api.get_mark(object, static_cast<unsigned long>(index));
        std::string mark_name;
        if (!mark || !ReadPdfMarkName(api, mark, mark_name)) continue;
        if (mark_name != "OC") continue;
        has_optional_content = true;
        std::string layer_name;
        if (!ReadPdfMarkStringParameter(api, mark, "Name", layer_name) || layer_name.empty())
            return std::nullopt;
        return layer_name;
    }
    return std::nullopt;
}

bool AddTextRecord(PdfiumApi& api, FPDF_PAGEOBJECT object,
                   FPDF_TEXTPAGE text_page, uint32_t local_id,
                   std::string source_id, const PdfPageBounds& bounds,
                   const FS_MATRIX& parent_matrix,
                   ImportTextRecord& record, std::string& diagnostic) {
    if (api.get_text_render_mode(object) != FPDF_TEXTRENDERMODE_FILL) {
        diagnostic = "PDF text uses stroke, clipping, or invisible rendering that PikDraw cannot preserve yet";
        return false;
    }
    FS_MATRIX object_matrix{};
    float font_size_pt = 0.0f;
    if (!api.get_object_matrix(object, &object_matrix) ||
        !api.get_text_font_size(object, &font_size_pt) ||
        !std::isfinite(font_size_pt) || font_size_pt <= 0.0f) {
        diagnostic = "PDF text transform or font size is not readable";
        return false;
    }
    const FS_MATRIX matrix = Multiply(parent_matrix, object_matrix);
    double transform_scale = 0.0;
    if (!IsSupportedMatrix(matrix, true, transform_scale) ||
        std::abs(matrix.b) > kMatrixTolerance ||
        std::abs(matrix.c) > kMatrixTolerance || matrix.a <= 0.0f || matrix.d <= 0.0f) {
        diagnostic = "Rotated, mirrored, or sheared PDF text cannot be represented as editable PikDraw text yet";
        return false;
    }

    const unsigned long length = api.get_text(object, text_page, nullptr, 0);
    if (length < sizeof(FPDF_WCHAR) || length % sizeof(FPDF_WCHAR) != 0 ||
        length > 64 * 1024) {
        diagnostic = "PDF text is empty or exceeds the bounded text profile";
        return false;
    }
    std::vector<FPDF_WCHAR> text(static_cast<size_t>(length) / sizeof(FPDF_WCHAR));
    if (api.get_text(object, text_page, text.data(), length) != length || text.empty() ||
        text.back() != 0) {
        diagnostic = "PDF text could not be decoded as UTF-16";
        return false;
    }
    const size_t text_units = text.size() - 1;
    if (text_units == 0 || !Utf16ToUtf8(text.data(), text_units, record.utf8_text)) {
        diagnostic = "PDF text contains invalid Unicode";
        return false;
    }

    bool color_ok = false;
    record.fill = ReadColor(api, object, false, color_ok);
    if (!color_ok) {
        diagnostic = "PDF text fill color is not readable";
        return false;
    }
    FPDF_FONT font = api.get_text_font(object);
    record.local_id = local_id;
    record.source_id = std::move(source_id);
    record.name = "PDF Text " + std::to_string(local_id);
    record.font_family = ReadFontString(font, api.get_font_family_name);
    record.font_postscript_name = ReadFontString(font, api.get_font_base_name);
    if (record.font_family.empty()) record.font_family = record.font_postscript_name;
    if (record.font_family.empty()) {
        diagnostic = "PDF text font identity is unavailable; implicit font substitution is not allowed";
        return false;
    }
    const int weight = font ? api.get_font_weight(font) : -1;
    record.font_weight = weight >= 100 && weight <= 900
        ? static_cast<uint16_t>(weight) : 400;
    const int flags = font ? api.get_font_flags(font) : -1;
    record.italic = flags >= 0 && (flags & (1 << 6)) != 0;
    record.rotation_degrees = static_cast<uint16_t>(
        ((4u - bounds.rotation_quarters_clockwise) % 4u) * 90u);
    record.font_size_pt = font_size_pt * transform_scale;
    const Point2D origin = PagePointToMillimetres(matrix.e, matrix.f, bounds);
    record.x_mm = origin.x;
    record.y_mm = origin.y;
    if (!IsFinitePositive(record.font_size_pt) ||
        !std::isfinite(record.x_mm) || !std::isfinite(record.y_mm)) {
        diagnostic = "PDF text transform produced invalid native coordinates";
        return false;
    }
    return true;
}

bool AddImageRecords(PdfiumApi& api, FPDF_DOCUMENT document, FPDF_PAGE page,
                     FPDF_PAGEOBJECT object, uint32_t local_id, std::string source_id,
                     const PdfPageBounds& page_bounds, const FS_MATRIX& parent_matrix,
                     uint64_t& decoded_image_bytes,
                     std::vector<ImportRecordValue>& records, std::string& diagnostic) {
    FPDF_IMAGEOBJ_METADATA metadata{};
    if (!api.get_image_metadata(object, page, &metadata)) {
        diagnostic = "PDF image dimensions and source metadata are not readable";
        return false;
    }
    FPDF_BITMAP bitmap = api.get_rendered_bitmap(document, page, object);
    if (!bitmap) {
        diagnostic = "PDF image pixels or soft mask could not be decoded";
        return false;
    }
    struct BitmapGuard {
        PdfiumApi& api;
        FPDF_BITMAP value;
        ~BitmapGuard() { if (value) api.destroy_bitmap(value); }
    } bitmap_guard{api, bitmap};

    const int width = api.get_bitmap_width(bitmap);
    const int height = api.get_bitmap_height(bitmap);
    const int stride = api.get_bitmap_stride(bitmap);
    const int format = api.get_bitmap_format(bitmap);
    const void* raw_buffer = api.get_bitmap_buffer(bitmap);
    const uint64_t pixel_count_64 = width > 0 && height > 0
        ? static_cast<uint64_t>(width) * static_cast<uint64_t>(height) : 0;
    if (pixel_count_64 == 0 || pixel_count_64 > kMaxImagePixels ||
        pixel_count_64 > UINT32_MAX || !raw_buffer || stride <= 0) {
        diagnostic = "PDF image exceeds the wire-format addressable pixel range";
        return false;
    }
    const uint32_t pixel_count = static_cast<uint32_t>(pixel_count_64);
    const size_t bytes_per_pixel = format == FPDFBitmap_Gray ? 1u
        : format == FPDFBitmap_BGR ? 3u
        : format == FPDFBitmap_BGRx || format == FPDFBitmap_BGRA ||
          format == FPDFBitmap_BGRA_Premul ? 4u : 0u;
    if (bytes_per_pixel == 0 || static_cast<uint64_t>(stride) <
            static_cast<uint64_t>(width) * bytes_per_pixel ||
        static_cast<uint64_t>(stride) * static_cast<uint64_t>(height) >
            kMaxDecodedImageBytes) {
        diagnostic = "PDFium returned an unsupported image bitmap pixel format";
        return false;
    }
    const uint64_t output_bytes = static_cast<uint64_t>(pixel_count) * 4u;
    if (output_bytes > UINT64_MAX - decoded_image_bytes || output_bytes > UINT32_MAX) {
        diagnostic = "PDF images exceed the wire-format addressable pixel range";
        return false;
    }
    float left = 0.0f, bottom = 0.0f, right = 0.0f, top = 0.0f;
    if (!api.get_object_bounds(object, &left, &bottom, &right, &top) ||
        !std::isfinite(left) || !std::isfinite(bottom) || !std::isfinite(right) ||
        !std::isfinite(top) || right <= left || top <= bottom) {
        diagnostic = "PDF image placement bounds are invalid";
        return false;
    }
    const std::array<Point2D, 4> transformed_bounds{
        TransformPoint(parent_matrix, left, bottom),
        TransformPoint(parent_matrix, right, bottom),
        TransformPoint(parent_matrix, left, top),
        TransformPoint(parent_matrix, right, top)};
    const Point2D first_rotated_bound = PagePointToMillimetres(
        transformed_bounds.front().x, transformed_bounds.front().y, page_bounds);
    double transformed_left = first_rotated_bound.x;
    double transformed_right = transformed_left;
    double transformed_min_y = first_rotated_bound.y;
    double transformed_max_y = transformed_min_y;
    for (const Point2D& source_point : transformed_bounds) {
        const Point2D point = PagePointToMillimetres(
            source_point.x, source_point.y, page_bounds);
        if (!point.IsFinite()) {
            diagnostic = "PDF image group transform produced invalid bounds";
            return false;
        }
        transformed_left = (std::min)(transformed_left, point.x);
        transformed_right = (std::max)(transformed_right, point.x);
        transformed_min_y = (std::min)(transformed_min_y, point.y);
        transformed_max_y = (std::max)(transformed_max_y, point.y);
    }
    const bool rotate_pixels = (page_bounds.rotation_quarters_clockwise & 1u) != 0;
    const int output_width = rotate_pixels ? height : width;
    const int output_height = rotate_pixels ? width : height;
    const size_t record_begin = records.size();
    ImportRasterImageRecord image;
    image.local_id = local_id;
    image.source_id = std::move(source_id);
    image.name = "PDF Image " + std::to_string(local_id);
    image.media_type = "application/x-pikdraw-rgba8";
    image.width_px = static_cast<uint32_t>(output_width);
    image.height_px = static_cast<uint32_t>(output_height);
    image.decoded_pixel_bytes = static_cast<uint32_t>(output_bytes);
    image.source_bits_per_pixel = metadata.bits_per_pixel ? metadata.bits_per_pixel : 32u;
    const double source_dpi_x = metadata.horizontal_dpi > 0.0f
        ? metadata.horizontal_dpi : 96.0;
    const double source_dpi_y = metadata.vertical_dpi > 0.0f
        ? metadata.vertical_dpi : 96.0;
    image.dpi_x = rotate_pixels ? source_dpi_y : source_dpi_x;
    image.dpi_y = rotate_pixels ? source_dpi_x : source_dpi_y;
    image.dpi_from_source = metadata.horizontal_dpi > 0.0f && metadata.vertical_dpi > 0.0f;
    image.x_mm = transformed_left;
    image.y_mm = transformed_min_y;
    image.width_mm = transformed_right - transformed_left;
    image.height_mm = transformed_max_y - transformed_min_y;
    image.pixel_data_local_id = local_id;
    records.emplace_back(image);
    const auto* source_pixels = static_cast<const uint8_t*>(raw_buffer);
    std::vector<uint8_t> chunk_bytes;
    chunk_bytes.reserve(kRasterChunkBytes);
    uint32_t output_offset = 0;
    const auto emit_chunk = [&]() {
        ImportRasterImageChunkRecord chunk;
        chunk.local_id = local_id;
        chunk.byte_offset = output_offset;
        chunk.total_bytes = image.decoded_pixel_bytes;
        chunk.bytes = std::move(chunk_bytes);
        output_offset += static_cast<uint32_t>(chunk.bytes.size());
        records.emplace_back(std::move(chunk));
        chunk_bytes.clear();
        chunk_bytes.reserve(kRasterChunkBytes);
    };
    for (int y = 0; y < output_height; ++y) {
        for (int x = 0; x < output_width; ++x) {
            int source_x = x;
            int source_y = y;
            switch (page_bounds.rotation_quarters_clockwise) {
            case 1: source_x = y; source_y = height - 1 - x; break;
            case 2: source_x = width - 1 - x; source_y = height - 1 - y; break;
            case 3: source_x = width - 1 - y; source_y = x; break;
            default: break;
            }
            const uint8_t* source_row = source_pixels +
                static_cast<size_t>(source_y) * stride;
            const size_t source_offset = static_cast<size_t>(source_x) * bytes_per_pixel;
            uint8_t red = 0, green = 0, blue = 0, alpha = 255;
            if (format == FPDFBitmap_Gray) {
                red = green = blue = source_row[source_offset];
            } else {
                blue = source_row[source_offset];
                green = source_row[source_offset + 1];
                red = source_row[source_offset + 2];
                if (format == FPDFBitmap_BGRA || format == FPDFBitmap_BGRA_Premul)
                    alpha = source_row[source_offset + 3];
            }
            if (format == FPDFBitmap_BGRA_Premul) {
                const auto unpremultiply = [alpha](uint8_t component) {
                    return alpha == 0 ? uint8_t{0} : static_cast<uint8_t>(
                        (std::min)(255u, (static_cast<uint32_t>(component) * 255u + alpha / 2u) / alpha));
                };
                red = unpremultiply(red);
                green = unpremultiply(green);
                blue = unpremultiply(blue);
            }
            chunk_bytes.push_back(red);
            chunk_bytes.push_back(green);
            chunk_bytes.push_back(blue);
            chunk_bytes.push_back(alpha);
            if (chunk_bytes.size() == kRasterChunkBytes) emit_chunk();
        }
    }
    if (!chunk_bytes.empty()) emit_chunk();
    decoded_image_bytes += output_bytes;
    if (output_offset != image.decoded_pixel_bytes) {
        diagnostic = "PDF raster chunks do not cover the complete decoded image";
        return false;
    }
    if (records.size() > kMaxProposalRecords) {
        records.resize(record_begin);
        diagnostic = "PDF image pixel chunks exceed the bounded proposal record profile";
        return false;
    }
    return true;
}

#endif // _WIN32
} // namespace

bool BuildPdfImportRecords(std::span<const uint8_t> source, bool is_ai_representation,
                           std::vector<CanonicalImportRecord>& records,
                           std::string& diagnostic,
                           uint32_t source_page_index) {
    records.clear();
    diagnostic.clear();
    if (source.empty()) {
        diagnostic = "PDF source is empty";
        return false;
    }
#ifndef _WIN32
    (void)is_ai_representation;
    diagnostic = "PDF vector import currently requires the Windows PDFium runtime";
    return false;
#else
    try {
        PdfiumApi api;
        if (!api.ready()) {
            diagnostic = "PDFium runtime is missing or its required page-object API is incomplete";
            return false;
        }
        DocumentGuard document{api};
        document.value = api.load_mem_document64(source.data(), source.size(), nullptr);
        if (!document.value) {
            diagnostic = "PDFium rejected the source PDF representation (it may be encrypted or malformed)";
            return false;
        }
        const int source_page_count = api.get_page_count(document.value);
        if (source_page_count <= 0) {
            diagnostic = "PDF document has no readable pages";
            return false;
        }
        if (source_page_index >= static_cast<uint32_t>(source_page_count)) {
            diagnostic = "Selected PDF page index is outside the source page tree";
            return false;
        }
        PageGuard page{api};
        page.value = api.load_page(document.value, static_cast<int>(source_page_index));
        if (!page.value) {
            diagnostic = "PDFium could not load source page " +
                std::to_string(source_page_index + 1);
            return false;
        }
        const int page_rotation = api.get_page_rotation(page.value);
        if (page_rotation < 0 || page_rotation > 3) {
            diagnostic = "PDF page rotation is invalid";
            return false;
        }

        FS_RECTF page_box{};
        if (!api.get_page_bounding_box(page.value, &page_box)) {
            diagnostic = "PDF page bounds are not readable";
            return false;
        }
        const PdfPageBounds bounds{page_box.left, page_box.bottom,
                                   page_box.right, page_box.top,
                                   static_cast<uint8_t>(page_rotation)};
        const double width_pt = bounds.right - bounds.left;
        const double height_pt = bounds.top - bounds.bottom;
        const bool swap_page_dimensions = (page_rotation & 1) != 0;
        const double width_mm = (swap_page_dimensions ? height_pt : width_pt) *
            kPdfPointsToMillimetres;
        const double height_mm = (swap_page_dimensions ? width_pt : height_pt) *
            kPdfPointsToMillimetres;
        if (!IsFinitePositive(width_mm) || !IsFinitePositive(height_mm) ||
            width_mm > 100000.0 || height_mm > 100000.0) {
            diagnostic = "PDF page dimensions exceed the supported physical page limits";
            return false;
        }

        const int object_count = api.count_objects(page.value);
        if (object_count <= 0 || object_count > kMaxPageObjects) {
            diagnostic = "PDF page contains " + std::to_string(object_count) +
                " objects; the bounded profile requires 1.." +
                std::to_string(kMaxPageObjects) + " objects";
            return false;
        }
        TextPageGuard text_page{api};
        text_page.value = api.load_text_page(page.value);
        if (!text_page.value) {
            diagnostic = "PDF text page could not be initialized";
            return false;
        }

        const std::string page_name = "PDF Page " + std::to_string(source_page_index + 1);
        const std::string layer_source_id = "pdf-page-" + std::to_string(source_page_index + 1);
        const std::string default_layer_name = page_name;
        const std::vector<std::string> source_ocg_names = ExtractPdfOptionalContentNames(source);
        const std::string initial_layer_name = source_ocg_names.size() == 1
            ? source_ocg_names.front() : default_layer_name;
        std::vector<ImportLayerRecord> layers;
        std::unordered_map<std::string, size_t> layer_indices;
        const auto ensure_layer = [&](const std::string& name) -> ImportLayerRecord* {
            const auto found = layer_indices.find(name);
            if (found != layer_indices.end()) return &layers[found->second];
            if (layers.size() >= IPC_MAX_OBJECTS) return nullptr;
            ImportLayerRecord layer;
            layer.local_id = static_cast<uint32_t>(layers.size() + 1);
            layer.source_id = name == default_layer_name
                ? layer_source_id
                : layer_source_id + "/ocg-" + std::to_string(layers.size() + 1);
            layer.name = name;
            const size_t index = layers.size();
            layers.push_back(std::move(layer));
            layer_indices.emplace(name, index);
            return &layers.back();
        };
        std::vector<ImportRecordValue> object_records;
        std::vector<ImportFidelityIssueRecord> issues;
        object_records.reserve(static_cast<size_t>(object_count));
        uint32_t local_id = 1;
        uint32_t issue_count = 0;
        uint64_t decoded_image_bytes = 0;
        bool saw_image = false;
        bool saw_form = false;
        bool saw_transparency = false;
        bool saw_unsupported_object = false;
        bool saw_unmapped_optional_content = source_ocg_names.size() > 1;
        bool saw_object_optional_content = false;
        const auto add_issue = [&](std::string_view feature, std::string_view locator) {
            ImportFidelityIssueRecord issue;
            issue.severity = ImportFidelitySeverity::Warning;
            issue.source_locator.assign(locator);
            issue.feature_code.assign(feature);
            issue.diagnostic_code = "pdf-to-native-editable-subset";
            issues.push_back(std::move(issue));
            ++issue_count;
            return issues.size() <= 4096;
        };

        size_t visited_objects = 0;
        const auto identity = FS_MATRIX{1, 0, 0, 1, 0, 0};
        std::function<bool(FPDF_PAGEOBJECT, const FS_MATRIX&, std::string, size_t, std::string)> import_object;
        import_object = [&](FPDF_PAGEOBJECT object, const FS_MATRIX& parent_matrix,
                            std::string source_id, size_t depth, std::string inherited_layer) {
            if (!object || depth > 16 || ++visited_objects >
                    static_cast<size_t>(kMaxPageObjects)) {
                saw_unsupported_object = true;
                return true;
            }
            const int type = api.get_object_type(object);
            saw_transparency |= api.has_object_transparency(object) != 0;
            bool has_optional_content = false;
            const auto optional_layer = ReadPdfObjectOptionalContentName(
                api, object, has_optional_content);
            if (has_optional_content) {
                if (optional_layer && !optional_layer->empty()) {
                    inherited_layer = *optional_layer;
                    saw_object_optional_content = true;
                }
                else saw_unmapped_optional_content = true;
            }
            if (inherited_layer.empty()) inherited_layer = default_layer_name;
            if (type == FPDF_PAGEOBJ_FORM) {
                saw_form = true;
                FS_MATRIX form_matrix{};
                const int child_count = api.count_form_objects(object);
                if (!api.get_object_matrix(object, &form_matrix) || child_count < 0 ||
                    child_count > kMaxPageObjects) {
                    saw_unsupported_object = true;
                    return true;
                }
                const FS_MATRIX composed_matrix = Multiply(parent_matrix, form_matrix);
                for (int child_index = 0; child_index < child_count; ++child_index) {
                    FPDF_PAGEOBJECT child = api.get_form_object(
                        object, static_cast<unsigned long>(child_index));
                    if (!import_object(child, composed_matrix,
                            source_id + "/child-" + std::to_string(child_index + 1), depth + 1,
                            inherited_layer))
                        return false;
                }
                return true;
            }

            if (type == FPDF_PAGEOBJ_PATH) {
                ImportPathRecord path;
                std::string object_diagnostic;
                if (!AddPathRecord(api, object, local_id, source_id, bounds, parent_matrix,
                                   path, object_diagnostic)) {
                    saw_unsupported_object = true;
                    return true;
                }
                auto* layer = ensure_layer(inherited_layer);
                if (!layer) return false;
                layer->object_local_ids.push_back(local_id++);
                object_records.emplace_back(std::move(path));
            } else if (type == FPDF_PAGEOBJ_TEXT) {
                ImportTextRecord text;
                std::string object_diagnostic;
                if (!AddTextRecord(api, object, text_page.value, local_id, source_id,
                                   bounds, parent_matrix, text, object_diagnostic)) {
                    saw_unsupported_object = true;
                    return true;
                }
                auto* layer = ensure_layer(inherited_layer);
                if (!layer) return false;
                layer->object_local_ids.push_back(local_id++);
                object_records.emplace_back(std::move(text));
            } else if (type == FPDF_PAGEOBJ_IMAGE) {
                std::string object_diagnostic;
                if (!AddImageRecords(api, document.value, page.value, object, local_id,
                                     source_id, bounds, parent_matrix, decoded_image_bytes,
                                     object_records, object_diagnostic)) {
                    saw_unsupported_object = true;
                    return true;
                }
                auto* layer = ensure_layer(inherited_layer);
                if (!layer) return false;
                layer->object_local_ids.push_back(local_id++);
                saw_image = true;
            } else {
                saw_unsupported_object = true;
            }
            return true;
        };
        for (int index = 0; index < object_count; ++index) {
            FPDF_PAGEOBJECT object = api.get_object(page.value, index);
            if (!object || !import_object(object, identity,
                    "pdf-object-" + std::to_string(index + 1), 0,
                    initial_layer_name)) {
                diagnostic = "PDF page object hierarchy could not be traversed safely";
                records.clear();
                return false;
            }
        }

        if (layers.empty()) {
            diagnostic = "PDF page contains no objects in the supported editable subset";
            return false;
        }

        records.reserve(object_records.size() + issues.size() + layers.size() + 2);
        ImportPageRecord page_record{width_mm, height_mm};
        if (page_rotation != 0) {
            page_record.local_id = 1;
            page_record.source_page_index = source_page_index;
            page_record.name = "PDF Page " + std::to_string(source_page_index + 1);
            page_record.rotation_degrees = static_cast<uint16_t>(page_rotation * 90);
            page_record.has_manifest_metadata = true;
        }
        if (!AddRecord(records, page_record)) {
            diagnostic = "PDF page or layer record exceeds C6 record limits";
            records.clear();
            return false;
        }
        for (const auto& layer : layers) {
            if (layer.object_local_ids.empty()) {
                diagnostic = "PDF source layer has no editable objects: " + layer.name;
                records.clear();
                return false;
            }
            if (!AddRecord(records, layer)) {
                diagnostic = "PDF source layer record exceeds C6 record limits";
                records.clear();
                return false;
            }
        }
        for (const auto& object : object_records) {
            if (!AddRecord(records, object)) {
                diagnostic = "PDF object record exceeds C6 record limits";
                records.clear();
                return false;
            }
        }
        const std::string fidelity_source_id = layers.front().source_id;
        const bool source_layers_not_mapped = source_ocg_names.empty() &&
            std::none_of(layers.begin(), layers.end(), [&](const auto& layer) {
                return layer.name != default_layer_name;
            });
        if ((source_layers_not_mapped &&
             !add_issue("pdf-source-layers-not-mapped", fidelity_source_id)) ||
            (saw_unmapped_optional_content &&
             !add_issue("pdf-some-source-layer-membership-not-mapped", fidelity_source_id)) ||
            (source_ocg_names.size() == 1 && !saw_object_optional_content &&
             !add_issue("pdf-single-ocg-layer-assignment-assumed", fidelity_source_id)) ||
            !add_issue("pdf-effects-and-clipping-not-mapped", fidelity_source_id) ||
            !add_issue("pdf-source-color-management-not-retained", fidelity_source_id) ||
            (saw_transparency && !add_issue("pdf-transparency-effects", fidelity_source_id)) ||
            (saw_form && !add_issue("pdf-form-groups-flattened", fidelity_source_id)) ||
            (saw_unsupported_object && !add_issue("pdf-object-types-or-styles-not-mapped", fidelity_source_id)) ||
            (saw_image && !add_issue("pdf-image-source-encoding-normalized-to-rgba8", fidelity_source_id)) ||
            (source_page_count > 1 && !add_issue(
                source_page_index == 0 ? "pdf-pages-after-first-not-imported"
                                       : "pdf-other-pages-not-selected",
                fidelity_source_id)) ||
            (is_ai_representation &&
             !add_issue("ai-authoring-data-not-read", fidelity_source_id))) {
            diagnostic = "PDF fidelity report exceeded C6 record limits";
            records.clear();
            return false;
        }
        for (const auto& issue : issues) {
            if (!AddRecord(records, issue)) {
                diagnostic = "PDF fidelity issue exceeds C6 record limits";
                records.clear();
                return false;
            }
        }

        ImportFidelityReportRecord report;
        report.coverage_profile = is_ai_representation
            ? "ai-pdf-compatible-vector-v2" : "pdf-page-vector-v2";
        report.parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
        report.coverage = ImportCoverage::Partial;
        report.appearance = ImportAppearance::Partial;
        report.editability = ImportEditability::NativeEditable;
        report.production = ImportProduction::NotAssessed;
        report.issue_count = issue_count;
        if (!AddRecord(records, report)) {
            diagnostic = "PDF fidelity summary exceeds C6 record limits";
            records.clear();
            return false;
        }
        return !layers.empty();
    } catch (...) {
        records.clear();
        diagnostic = "PDF vector import allocation or PDFium API failure";
        return false;
    }
#endif
}

bool InspectPdfSource(std::span<const uint8_t> source,
                      PdfSourceManifest& manifest,
                      std::string& diagnostic) {
    manifest = {};
    diagnostic.clear();
    if (source.empty()) {
        diagnostic = "PDF source is empty";
        return false;
    }
#ifndef _WIN32
    diagnostic = "PDF page inspection currently requires the Windows PDFium runtime";
    return false;
#else
    try {
        PdfiumApi api;
        if (!api.ready()) {
            diagnostic = "PDFium runtime is missing or its required page-object API is incomplete";
            return false;
        }
        DocumentGuard document{api};
        document.value = api.load_mem_document64(source.data(), source.size(), nullptr);
        if (!document.value) {
            diagnostic = "PDFium rejected the source PDF representation (it may be encrypted or malformed)";
            return false;
        }
        const int page_count = api.get_page_count(document.value);
        if (page_count <= 0) {
            diagnostic = "PDF document has no readable pages";
            return false;
        }

        PdfSourceManifest discovered;
        discovered.pages.reserve(static_cast<size_t>(page_count));
        for (int index = 0; index < page_count; ++index) {
            PageGuard page{api};
            page.value = api.load_page(document.value, index);
            if (!page.value) {
                diagnostic = "PDFium could not load source page " + std::to_string(index + 1);
                return false;
            }
            FS_RECTF page_box{};
            if (!api.get_page_bounding_box(page.value, &page_box)) {
                diagnostic = "PDF page bounds are not readable for page " + std::to_string(index + 1);
                return false;
            }
            const double width_pt = static_cast<double>(page_box.right) - page_box.left;
            const double height_pt = static_cast<double>(page_box.top) - page_box.bottom;
            const int rotation = api.get_page_rotation(page.value);
            const int object_count = api.count_objects(page.value);
            if (!std::isfinite(width_pt) || !std::isfinite(height_pt) ||
                width_pt <= 0.0 || height_pt <= 0.0 || rotation < 0 || rotation > 3 ||
                object_count < 0) {
                diagnostic = "PDF page metadata is invalid for page " + std::to_string(index + 1);
                return false;
            }
            discovered.pages.push_back(PdfSourcePageDescriptor{
                static_cast<uint32_t>(index),
                static_cast<double>(page_box.left),
                static_cast<double>(page_box.bottom),
                static_cast<double>(page_box.right),
                static_cast<double>(page_box.top),
                width_pt * kPdfPointsToMillimetres,
                height_pt * kPdfPointsToMillimetres,
                static_cast<uint8_t>(rotation),
                static_cast<int32_t>(object_count)});
        }
        discovered.exact_page_count = static_cast<uint32_t>(page_count);
        manifest = std::move(discovered);
        return true;
    } catch (...) {
        manifest = {};
        diagnostic = "PDF page inspection allocation or PDFium API failure";
        return false;
    }
#endif
}

} // namespace pikdraw::interop
