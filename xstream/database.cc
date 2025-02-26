#include "xstream/database.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_set>

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "xstream/database-parsers.h"
#include "xstream/memory-mapped-file.h"

namespace xstream {
absl::StatusOr<BanksTilesRegistry> BanksTilesRegistry::Create(
  const Part &part, const PackagePins &package_pins) {
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
    banks_to_tiles.insert(
      {pair.first,
       std::vector<std::string>(pair.second.begin(), pair.second.end())});
  }
  return BanksTilesRegistry(tile_to_bank, banks_to_tiles);
}

std::optional<std::vector<std::string>> BanksTilesRegistry::Tiles(
  uint32_t bank) const {
  if (banks_to_tiles_.count(bank) == 0) {
    return {};
  }
  return banks_to_tiles_.at(bank);
}

std::optional<uint32_t> BanksTilesRegistry::TileBank(
  const std::string &tile) const {
  if (tile_to_bank_.count(tile) == 0) {
    return {};
  }
  return tile_to_bank_.at(tile);
}

bool PartDatabase::AddSegbitsToCache(const std::string &tile_type) {
  // Already have the tile type segbits.
  if (segment_bits_cache_.count(tile_type)) {
    return false;
  }
  const std::optional<SegmentsBitsWithPseudoPIPs> maybe_segbits =
    tiles_->bits(tile_type);
  if (!maybe_segbits.has_value()) {
    return false;
  }
  segment_bits_cache_.insert({tile_type, maybe_segbits.value()});
  return true;
}

static absl::StatusOr<PartInfo> ParsePartInfo(
  const std::filesystem::path &prjxray_db_path, const std::string &part) {
  const auto parts_yaml_content_result =
    xstream::MemoryMapFile(prjxray_db_path / "mapping" / "parts.yaml");
  if (!parts_yaml_content_result.ok()) {
    return parts_yaml_content_result.status();
  }
  const auto devices_yaml_content_result =
    xstream::MemoryMapFile(prjxray_db_path / "mapping" / "devices.yaml");
  if (!devices_yaml_content_result.ok()) {
    return devices_yaml_content_result.status();
  }
  const absl::StatusOr<std::map<std::string, PartInfo>> parts_infos_result =
    xstream::ParsePartsInfos(
      parts_yaml_content_result.value()->AsStringView(),
      devices_yaml_content_result.value()->AsStringView());
  if (!parts_infos_result.ok()) {
    return parts_infos_result.status();
  }
  const std::map<std::string, PartInfo> &parts_infos =
    parts_infos_result.value();
  if (!parts_infos.count(part)) {
    return absl::InvalidArgumentError(
      absl::StrFormat("invalid or unknown part \"%s\"", part));
  }
  return parts_infos.at(part);
}

static absl::StatusOr<TileGrid> ParseTileGrid(
  const std::filesystem::path &prjxray_db_path,
  const xstream::PartInfo &part_info) {
  const auto tilegrid_json_content_result =
    MemoryMapFile(prjxray_db_path / part_info.fabric / "tilegrid.json");
  if (!tilegrid_json_content_result.ok()) {
    return tilegrid_json_content_result.status();
  }
  return xstream::ParseTileGridJSON(
    tilegrid_json_content_result.value()->AsStringView());
}

// Stores full absolute paths of the databases for each tile type.
struct TileTypeDatabasePaths {
  // Corresponds to tile_type_<tile-type>.json
  std::filesystem::path tile_type_json;

  // segbits_<tile-type>.block_ram.db
  std::optional<std::filesystem::path> segbits_db;

  // segbits_<tile-type>.block_ram.db
  std::optional<std::filesystem::path> segbits_block_ram_db;

  // ppips_<tile-type>.db
  std::optional<std::filesystem::path> ppips_db;

  // mask_<tile-type>.db
  std::optional<std::filesystem::path> mask_db;
};

constexpr absl::string_view kTileTypeJSONPrefix = "tile_type_";
constexpr absl::string_view kTileTypeJSONSuffix = ".json";
constexpr absl::string_view KFileNameFormatSegbits = "segbits_%s.db";
constexpr absl::string_view KFileNameFormatSegbitsBlockRAM =
  "segbits_%s.block_ram.db";
constexpr absl::string_view KFileNameFormatPseudoPIPs = "ppips_%s.db";
constexpr absl::string_view KFileNameFormatMask = "mask_%s.db";

