#ifndef PRJXSTREAM_DATABASE_H
#define PRJXSTREAM_DATABASE_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace prjxstream {
enum class FrameBlockType {
  kCLBIOCLK,
  kBlockRam
};

struct Location {
  uint32_t x;
  uint32_t y;
};

using bits_addr_t = uint32_t;

struct BitsBlockAlias {
  std::map<std::string, std::string> sites;
  uint32_t start_offset;
  std::string type;
};

struct BitsBlock {
  std::optional<BitsBlockAlias> alias;
  bits_addr_t base_address;
  uint32_t frames;
  uint32_t offset;
  uint32_t words;
};

using Bits = std::map<FrameBlockType, BitsBlock>;

struct Tile {
  // Tile type.
  // Maybe repeated.
  std::string type;

  // Grid coordinates.
  Location coord;

  // Maybe repeated.
  std::optional<std::string> clock_region;

  // Tile configuration bits.
  std::optional<Bits> bits;

  std::map<std::string, std::string> pin_functions;
  std::map<std::string, std::string> sites;
  std::vector<std::string> prohibited_sites;
};

// TODO, for now basic std map, we might want to use
// an unordered map or flat_hash_map from absl.
using TileGrid = std::map<std::string, Tile>;

absl::StatusOr<TileGrid> ParseTileGridJSON(absl::string_view content);

enum class PseudoPIPType {
  kAlways,
  kDefault,
  kHint,
};

// Pseudo Programmable Interconnect Points.
using PseudoPIPs = std::map<std::string, PseudoPIPType>;

// Parse tile-type specific Pseudo pips database.
absl::StatusOr<PseudoPIPs> ParsePseudoPIPsDatabase(absl::string_view content);

struct SegmentBit {
  uint32_t word_column;
  uint32_t word_bit;
  bool is_set;
};

using SegmentsBits = std::map<std::string, std::vector<SegmentBit>>;

// Parse tile-type segbits database.
absl::StatusOr<SegmentsBits> ParseSegmentsBitsDatabase(absl::string_view content);
}  // namespace prjxstream
#endif  // PRJXSTREAM_DATABASE_H
