#include "pikdraw/raster/tile_cache.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

using namespace pikdraw::raster;

std::shared_ptr<const RasterTile> make_tile(
    const std::shared_ptr<RasterMemoryBudget::Reservation>& reservation,
    int64_t x, uint8_t fill) {
  auto tile = std::make_shared<RasterTile>();
  tile->key.asset_id = {0x1234, 0x5678};
  tile->key.revision = 1;
  tile->key.tile_x = x;
  tile->width = 2;
  tile->height = 2;
  tile->row_stride = 8;
  tile->pixels.assign(16, fill);
  tile->reservation = reservation;
  return tile;
}

void test_budget_reservations_follow_shared_data_lifetime() {
  RasterMemoryBudget budget(32);
  auto first = budget.try_reserve(16);
  assert(first);
  assert(budget.reserved_bytes() == 16);
  auto shared = first;
  first.reset();
  assert(budget.reserved_bytes() == 16);
  assert(!budget.try_reserve(17));
  shared.reset();
  assert(budget.reserved_bytes() == 0);
  auto second = budget.try_reserve(32);
  assert(second);
  budget.set_capacity(8);
  assert(budget.available_bytes() == 0);
  second.reset();
  assert(budget.reserved_bytes() == 0);
}

void test_reservations_use_external_memory_admission_and_resume() {
  bool ram_available = false;
  uint64_t admitted_bytes = 0;
  RasterMemoryBudget budget(std::numeric_limits<uint64_t>::max(),
      [&](uint64_t bytes) {
        admitted_bytes = bytes;
        return ram_available;
      });
  assert(!budget.try_reserve(4096));
  assert(admitted_bytes == 4096 && budget.reserved_bytes() == 0);
  ram_available = true;
  auto resumed = budget.try_reserve(4096);
  assert(resumed && budget.reserved_bytes() == 4096);
  resumed.reset();
  assert(budget.reserved_bytes() == 0);
}

void test_budget_refreshes_from_additional_system_headroom() {
  RasterMemoryBudget budget(0);
  auto first = budget.try_reserve_with_headroom(40, 64);
  assert(first);
  assert(budget.reserved_bytes() == 40);
  assert(budget.available_bytes() == 24);

  // Existing reservations remain charged; newly measured RAM headroom adds
  // capacity for the next allocation rather than replacing the prior charge.
  auto second = budget.try_reserve_with_headroom(32, 32);
  assert(second);
  assert(budget.reserved_bytes() == 72);
  assert(budget.available_bytes() == 0);
  assert(!budget.try_reserve_with_headroom(1, 0));

  second.reset();
  assert(budget.reserved_bytes() == 40);
  auto after_release = budget.try_reserve_with_headroom(8, 8);
  assert(after_release);
  assert(budget.reserved_bytes() == 48);
}

void test_cache_evicts_lru_and_keeps_external_tile_charged() {
  RasterMemoryBudget budget(48);
  RasterTileCache cache(32);
  auto reserve_a = budget.try_reserve(16);
  auto tile_a = make_tile(reserve_a, 0, 0x11);
  const auto key_a = tile_a->key;
  assert(cache.insert(tile_a));
  reserve_a.reset();

  auto pinned_a = cache.find(key_a);
  assert(pinned_a && pinned_a->pixels.front() == 0x11);
  tile_a.reset();
  auto reserve_b = budget.try_reserve(16);
  auto tile_b = make_tile(reserve_b, 1, 0x22);
  const auto key_b = tile_b->key;
  assert(cache.insert(tile_b));
  reserve_b.reset();
  tile_b.reset();

  auto reserve_c = budget.try_reserve(16);
  auto tile_c = make_tile(reserve_c, 2, 0x33);
  const auto key_c = tile_c->key;
  assert(cache.insert(tile_c));
  reserve_c.reset();
  tile_c.reset();

  assert(cache.entry_count() == 2);
  assert(!cache.find(key_a));
  assert(cache.find(key_b));
  assert(cache.find(key_c));
  // The evicted tile remains budgeted while a renderer still owns it.
  assert(budget.reserved_bytes() == 48);
  cache.set_capacity(16);
  assert(cache.resident_bytes() == 16);
  cache.clear();
  assert(cache.entry_count() == 0);
  assert(budget.reserved_bytes() == 16);
  pinned_a.reset();
  assert(budget.reserved_bytes() == 0);
}

void test_cache_rejects_invalid_and_over_budget_tiles() {
  RasterMemoryBudget budget(32);
  RasterTileCache cache(16);
  auto oversized_reservation = budget.try_reserve(32);
  auto oversized = std::make_shared<RasterTile>();
  oversized->key.asset_id = {1, 2};
  oversized->key.revision = 1;
  oversized->width = 4;
  oversized->height = 2;
  oversized->row_stride = 16;
  oversized->pixels.assign(32, 0x44);
  oversized->reservation = oversized_reservation;
  assert(!cache.insert(oversized));
  oversized_reservation.reset();
  oversized.reset();

  auto reservation = budget.try_reserve(16);

  auto valid = std::make_shared<RasterTile>();
  valid->key.asset_id = {1, 2};
  valid->key.revision = 1;
  valid->width = 2;
  valid->height = 2;
  valid->row_stride = 8;
  valid->pixels.assign(16, 0x55);
  valid->reservation = reservation;
  assert(cache.insert(valid));
  assert(!cache.insert(nullptr));
  assert(cache.resident_bytes() == 16);

  auto malformed = std::make_shared<RasterTile>(*valid);
  malformed->key.asset_id = {};
  assert(!cache.insert(malformed));
}

}  // namespace

int main() {
  test_budget_reservations_follow_shared_data_lifetime();
  test_reservations_use_external_memory_admission_and_resume();
  test_budget_refreshes_from_additional_system_headroom();
  test_cache_evicts_lru_and_keeps_external_tile_charged();
  test_cache_rejects_invalid_and_over_budget_tiles();
  return 0;
}
