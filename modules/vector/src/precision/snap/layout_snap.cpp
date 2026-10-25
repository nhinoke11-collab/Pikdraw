#include "pikdraw/vector/precision/snap/layout_snap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace pikdraw::vector::precision::snap {
namespace {

struct Candidate {
    LayoutSnapKey key;
    double correction{0.0};
    double score_dip{std::numeric_limits<double>::infinity()};
    double spacing_mm{0.0};
    double start_mm{0.0};
    double end_mm{0.0};
    double coordinate_mm{0.0};
};

double low(const geometry::Rect2D& bounds, bool vertical) noexcept {
    return vertical ? bounds.y : bounds.x;
}

double high(const geometry::Rect2D& bounds, bool vertical) noexcept {
    return vertical ? bounds.y + bounds.height : bounds.x + bounds.width;
}

double span_low(const geometry::Rect2D& bounds, bool vertical) noexcept {
    return vertical ? bounds.x : bounds.y;
}

double span_high(const geometry::Rect2D& bounds, bool vertical) noexcept {
    return vertical ? bounds.x + bounds.width : bounds.y + bounds.height;
}

bool lane_overlaps(const geometry::Rect2D& first, const geometry::Rect2D& second,
                   bool vertical_axis) noexcept {
    const double overlap = std::min(span_high(first, vertical_axis), span_high(second, vertical_axis)) -
                           std::max(span_low(first, vertical_axis), span_low(second, vertical_axis));
    return overlap >= 0.0;
}

double feature(const geometry::Rect2D& bounds, bool vertical, uint8_t index) noexcept {
    const double lo = low(bounds, vertical), hi = high(bounds, vertical);
    return index == 0 ? lo : index == 1 ? (lo + hi) * 0.5 : hi;
}

bool stable_before(const Candidate& first, const Candidate& second) noexcept {
    return std::tuple{first.score_dip, static_cast<uint8_t>(first.key.relation),
                      first.key.reference_id, first.key.secondary_id,
                      first.key.moving_feature, first.key.reference_feature} <
           std::tuple{second.score_dip, static_cast<uint8_t>(second.key.relation),
                      second.key.reference_id, second.key.secondary_id,
                      second.key.moving_feature, second.key.reference_feature};
}

void offer(std::vector<Candidate>& candidates, const LayoutSnapRequest& request,
           LayoutSnapKey key, double correction, double scale, double start, double end,
           double spacing = 0.0) {
    const double score = std::abs(correction) * scale;
    const auto& previous = request.previous_acquisition[key.vertical_axis ? 1 : 0];
    const double limit = previous && *previous == key
        ? request.release_radius_dip : request.acquire_radius_dip;
    if (!std::isfinite(score) || score > limit) return;
    candidates.push_back(Candidate{key, correction, score, spacing, start, end,
                                   feature(request.moving_bounds, key.vertical_axis,
                                           key.moving_feature) +
                                       (key.vertical_axis ? request.raw_delta.y : request.raw_delta.x) + correction});
}

std::optional<Candidate> best_candidate(const std::vector<Candidate>& candidates,
                                        const LayoutSnapRequest& request, bool vertical) {
    const Candidate* best = nullptr;
    const auto& previous = request.previous_acquisition[vertical ? 1 : 0];
    for (const auto& candidate : candidates) {
        if (candidate.key.vertical_axis != vertical) continue;
        if (!best) { best = &candidate; continue; }
        const bool candidate_retained = previous && candidate.key == *previous;
        const bool best_retained = previous && best->key == *previous;
        if (candidate_retained != best_retained) {
            if (candidate_retained) best = &candidate;
        } else if (stable_before(candidate, *best)) {
            best = &candidate;
        }
    }
    return best ? std::optional<Candidate>(*best) : std::nullopt;
}

std::optional<Candidate> equal_spacing_candidate(const LayoutSnapRequest& request,
                                                  const geometry::Rect2D& moving,
                                                  bool vertical) {
    std::vector<const LayoutSnapReference*> lane;
    lane.reserve(request.references.size());
    for (const auto& reference : request.references) {
        if (reference.kind != LayoutSnapReferenceKind::Object ||
            reference.id == document::kInvalidObjectId || !reference.bounds.is_finite() ||
            reference.bounds.is_empty() || !lane_overlaps(moving, reference.bounds, vertical))
            continue;
        lane.push_back(&reference);
    }
    if (lane.size() < 2) return std::nullopt;

    const double mlo = low(moving, vertical), mhi = high(moving, vertical);
    const double msize = mhi - mlo;
    const double mcenter = (mlo + mhi) * 0.5;
    const double scale = vertical ? request.dip_per_mm_y : request.dip_per_mm_x;
    std::vector<Candidate> candidates;

    const LayoutSnapReference* nearest_left = nullptr;
    const LayoutSnapReference* second_left = nullptr;
    const LayoutSnapReference* nearest_right = nullptr;
    const LayoutSnapReference* second_right = nullptr;
    for (const auto* reference : lane) {
        const double rlo = low(reference->bounds, vertical);
        const double rhi = high(reference->bounds, vertical);
        if (rhi <= mcenter) {
            if (!nearest_left || rhi > high(nearest_left->bounds, vertical) ||
                (rhi == high(nearest_left->bounds, vertical) && reference->id < nearest_left->id)) {
                second_left = nearest_left;
                nearest_left = reference;
            } else if (!second_left || rhi > high(second_left->bounds, vertical) ||
                       (rhi == high(second_left->bounds, vertical) && reference->id < second_left->id)) {
                second_left = reference;
            }
        } else if (rlo >= mcenter) {
            if (!nearest_right || rlo < low(nearest_right->bounds, vertical) ||
                (rlo == low(nearest_right->bounds, vertical) && reference->id < nearest_right->id)) {
                second_right = nearest_right;
                nearest_right = reference;
            } else if (!second_right || rlo < low(second_right->bounds, vertical) ||
                       (rlo == low(second_right->bounds, vertical) && reference->id < second_right->id)) {
                second_right = reference;
            }
        }
    }

    if (nearest_left && nearest_right) {
        const double left_end = high(nearest_left->bounds, vertical);
        const double right_start = low(nearest_right->bounds, vertical);
        const double gap = right_start - left_end - msize;
        if (gap >= 0.0) {
            const double target = (left_end + right_start - msize) * 0.5;
            LayoutSnapKey key{LayoutSnapRelation::EqualSpacing, vertical,
                              nearest_left->id, nearest_right->id, 1, 1};
            candidates.push_back(Candidate{key, target - mlo, std::abs(target - mlo) * scale,
                gap * 0.5, left_end, right_start, target});
        }
    }
    if (second_left && nearest_left) {
        const double gap = low(nearest_left->bounds, vertical) - high(second_left->bounds, vertical);
        if (gap >= 0.0) {
            const double target = high(nearest_left->bounds, vertical) + gap;
            LayoutSnapKey key{LayoutSnapRelation::EqualSpacing, vertical,
                              nearest_left->id, second_left->id, 0, 0};
            candidates.push_back(Candidate{key, target - mlo, std::abs(target - mlo) * scale,
                                           gap, high(nearest_left->bounds, vertical), target, target});
        }
    }
    if (nearest_right && second_right) {
        const double gap = low(second_right->bounds, vertical) - high(nearest_right->bounds, vertical);
        if (gap >= 0.0) {
            const double target = low(nearest_right->bounds, vertical) - gap - msize;
            LayoutSnapKey key{LayoutSnapRelation::EqualSpacing, vertical,
                              nearest_right->id, second_right->id, 2, 2};
            candidates.push_back(Candidate{key, target - mlo, std::abs(target - mlo) * scale,
                gap, target + msize, low(nearest_right->bounds, vertical), target});
        }
    }

    const auto& previous = request.previous_acquisition[vertical ? 1 : 0];
    const double limit = request.acquire_radius_dip;
    for (auto& candidate : candidates) {
        const bool retained = previous && candidate.key == *previous;
        const double allowed = retained ? request.release_radius_dip : limit;
        if (!std::isfinite(candidate.score_dip) || candidate.score_dip > allowed)
            candidate.score_dip = std::numeric_limits<double>::infinity();
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
        return !std::isfinite(candidate.score_dip);
    }), candidates.end());
    if (candidates.empty()) return std::nullopt;
    const auto best = std::min_element(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const bool a_retained = previous && a.key == *previous;
        const bool b_retained = previous && b.key == *previous;
        return a_retained != b_retained ? a_retained : stable_before(a, b);
    });
    return *best;
}

} // namespace

