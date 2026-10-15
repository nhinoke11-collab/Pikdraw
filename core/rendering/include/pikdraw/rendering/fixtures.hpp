#pragma once

#include "pikdraw/rendering/scene.hpp"

namespace pikdraw::rendering {

class FixtureGenerator {
public:
    static ExperimentalSceneSnapshot generate_many_small_paths(
        size_t object_count = 25000,
        double spacing_mm = 4.0
    );
    static ExperimentalSceneSnapshot generate_few_large_paths(size_t path_count = 5, size_t points_per_path = 20500);
};

} // namespace pikdraw::rendering
