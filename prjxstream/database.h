#ifndef PRJXSTREAM_DATABASE_H
#define PRJXSTREAM_DATABASE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace prjxstream {
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

// TODO, for now basic std map, we might want to use
// an unordered map or flat_hash_map from absl.
// Set of tiles of a specific architecture. Different fpgas might share
// the different "fabric".
using TileGrid = std::map<std::string, Tile>;

absl::StatusOr<TileGrid> ParseTileGridJSON(absl::string_view content);

enum class PseudoPIPType {
  kAlways,
  kDefault,
  kHint,
};

// Pseudo Programmable Interconnect Points.
using TileTypePseudoPIPs = std::map<std::string, PseudoPIPType>;

// Parse pseudo pips associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<TileTypePseudoPIPs> ParsePseudoPIPsDatabase(
  absl::string_view content);

struct SegmentBit {
  uint32_t word_column;
  uint32_t word_bit;
  bool is_set;
};

using TileTypeSegmentsBits = std::map<std::string, std::vector<SegmentBit>>;

// Parse the segments bits associated to each tile that is part
// of a tile sub-type.
absl::StatusOr<TileTypeSegmentsBits> ParseSegmentsBitsDatabase(
  absl::string_view content);

struct PackagePin {
  std::string pin;
  uint32_t bank;
  std::string site;
  std::string tile;
  std::string pin_function;
};

using PackagePins = std::vector<PackagePin>;

// Usually found inside:
// <db-root>/<family>/<part>/package_pins.csv
// Expects a csv file with the first line containing:
// pin,bank,site,tile,pin_function.
absl::StatusOr<PackagePins> ParsePackagePins(absl::string_view content);

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

absl::StatusOr<Part> ParsePartJSON(absl::string_view content);

struct PartInfo {
  std::string device;
  std::string fabric;
  std::string package;
  std::string speedgrade;
};

absl::StatusOr<std::map<std::string, PartInfo>> ParsePartInfo(
    absl::string_view parts_mapper_yaml, absl::string_view devices_mapper_yaml);
}  // namespace prjxstream
#endif  // PRJXSTREAM_DATABASE_H
