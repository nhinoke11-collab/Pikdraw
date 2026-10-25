#include "byte_codec.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include <unordered_set>

namespace pikdraw::document::storage_detail {

void write_gradient(Writer &writer, const std::shared_ptr<const GradientPaint> &gradient,
                    const ProfileResourceIndexMap *profile_indices) {
    if (!gradient) {
        writer.u32(0);
        return;
    }

    Writer body(true);
    body.u32(gradient->schema_major);
    body.u32(gradient->schema_minor);
    body.u64(gradient->id);
    body.u8(static_cast<uint8_t>(gradient->kind));
    body.u8(static_cast<uint8_t>(gradient->spread));
    body.u8(static_cast<uint8_t>(gradient->interpolation));
    body.f64(gradient->gradient_to_object.a);
    body.f64(gradient->gradient_to_object.b);
    body.f64(gradient->gradient_to_object.c);
    body.f64(gradient->gradient_to_object.d);
    body.f64(gradient->gradient_to_object.tx);
    body.f64(gradient->gradient_to_object.ty);
    body.count(gradient->stops.size(), static_cast<uint32_t>(kMaxGradientStops));

    for (const auto &stop : gradient->stops) {
        body.u64(stop.id);
        body.f64(stop.offset);
        write_color(body, stop.color, profile_indices);
        body.f64(stop.opacity);
        body.f64(stop.midpoint_to_next);
    }

    writer.u32(static_cast<uint32_t>(body.bytes.size()));
    writer.append(body.bytes);
}

std::shared_ptr<const GradientPaint> read_gradient(Reader &reader, size_t &remaining_stops,
                                                   bool include_profile_bindings) {
    const uint32_t length = reader.u32();
    if (length == 0) {
        return nullptr;
    }

    Reader sub = reader.subreader(length);
    const auto schema_major = sub.u32();
    const auto schema_minor = sub.u32();
    if (schema_major != kGradientSchemaMajor || schema_minor > kGradientSchemaMinor) {
        throw std::invalid_argument("Unsupported native gradient schema version");
    }

    GradientPaint paint;
    paint.schema_major = schema_major;
    paint.schema_minor = schema_minor;
    paint.id = sub.u64();
    if (paint.id == 0) {
        throw std::invalid_argument("Native gradient paint ID must be nonzero");
    }

    const auto kind_raw = sub.u8();
    if (kind_raw > static_cast<uint8_t>(GradientKind::Radial)) {
        throw std::invalid_argument("Unknown native gradient kind");
    }
    paint.kind = static_cast<GradientKind>(kind_raw);

    const auto spread_raw = sub.u8();
    if (spread_raw > static_cast<uint8_t>(GradientSpread::Reflect)) {
        throw std::invalid_argument("Unknown native gradient spread");
    }
    paint.spread = static_cast<GradientSpread>(spread_raw);

    const auto interp_raw = sub.u8();
    if (interp_raw > static_cast<uint8_t>(GradientInterpolation::EncodedSRGBStraightAlpha)) {
        throw std::invalid_argument("Unknown native gradient interpolation");
    }
    paint.interpolation = static_cast<GradientInterpolation>(interp_raw);

    paint.gradient_to_object = geometry::Transform2D{
        sub.f64(), sub.f64(), sub.f64(), sub.f64(), sub.f64(), sub.f64()
    };
    if (!validate_gradient_matrix(paint.gradient_to_object)) {
        throw std::invalid_argument("Invalid or non-invertible native gradient transform");
    }

    const auto stop_count = sub.count(static_cast<uint32_t>(kMaxGradientStops), 24);
    if (stop_count < kMinGradientStops) {
        throw std::invalid_argument("Native gradient must have at least 2 stops");
    }
    if (stop_count > remaining_stops) {
        throw std::length_error("Too many gradient stops in native document");
    }
    remaining_stops -= stop_count;

    paint.stops.reserve(stop_count);
    std::unordered_set<uint64_t> seen_stop_ids;
    seen_stop_ids.reserve(stop_count);

    for (uint32_t i = 0; i < stop_count; ++i) {
        GradientStop stop;
        stop.id = sub.u64();
        if (stop.id == 0 || !seen_stop_ids.insert(stop.id).second) {
            throw std::invalid_argument("Invalid or duplicate native gradient stop ID");
        }
        stop.offset = sub.f64();
        stop.color = read_color(sub, include_profile_bindings);
        stop.opacity = sub.f64();
        stop.midpoint_to_next = sub.f64();
        paint.stops.push_back(std::move(stop));
    }

    if (sub.remaining() != 0) {
        throw std::invalid_argument("Trailing unconsumed bytes in native gradient record");
    }

    if (const auto error = paint.validation_error()) {
        throw std::invalid_argument("Invalid native gradient: " + *error);
    }

    reserve_gradient_paint_id(paint.id);
    for (const auto &stop : paint.stops) {
        reserve_gradient_stop_id(stop.id);
    }

    return std::make_shared<const GradientPaint>(std::move(paint));
}

} // namespace pikdraw::document::storage_detail
