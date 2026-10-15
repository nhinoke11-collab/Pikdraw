#pragma once

#include <cstdint>
#include <string>

namespace pikdraw::document {

using ObjectId = uint64_t;
using Revision = uint64_t;
using Epoch = uint64_t;
using JobGeneration = uint64_t;
using DocumentId = std::string;

constexpr ObjectId INVALID_OBJECT_ID = 0;

} // namespace pikdraw::document
