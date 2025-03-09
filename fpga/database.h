#ifndef FPGA_DATABASE_H
#define FPGA_DATABASE_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "fpga/database-parsers.h"

namespace fpga {
// Many to many map between banks and tiles.
class BanksTilesRegistry {
  using tile_to_bank_type =
    absl::flat_hash_map<std::string, std::vector<uint32_t>>;
  using banks_to_tiles_type =
    absl::flat_hash_map<uint32_t, std::vector<std::string>>;

 public:
  using const_iterator = banks_to_tiles_type::const_iterator;
  const_iterator begin() const { return banks_to_tiles_.begin(); }
  const_iterator end() const { return banks_to_tiles_.end(); }

  static absl::StatusOr<BanksTilesRegistry> Create(
    const Part &part, const PackagePins &package_pins);

  // Get tiles from an IO bank name.
  std::optional<std::vector<std::string>> Tiles(uint32_t bank) const;

  // Get an IO bank from a tile.
  std::vector<uint32_t> TileBanks(const std::string &tile) const;

 private:
  explicit BanksTilesRegistry(tile_to_bank_type tile_to_bank,
                              banks_to_tiles_type banks_to_tiles)
      : tile_to_bank_(std::move(tile_to_bank)),
        banks_to_tiles_(std::move(banks_to_tiles)) {}
  const tile_to_bank_type tile_to_bank_;
  const banks_to_tiles_type banks_to_tiles_;
};

inline constexpr uint32_t kFrameWordCount = 101;
inline constexpr uint32_t kWordSizeBits = 32;

// Define frame configutration word.
using word_t = uint32_t;
static_assert(8 * sizeof(word_t) == 32, "expected word size of 32");

// Frame is made of 101 words of 32-bit size.
// Maps an address to an array of 101 words.
using Frames =
  absl::flat_hash_map<bits_addr_t, std::array<word_t, kFrameWordCount>>;

struct SegmentsBitsWithPseudoPIPs {
  PseudoPIPs pips;
  absl::flat_hash_map<ConfigBusType, SegmentsBits> segment_bits;
};

// Maps tile types to segbits.
using TileTypesSegmentsBitsGetter =
  std::function<std::optional<SegmentsBitsWithPseudoPIPs>(std::string)>;

// Centralize access to all the required information for a specific part.
class PartDatabase {
 public:
  ~PartDatabase() = default;
  struct Tiles {
    Tiles(TileGrid grid, TileTypesSegmentsBitsGetter bits,
          BanksTilesRegistry banks)
        : grid(std::move(grid)),
          bits(std::move(bits)),
          banks(std::move(banks)) {}
    TileGrid grid;
    TileTypesSegmentsBitsGetter bits;
    BanksTilesRegistry banks;
  };
  explicit PartDatabase(std::shared_ptr<Tiles> part_tiles)
      : tiles_(std::move(part_tiles)) {}

  static absl::StatusOr<PartDatabase> Parse(std::string_view database_path,
                                            std::string_view part_name);

  struct FrameBit {
    uint32_t word;
    uint32_t index;
  };
  using BitSetter = std::function<void(ConfigBusType bus, uint32_t address,
                                       const FrameBit &bit, bool value)>;

  // Set bits to configure a feature in a specific tile.
  void ConfigBits(const std::string &tile_name, const std::string &feature,
                  uint32_t address, const BitSetter &bit_setter);
  const struct Tiles &tiles() { return *tiles_; }

 private:
  bool AddSegbitsToCache(const std::string &tile_type);

  std::shared_ptr<Tiles> tiles_;
  absl::flat_hash_map<std::string, SegmentsBitsWithPseudoPIPs>
    segment_bits_cache_;
};
}  // namespace fpga
#endif  // FPGA_DATABASE_H
