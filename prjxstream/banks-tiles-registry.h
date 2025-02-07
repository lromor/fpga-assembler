#ifndef PRJXSTREAM_BANKS_TILES_REGISTRY_H
#define PRJXSTREAM_BANKS_TILES_REGISTRY_H

#include <utility>

#include "prjxstream/database.h"

namespace prjxstream {
// Get all tiles associated with an IO bank.
// Allows also to query for an IO bank from a tile.
class BanksTilesRegistry {
  using tile_to_bank_type = std::map<std::string, uint32_t>;
  using banks_to_tiles_type = std::map<uint32_t, std::vector<std::string>>;

 public:
  using const_iterator = banks_to_tiles_type::const_iterator;
  const_iterator begin() const { return banks_to_tiles_.begin(); }
  const_iterator end() const { return banks_to_tiles_.end(); }

  static absl::StatusOr<BanksTilesRegistry> Create(
      const Part &part, const PackagePins &package_pins);

  // Get tiles from an IO bank name.
  std::optional<std::vector<std::string>> Tiles(uint32_t bank) const;

  // Get an IO bank from a tile.
  std::optional<uint32_t> TileBank(const std::string &tile) const;

 private:
  explicit BanksTilesRegistry(tile_to_bank_type tile_to_bank, banks_to_tiles_type banks_to_tiles)
      : tile_to_bank_(std::move(tile_to_bank)), banks_to_tiles_(std::move(banks_to_tiles)) {}
  const tile_to_bank_type tile_to_bank_;
  const banks_to_tiles_type banks_to_tiles_;
};
}  // namespace prjxstream
#endif  // PRJXSTREAM_BANKS_TILES_REGISTRY_H
