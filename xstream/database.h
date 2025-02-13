#ifndef XSTREAM_DATABASE_H
#define XSTREAM_DATABASE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "xstream/database-parsers.h"

namespace xstream {
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

inline constexpr uint32_t kFrameWordCount = 101;

// Frame is made of 101 words of 32-bit size.
struct Frame {
  uint32_t address;
  std::array<uint32_t, kFrameWordCount> words;
};

// Centralize access to all the required information for a specific part.
class PartDatabase {
 public:
  ~PartDatabase();
  struct TilesDatabase {
    TileGrid tile_grid;
    TileTypesSegmentsBits segments_bits;
  };
  explicit PartDatabase(std::shared_ptr<TilesDatabase> tiles);

  static absl::StatusOr<PartDatabase> Create(
      absl::string_view database_path, absl::string_view part_name);

  const struct Tile* Tile(absl::string_view tile_name) const;
  std::vector<Frame> ComputeSegmentBits(absl::string_view tile_name, uint32_t address) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace xstream
#endif  // XSTREAM_DATABASE_H
