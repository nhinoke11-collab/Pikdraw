#include "d2d_internal.hpp"
#include <iostream>
#include <cstdlib>
#include <string_view>

namespace pikdraw::rendering::d2d {

D2DDeviceManager::D2DDeviceManager() = default;

D2DDeviceManager::~D2DDeviceManager() {
  shutdown();
}

bool D2DDeviceManager::initialize() {
  // First attempt hardware device
  const auto* driver=std::getenv("PIKDRAW_D3D_DRIVER");
  const bool force_warp=driver&&std::string_view(driver)=="warp";
  if (!force_warp && create_d3d_device(D3D_DRIVER_TYPE_HARDWARE)) {
    is_hardware_ = true;
  } else {
    // Graceful fallback to WARP software rasterizer on GPU pipeline
    if (!create_d3d_device(D3D_DRIVER_TYPE_WARP)) {
      status_ = DeviceLossStatus::FatalDriverCrash;
      return false;
    }
    is_hardware_ = false;
  }

  if (!create_d2d_device()) {
    shutdown();
    status_ = DeviceLossStatus::FatalDriverCrash;
    return false;
  }

  // Create DirectWrite Factory for typography glyph fallback
  HRESULT hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED,
      __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(dwrite_factory_.ReleaseAndGetAddressOf()));
  if (FAILED(hr)) {
    dwrite_factory_.Reset();
  }

  status_ = DeviceLossStatus::Healthy;
  return true;
}

void D2DDeviceManager::shutdown() {
  dwrite_factory_.Reset();
  d2d_context_.Reset();
  d2d_device_.Reset();
  d2d_factory_.Reset();
  dxgi_device_.Reset();
  d3d_context_.Reset();
  d3d_device_.Reset();
  status_ = DeviceLossStatus::FatalDriverCrash;
}

bool D2DDeviceManager::recover() {
  shutdown();
  return initialize();
}

bool D2DDeviceManager::is_valid() const noexcept {
  return d2d_context_ != nullptr && d2d_device_ != nullptr &&
         d3d_device_ != nullptr;
}

DeviceLossStatus D2DDeviceManager::status() const noexcept {
  if (!d3d_device_)
    return DeviceLossStatus::FatalDriverCrash;

  HRESULT reason = d3d_device_->GetDeviceRemovedReason();
  if (SUCCEEDED(reason))
    return DeviceLossStatus::Healthy;

  if (reason == DXGI_ERROR_DEVICE_REMOVED ||
      reason == DXGI_ERROR_DEVICE_RESET ||
      reason == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
    return DeviceLossStatus::RecoverableLoss;
  }
  return DeviceLossStatus::FatalDriverCrash;
}

bool D2DDeviceManager::create_d3d_device(D3D_DRIVER_TYPE driver_type) {
  UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
  // creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_1,
  };
  D3D_FEATURE_LEVEL feature_level;

  HRESULT hr = D3D11CreateDevice(
      nullptr,                    // Default adapter
      driver_type,                // Hardware or WARP
      nullptr,                    // Software module
      creation_flags,             // Flags
      feature_levels,             // Feature levels
      ARRAYSIZE(feature_levels),  // Number of feature levels
      D3D11_SDK_VERSION,          // SDK version
      d3d_device_.ReleaseAndGetAddressOf(),
      &feature_level,
      d3d_context_.ReleaseAndGetAddressOf());

  if (FAILED(hr))
    return false;

  hr = d3d_device_.As(&dxgi_device_);
  return SUCCEEDED(hr);
}

bool D2DDeviceManager::create_d2d_device() {
  D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
  // options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

  HRESULT hr = D2D1CreateFactory(
      D2D1_FACTORY_TYPE_SINGLE_THREADED,
      __uuidof(ID2D1Factory1),
      &options,
      reinterpret_cast<void **>(d2d_factory_.ReleaseAndGetAddressOf()));
  if (FAILED(hr))
    return false;

  hr = d2d_factory_->CreateDevice(dxgi_device_.Get(),
                                  d2d_device_.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  hr = d2d_device_->CreateDeviceContext(
      D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
      d2d_context_.ReleaseAndGetAddressOf());
  return SUCCEEDED(hr);
}

} // namespace pikdraw::rendering::d2d
