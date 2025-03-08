#ifndef FPGA_DATABASE_PARSERS_H
#define FPGA_DATABASE_PARSERS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/container/flat_hash_map.h"

namespace fpga {
enum class ConfigBusType {
  kCLBIOCLK,
  kBlockRam,
  kCFGCLB,
};

struct Location {
  uint32_t x;
  uint32_t y;
};

using bits_addr_t = uint64_t;

struct BitsBlockAlias {
  absl::flat_hash_map<std::string, std::string> sites;
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

using Bits = absl::flat_hash_map<ConfigBusType, BitsBlock>;

struct Tile {
  // Tile type.
  std::string type;

  // Grid coordinates.
  // x: column, increasing right.
  // y: row, increasing down.
  Location coord;

  // Maybe repeated.
  std::optional<std::string> clock_region;

  // Tile configuration bits.
  Bits bits;

  // Indicates the special functions of the Tile pins.
  // Usually it is related to IOB blocks and indicates
  // i.e. differential output pins.
  absl::flat_hash_map<std::string, std::string> pin_functions;

  // Maps <site-name> to <site-type>.
  absl::flat_hash_map<std::string, std::string> sites;

  // Which sites not to use in the tile.
  std::vector<std::string> prohibited_sites;
};

using TileGrid = absl::flat_hash_map<std::string, Tile>;

enum class PseudoPIPType {
  kAlways,
  kDefault,
  kHint,
};

// Pseudo Programmable Interconnect Points.
using PseudoPIPs = absl::flat_hash_map<std::string, PseudoPIPType>;

struct SegmentBit {
  // To which word the bit is part of.
  uint32_t word_column;

  // Word index of the bit to enable.
  uint32_t word_bit;

  // If the char '!' is prepended.
  bool is_set;
};

struct TileFeature {
  // Expecting a tile type and feature in a single string.
  std::string tile_feature;

  // If not specified in the db, is 0 by default.
  uint32_t address;

  template <typename H>
  friend H AbslHashValue(H h, const TileFeature& c) {
    return H::combine(std::move(h), c.tile_feature, c.address);
  }
  auto operator<=>(const TileFeature& o) const = default;
};

using SegmentsBits = absl::flat_hash_map<TileFeature, std::vector<SegmentBit>>;

struct PackagePin {
  std::string pin;
  uint32_t bank;
  std::string site;
  std::string tile;
  std::string pin_function;
};

using PackagePins = std::vector<PackagePin>;

using IOBanksIDsToLocation = absl::flat_hash_map<uint32_t, std::string>;

// For each column index, associate a number of frames.
using ConfigColumnsFramesCount = std::vector<uint32_t>;

using ClockRegionRow = absl::flat_hash_map<ConfigBusType, ConfigColumnsFramesCount>;

using GlobalClockRegionHalf = std::vector<ClockRegionRow>;

struct GlobalClockRegions {
  GlobalClockRegionHalf bottom_rows;
  GlobalClockRegionHalf top_rows;
};

struct Part {
  GlobalClockRegions global_clock_regions;
  uint32_t idcode;
  IOBanksIDsToLocation iobanks;
};

struct PartInfo {
  std::string device;
  std::string fabric;
  std::string package;
  std::string speedgrade;
};

// Parse family part.
// <db-root>/<family>/<part>/part.json.
absl::StatusOr<Part> ParsePartJSON(std::string_view content);

// Parses file found at:
// <db-root>/<family>/<part>/package_pins.csv
// Expects a csv file containing:
// ```
// pin,bank,site,tile,pin_function
// A2,216,OPAD_X0Y2,GTP_CHANNEL_1_X97Y121,MGTPTXN1_216
// ```
absl::StatusOr<PackagePins> ParsePackagePins(std::string_view content);

// Parse pseudo pips associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<PseudoPIPs> ParsePseudoPIPsDatabase(std::string_view content);

// Parse the segments bits associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<SegmentsBits> ParseSegmentsBitsDatabase(
  std::string_view content);

absl::StatusOr<absl::flat_hash_map<std::string, PartInfo>> ParsePartsInfos(
  std::string_view parts_mapper_yaml, std::string_view devices_mapper_yaml);

absl::StatusOr<TileGrid> ParseTileGridJSON(std::string_view content);
}  // namespace fpga
#endif  // FPGA_DATABASE_PARSERS_H
