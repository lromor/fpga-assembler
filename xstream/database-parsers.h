#ifndef XSTREAM_DATABASE_PARSERS_H
#define XSTREAM_DATABASE_PARSERS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace xstream {
enum class ConfigBusType {
  kCLBIOCLK,
  kBlockRam,
  kCFGCLB,
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

using Bits = std::map<ConfigBusType, BitsBlock>;

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

// Set of tiles of a specific architecture. Different fpgas might share
// the different "fabric".
// Maps tile names (type.feature) to their metadata.
// For instance the tilegrid item:
// ```
// "CLBLL_L_X16Y149": {
//     "bits": {
//         "CLB_IO_CLK": {
//             "baseaddr": "0x00020800",
//             "frames": 36,
//             "offset": 99,
//             "words": 2
//         }
//     },
//     "clock_region": "X0Y2",
//     "grid_x": 43,
//     "grid_y": 1,
//     "pin_functions": {},
//     "sites": {
//         "SLICE_X24Y149": "SLICEL",
//         "SLICE_X25Y149": "SLICEL"
//     },
//     "type": "CLBLL_L"
// }
// ```
// Corresponds to a key (tile name) CLBLL_L_X16Y149 and tile type: CLBLL_L
using TileGrid = std::map<std::string, Tile>;

enum class PseudoPIPType {
  kAlways,
  kDefault,
  kHint,
};

// Pseudo Programmable Interconnect Points.
using PseudoPIPs = std::map<std::string, PseudoPIPType>;

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
};

inline bool operator<(const TileFeature &lhs, const TileFeature &rhs) {
  return std::tie(lhs.tile_feature, lhs.address) <
         std::tie(rhs.tile_feature, rhs.address);
}

using SegmentsBits = std::map<TileFeature, std::vector<SegmentBit>>;

struct PackagePin {
  std::string pin;
  uint32_t bank;
  std::string site;
  std::string tile;
  std::string pin_function;
};

using PackagePins = std::vector<PackagePin>;

using IOBanksIDsToLocation = std::map<uint32_t, std::string>;

// For each column index, associate a number of frames.
using ConfigColumnsFramesCount = std::vector<uint32_t>;

using ClockRegionRow = std::map<ConfigBusType, ConfigColumnsFramesCount>;

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
absl::StatusOr<Part> ParsePartJSON(absl::string_view content);

// Parses file found at:
// <db-root>/<family>/<part>/package_pins.csv
// Expects a csv file containing:
// ```
// pin,bank,site,tile,pin_function
// A2,216,OPAD_X0Y2,GTP_CHANNEL_1_X97Y121,MGTPTXN1_216
// ```
absl::StatusOr<PackagePins> ParsePackagePins(absl::string_view content);

// Parse pseudo pips associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<PseudoPIPs> ParsePseudoPIPsDatabase(absl::string_view content);

// Parse the segments bits associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<SegmentsBits> ParseSegmentsBitsDatabase(
  absl::string_view content);

absl::StatusOr<std::map<std::string, PartInfo>> ParsePartsInfos(
  absl::string_view parts_mapper_yaml, absl::string_view devices_mapper_yaml);

absl::StatusOr<TileGrid> ParseTileGridJSON(absl::string_view content);
}  // namespace xstream
#endif  // XSTREAM_DATABASE_PARSERS_H
