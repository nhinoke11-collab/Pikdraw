// Clipper2 2.0.1's engine header uses std::back_inserter without including
// <iterator>. Include it here before compiling the pinned upstream source.
#include <iterator>
#include "clipper.engine.cpp"
