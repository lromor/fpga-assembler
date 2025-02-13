#include "xstream/database.h"

#include <memory>
#include <unordered_set>

#include "xstream/database-parsers.h"
#include "xstream/memory-mapped-file.h"

namespace xstream {
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

class PartDatabase::Impl {
 public:
  explicit Impl(std::shared_ptr<TilesDatabase> tiles) : tiles_(std::move(tiles)) {}

  const struct Tile* Tile(absl::string_view tile_name) {
    if (tiles_->tile_grid.count(std::string(tile_name))) {
      return &tiles_->tile_grid.at(std::string(tile_name));
    }
    return nullptr;
  }

  std::vector<Frame> ComputeSegmentBits(absl::string_view tile_name, uint32_t address) const {
    return {};
  }
 private:
  std::shared_ptr<TilesDatabase> tiles_;
};

const Tile* PartDatabase::Tile(absl::string_view tile_name) const {
  return impl_->Tile(tile_name);
}
std::vector<Frame> PartDatabase::ComputeSegmentBits(absl::string_view tile_name, uint32_t address) const {
  return impl_->ComputeSegmentBits(tile_name, address);
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
      parts_yaml_content_result.value()->AsStringVew(),
      devices_yaml_content_result.value()->AsStringVew());
  if (!parts_infos_result.ok()) {
    return parts_infos_result.status();
  }
  const std::map<std::string, PartInfo> &parts_infos = parts_infos_result.value();
  if (!parts_infos.count(part)) {
    return absl::InvalidArgumentError(absl::StrFormat("invalid or unknown part \"%s\"", part));
  }
  return parts_infos.at(part);
}

static absl::StatusOr<TileGrid> ParseTileGrid(
  const std::filesystem::path &prjxray_db_path, const xstream::PartInfo &part_info) {
  const auto tilegrid_json_content_result =
      MemoryMapFile(prjxray_db_path / part_info.fabric / "tilegrid.json");
  if (!tilegrid_json_content_result.ok()) {
    return tilegrid_json_content_result.status();
  }
  return xstream::ParseTileGridJSON(
      tilegrid_json_content_result.value()->AsStringVew());
}

PartDatabase::~PartDatabase() = default;

PartDatabase::PartDatabase(std::shared_ptr<TilesDatabase> tiles)
    : impl_(std::make_unique<Impl>(std::move(tiles))) {}

absl::StatusOr<PartDatabase> PartDatabase::Create(
    absl::string_view database_path, absl::string_view part_name) {
  const absl::StatusOr<PartInfo> part_info_result = ParsePartInfo(
      std::filesystem::path(database_path), std::string(part_name));
  if (!part_info_result.ok()) {
    return part_info_result.status();
  }
  const PartInfo &part_info = part_info_result.value();

  // Parse tilegrid.
  const absl::StatusOr<xstream::TileGrid> tilegrid_result =
      ParseTileGrid(database_path, part_info);
  if (!tilegrid_result.ok()) {
    return tilegrid_result.status();
  }
  std::shared_ptr<TilesDatabase> tiles = std::make_shared<TilesDatabase>();
  return absl::StatusOr<PartDatabase>(absl::in_place, tiles);
}
}  // namespace xstream