absl::Status GetDatabasePaths(
  const std::filesystem::path &path,
  std::map<std::string, TileTypeDatabasePaths> &out) {
  const std::string filename = path.filename();
  const std::filesystem::path base_path = path.parent_path();

  // Extract string core of tile_type_<tile-type>.json.
  const size_t core_start = kTileTypeJSONPrefix.size();
  const size_t core_length =
    filename.size() - kTileTypeJSONPrefix.size() - kTileTypeJSONSuffix.size();
  std::string tile_type = filename.substr(core_start, core_length);
  std::string tile_type_lower;
  std::transform(tile_type.begin(), tile_type.end(),
                 std::back_inserter(tile_type_lower),
                 [](unsigned char c) { return std::tolower(c); });

  struct TileTypeDatabasePaths paths;
  paths.tile_type_json = path;
  const std::filesystem::path segbits_db =
    base_path / absl::StrFormat(KFileNameFormatSegbits, tile_type_lower);
  if (std::filesystem::exists(segbits_db)) {
    paths.segbits_db.emplace(segbits_db);
  }

  const std::filesystem::path segbits_block_ram_db =
    base_path /
    absl::StrFormat(KFileNameFormatSegbitsBlockRAM, tile_type_lower);
  if (std::filesystem::exists(segbits_block_ram_db)) {
    paths.segbits_block_ram_db.emplace(segbits_block_ram_db);
  }

  const std::filesystem::path ppips_db =
    base_path / absl::StrFormat(KFileNameFormatPseudoPIPs, tile_type_lower);
  if (std::filesystem::exists(ppips_db)) {
    paths.ppips_db.emplace(ppips_db);
  }

  const std::filesystem::path mask_db =
    base_path / absl::StrFormat(KFileNameFormatMask, tile_type_lower);
  if (std::filesystem::exists(mask_db)) {
    paths.mask_db.emplace(mask_db);
  }
  out.insert({tile_type, paths});
  return absl::OkStatus();
}