LayoutSnapSolution solve_layout_snap(const LayoutSnapRequest& request) {
    LayoutSnapSolution result{};
    result.accepted_delta = request.raw_delta;
    if (!request.moving_bounds.is_finite() || request.moving_bounds.is_empty() ||
        !std::isfinite(request.raw_delta.x) || !std::isfinite(request.raw_delta.y) ||
        !std::isfinite(request.dip_per_mm_x) || !std::isfinite(request.dip_per_mm_y) ||
        request.dip_per_mm_x <= 0.0 || request.dip_per_mm_y <= 0.0 ||
        !std::isfinite(request.acquire_radius_dip) || request.acquire_radius_dip <= 0.0)
        return result;

    auto moving = request.moving_bounds;
    moving.x += request.raw_delta.x;
    moving.y += request.raw_delta.y;
    for (const bool vertical : {false, true}) {
        std::vector<Candidate> candidates;
        if (request.alignment_enabled) {
            const double scale = vertical ? request.dip_per_mm_y : request.dip_per_mm_x;
            for (const auto& reference : request.references) {
                const bool enabled = reference.kind == LayoutSnapReferenceKind::Object
                    ? request.objects_enabled :
                    (reference.kind == LayoutSnapReferenceKind::Page ||
                     reference.kind == LayoutSnapReferenceKind::Artboard)
                    ? request.page_enabled : false;
                if (!enabled) continue;
                if ((reference.kind == LayoutSnapReferenceKind::Object &&
                     reference.id == document::kInvalidObjectId) || !reference.bounds.is_finite() ||
                    reference.bounds.is_empty()) continue;
                for (uint8_t moving_feature = 0; moving_feature < 3; ++moving_feature) {
                    const uint8_t moving_mask = vertical
                        ? request.moving_feature_mask_y : request.moving_feature_mask_x;
                    if ((moving_mask & static_cast<uint8_t>(1u << moving_feature)) == 0) continue;
                    for (uint8_t reference_feature = 0; reference_feature < 3; ++reference_feature) {
                        const double delta = feature(reference.bounds, vertical, reference_feature) -
                                             feature(moving, vertical, moving_feature);
                        const bool contact = (moving_feature == 0 && reference_feature == 2) ||
                                             (moving_feature == 2 && reference_feature == 0);
                        LayoutSnapKey key{contact ? LayoutSnapRelation::Contact : LayoutSnapRelation::Alignment,
                            vertical, reference.id, document::kInvalidObjectId,
                            moving_feature, reference_feature, reference.kind};
                        const double start = std::min(span_low(moving, vertical),
                                                      span_low(reference.bounds, vertical));
                        const double end = std::max(span_high(moving, vertical),
                                                    span_high(reference.bounds, vertical));
                        offer(candidates, request, key, delta, scale, start, end);
                    }
                }
            }
        }
        if (request.alignment_enabled && request.guidelines_enabled) {
            const double scale = vertical ? request.dip_per_mm_y : request.dip_per_mm_x;
            for (const auto& line : request.guidelines) {
                if (line.vertical_axis != vertical || line.id == 0 ||
                    !std::isfinite(line.coordinate_mm) || !std::isfinite(line.start_mm) ||
                    !std::isfinite(line.end_mm) || line.start_mm > line.end_mm)
                    continue;
                for (uint8_t moving_feature = 0; moving_feature < 3; ++moving_feature) {
                    const uint8_t moving_mask = vertical
                        ? request.moving_feature_mask_y : request.moving_feature_mask_x;
                    if ((moving_mask & static_cast<uint8_t>(1u << moving_feature)) == 0) continue;
                    const double delta = line.coordinate_mm - feature(moving, vertical, moving_feature);
                    LayoutSnapKey key{LayoutSnapRelation::Alignment, vertical,
                        static_cast<document::ObjectId>(line.id), document::kInvalidObjectId,
                        moving_feature, 3, LayoutSnapReferenceKind::Guideline};
                    offer(candidates, request, key, delta, scale, line.start_mm, line.end_mm);
                }
            }
        }
        if (request.spacing_enabled) {
            if (const auto spacing = equal_spacing_candidate(request, moving, vertical))
                candidates.push_back(*spacing);
        }
        if (const auto chosen = best_candidate(candidates, request, vertical)) {
            const double correction = chosen->correction;
            if (vertical) result.accepted_delta.y += correction;
            else result.accepted_delta.x += correction;
            result.acquisition[vertical ? 1 : 0] = chosen->key;
            result.guides.push_back(LayoutSnapGuide{chosen->key, chosen->coordinate_mm,
                chosen->start_mm, chosen->end_mm, chosen->spacing_mm});
            result.snapped = true;
        }
    }
    return result;
}

} // namespace pikdraw::vector::precision::snap
