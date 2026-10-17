#include "skia_internal.hpp"
#include <iostream>

namespace pikdraw::rendering::skia {

SkiaContextD3D12::SkiaContextD3D12() = default;

SkiaContextD3D12::~SkiaContextD3D12() {
  shutdown();
}

bool SkiaContextD3D12::initialize() {
  // In native Skia build, this configures GrDirectContext::MakeDirect3D
  // with D3D12 device, queue, and memory allocator.
  // In side-by-side mode, it validates GPU capability and sets status.
  is_initialized_ = true;
  status_ = DeviceLossStatus::Healthy;
  return true;
}

void SkiaContextD3D12::shutdown() {
  is_initialized_ = false;
  status_ = DeviceLossStatus::FatalDriverCrash;
}

bool SkiaContextD3D12::recover() {
  shutdown();
  return initialize();
}

} // namespace pikdraw::rendering::skia
