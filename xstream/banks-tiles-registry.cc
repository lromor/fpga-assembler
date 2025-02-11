#include "prjxstream/banks-tiles-registry.h"
#include <unordered_set>

namespace prjxstream {
absl::StatusOr<BanksTilesRegistry> BanksTilesRegistry::Create(const Part &part, const PackagePins &package_pins) {
  std::map<std::string, uint32_t> tile_to_bank;
  std::map<uint32_t, std::unordered_set<std::string>> banks_to_tiles_set;

  for (const auto &pair : part.iobanks) {
    const std::string tile = "HCLK_IOI3_" + pair.second;
    banks_to_tiles_set[pair.first].insert(tile);
    if (tile_to_bank.count(tile) > 0) {
      return absl::InvalidArgumentError("tile mapped to multiple banks");
    }
    tile_to_bank.insert({tile, pair.first});
  }

  for (const auto &pin : package_pins) {
    banks_to_tiles_set[pin.bank].insert(pin.tile);
    if (tile_to_bank.count(pin.tile) > 0) {
      return absl::InvalidArgumentError("tile mapped to multiple banks");
    }
    tile_to_bank.insert({pin.tile, pin.bank});
  }
  // Convert sets to vectors.
  banks_to_tiles_type banks_to_tiles;
  for (const auto &pair : banks_to_tiles_set) {
    banks_to_tiles.insert({pair.first, std::vector<std::string>(pair.second.begin(), pair.second.end())});
  }
  return BanksTilesRegistry(tile_to_bank, banks_to_tiles);
}

std::optional<std::vector<std::string>> BanksTilesRegistry::Tiles(uint32_t bank) const {
  if (banks_to_tiles_.count(bank) == 0) {
    return {};
  }
  return banks_to_tiles_.at(bank);
}

std::optional<uint32_t> BanksTilesRegistry::TileBank(const std::string &tile) const {
  if (tile_to_bank_.count(tile) == 0) {
    return {};
  }
  return tile_to_bank_.at(tile);
}
}  // namespace prjxstream