absl::Status IndexTileTypes(
  const std::filesystem::path &database_path,
  std::map<std::string, TileTypeDatabasePaths> &tile_types_database_paths) {
  std::error_code ec;
  // Create a recursive directory iterator with options to skip permission
  // errors
  std::filesystem::recursive_directory_iterator it(
    database_path, std::filesystem::directory_options::skip_permission_denied,
    ec);
  std::filesystem::recursive_directory_iterator end;
  if (ec) {
    return absl::InternalError("could not initialize directory iterator");
  }
  for (; it != end; it.increment(ec)) {
    if (ec) {
      std::cerr << "error accessing " << it->path() << ": " << ec.message()
                << std::endl;
      ec.clear();
      continue;
    }

    // Check if the current entry is a regular file without throwing exceptions
    std::error_code file_ec;
    if (std::filesystem::is_regular_file(it->path(), file_ec)) {
      if (file_ec) {
        std::cerr << "error checking file type for " << it->path() << ": "
                  << file_ec.message() << std::endl;
        file_ec.clear();
      } else {
        const std::filesystem::path &path = std::string(it->path());
        const std::string filename = path.filename().string();
        if (absl::StartsWith(filename, kTileTypeJSONPrefix) &&
            absl::EndsWith(filename, kTileTypeJSONSuffix)) {
          absl::Status status =
            GetDatabasePaths(path, tile_types_database_paths);
          if (!status.ok()) return status;
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<SegmentsBitsWithPseudoPIPs> ParseTileTypeDatabase(
  const TileTypeDatabasePaths &paths) {
  SegmentsBitsWithPseudoPIPs out;
  // Parse pseudo pips db.
  if (paths.ppips_db.has_value()) {
    const auto content = xstream::MemoryMapFile(paths.ppips_db.value());
    if (!content.ok()) return content.status();
    auto ppips_db = ParsePseudoPIPsDatabase(content.value()->AsStringView());
    if (!ppips_db.ok()) return ppips_db.status();
    out.pips = ppips_db.value();
  }
  std::map<ConfigBusType, SegmentsBits> &segment_bits = out.segment_bits;
  // Parse segments bits.
  if (paths.segbits_db.has_value()) {
    const auto content = xstream::MemoryMapFile(paths.segbits_db.value());
    if (!content.ok()) return content.status();
    auto segbits_db =
      ParseSegmentsBitsDatabase(content.value()->AsStringView());
    if (!segbits_db.ok()) return segbits_db.status();
    segment_bits.insert({ConfigBusType::kCLBIOCLK, segbits_db.value()});
  }
  if (paths.segbits_block_ram_db.has_value()) {
    const auto content =
      xstream::MemoryMapFile(paths.segbits_block_ram_db.value());
    if (!content.ok()) return content.status();
    auto segbits_db =
      ParseSegmentsBitsDatabase(content.value()->AsStringView());
    if (!segbits_db.ok()) return segbits_db.status();
    segment_bits.insert({ConfigBusType::kBlockRam, segbits_db.value()});
  }
  return out;
}

absl::StatusOr<PartDatabase> PartDatabase::Parse(
  absl::string_view database_path, absl::string_view part_name) {
  const absl::StatusOr<PartInfo> part_info_result =
    ParsePartInfo(std::filesystem::path(database_path), std::string(part_name));
  if (!part_info_result.ok()) {
    return part_info_result.status();
  }
  const PartInfo &part_info = part_info_result.value();

  // Parse tilegrid.
  absl::StatusOr<xstream::TileGrid> tilegrid_result =
    ParseTileGrid(database_path, part_info);
  if (!tilegrid_result.ok()) {
    return tilegrid_result.status();
  }

  std::map<std::string, TileTypeDatabasePaths> tiles_types_databases_paths;
  absl::Status status = IndexTileTypes(std::filesystem::path(database_path),
                                       tiles_types_databases_paths);
  if (!status.ok()) {
    return status;
  }

  auto tiles_database = [paths = std::move(tiles_types_databases_paths)](
                          const std::string &tile_type)
    -> std::optional<SegmentsBitsWithPseudoPIPs> {
    if (paths.count(tile_type)) {
      absl::StatusOr<SegmentsBitsWithPseudoPIPs> tile_type_database_result =
        ParseTileTypeDatabase(paths.at(tile_type));
      if (!tile_type_database_result.ok()) {
        // TODO(lromor): Fix silent failure.
        return {};
      }
      return tile_type_database_result.value();
    }
    return {};
  };
  Tiles tiles_foo(std::move(tilegrid_result.value()),
                  std::move(tiles_database));
  std::shared_ptr<Tiles> tiles = std::make_shared<Tiles>(tiles_foo);
  return absl::StatusOr<PartDatabase>(tiles);
}

// CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT, CLBLM_R_X33Y38 is a tilename.
void PartDatabase::ConfigBits(const std::string &tile_name,
                              const std::string &feature, uint32_t address,
                              const BitSetter &bit_setter) {
  // Given the tilename, get the tile type.
  const Tile &tile = tiles_->grid.at(tile_name);
  const std::optional<SegmentsBitsWithPseudoPIPs> maybe_segment_bits =
    tiles_->bits(tile.type);
  if (!maybe_segment_bits.has_value()) return;
  const SegmentsBitsWithPseudoPIPs &segment_bits = maybe_segment_bits.value();
  const std::string tile_segments_bits_key =
    absl::StrJoin({tile_name, feature}, ".");
  if (segment_bits.pips.count(tile_segments_bits_key)) {
    return;
  }
  // Check if it has an alias. If it does, exit as we don't support it yet.
  const std::map<ConfigBusType, BitsBlock> &bits = tile.bits.value_or(Bits{});
  for (const auto &pair : bits) {
    if (pair.second.alias.has_value()) {
      std::cerr
        << "oh no, found a tile with an alias, we don't support that yet."
        << std::endl;
      exit(EXIT_FAILURE);
    }
  }
  // Search our database of features and get the segbit.
  struct TileFeature tile_feature = {
    .tile_feature = absl::StrJoin({tile.type, feature}, "."),
    .address = address,
  };

  // Fill the cache with the current tile type segbits.
  AddSegbitsToCache(tile.type);
  if (!segment_bits_cache_.count(tile.type)) {
    return;
  }
  const SegmentsBitsWithPseudoPIPs &tile_type_features_bits =
    segment_bits_cache_.at(tile.type);
  // If it's a pseudo pip, skip.
  // TODO(lromor): Is this really necessary? It looks like all pips are
  // different.
  if (tile_type_features_bits.pips.count(tile_feature.tile_feature)) {
    return;
  }
  // The tile name has some specific config bus base addresses.
  for (const auto &config_bus_bits_pair : bits) {
    const ConfigBusType &bus = config_bus_bits_pair.first;
    const uint32_t base_address = config_bus_bits_pair.second.base_address;
    const uint32_t offset = config_bus_bits_pair.second.offset;
    if (!tile_type_features_bits.segment_bits.count(bus)) {
      continue;
    }
    const SegmentsBits &features_segbits =
      tile_type_features_bits.segment_bits.at(bus);
    const auto &segbits = features_segbits.at(tile_feature);
    for (const auto &segbit : segbits) {
      if (segbit.is_set) {
        const uint32_t address = base_address + segbit.word_column;
        const uint32_t bit_pos = offset * kWordSizeBits + segbit.word_bit;
        const FrameBit frame_bit = {
          .word = bit_pos / kWordSizeBits,
          .index = bit_pos % kWordSizeBits,
        };
        bit_setter(address, frame_bit);
      }
    }
  }
}
}  // namespace xstream
