// Copyright 2025 fpga-assembler authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fpga/ffi/fabric-view.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "fpga/database-parsers.h"
#include "fpga/database.h"
#include "fpga/fasm-parser.h"
#include "fpga/memory-mapped-file.h"

namespace {
// Stable storage for every string the C API hands out.  A deque never moves
// the elements already in it, so the pointers stay valid for as long as the
// owning object lives.
class Arena {
 public:
  fpga_view_str Add(std::string_view text) {
    if (text.empty()) return fpga_view_str{nullptr, 0};
    strings_.emplace_back(text);
    const std::string &stored = strings_.back();
    return fpga_view_str{stored.data(), static_cast<uint32_t>(stored.size())};
  }

 private:
  std::deque<std::string> strings_;
};

// Copies `message` into a freshly allocated NUL-terminated C string, so the
// caller releases it with fpga_view_string_free and never with delete.
char *DupMessage(std::string_view message) {
  char *out = static_cast<char *>(std::malloc(message.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, message.data(), message.size());
  out[message.size()] = '\0';
  return out;
}

void SetError(char **error, std::string_view message) {
  if (error != nullptr) *error = DupMessage(message);
}

fpga_view_bus ToBus(fpga::ConfigBusType bus) {
  switch (bus) {
  case fpga::ConfigBusType::kCLBIOCLK: return FPGA_VIEW_BUS_CLB_IO_CLK;
  case fpga::ConfigBusType::kBlockRam: return FPGA_VIEW_BUS_BLOCK_RAM;
  case fpga::ConfigBusType::kCFGCLB: return FPGA_VIEW_BUS_CFG_CLB;
  }
  return FPGA_VIEW_BUS_CLB_IO_CLK;
}

fpga_view_ppip_type ToPseudoPIPType(fpga::PseudoPIPType type) {
  switch (type) {
  case fpga::PseudoPIPType::kAlways: return FPGA_VIEW_PPIP_ALWAYS;
  case fpga::PseudoPIPType::kDefault: return FPGA_VIEW_PPIP_DEFAULT;
  case fpga::PseudoPIPType::kHint: return FPGA_VIEW_PPIP_HINT;
  }
  return FPGA_VIEW_PPIP_HINT;
}
}  // namespace

// The database plus the lock that serializes access to it.  ConfigBits fills
// a segbits cache on first use of a tile type, so it mutates the database
// even though it reads conceptually; the server calls it from many tasks.
struct fpga_view_db {
  absl::Mutex mutex;
  fpga::PartDatabase database;

  explicit fpga_view_db(fpga::PartDatabase db) : database(std::move(db)) {}
};

struct fpga_view_snapshot {
  Arena arena;
  std::vector<fpga_view_tile> tiles;
  std::vector<fpga_view_site> sites;
  absl::flat_hash_map<std::string, uint32_t> index_by_name;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct fpga_view_eval {
  Arena arena;
  std::vector<fpga_view_feature> features;
  std::vector<fpga_view_frame_bit> frame_bits;
  std::string diagnostics;
  uint32_t parse_result = 0;
};

struct fpga_view_catalog {
  Arena arena;
  std::vector<fpga_view_catalog_entry> entries;
  std::vector<fpga_view_segbit> segbits;
  std::vector<fpga_view_ppip> ppips;
};

struct fpga_view_parts {
  Arena arena;
  struct Entry {
    fpga_view_str name;
    fpga_view_str device;
    fpga_view_str fabric;
    fpga_view_str package;
    fpga_view_str speedgrade;
  };
  std::vector<Entry> entries;
};

size_t fpga_view_abi_sizeof(fpga_view_abi_struct which) {
  switch (which) {
  case FPGA_VIEW_ABI_STR: return sizeof(fpga_view_str);
  case FPGA_VIEW_ABI_BITS_BLOCK: return sizeof(fpga_view_bits_block);
  case FPGA_VIEW_ABI_TILE: return sizeof(fpga_view_tile);
  case FPGA_VIEW_ABI_SITE: return sizeof(fpga_view_site);
  case FPGA_VIEW_ABI_FRAME_BIT: return sizeof(fpga_view_frame_bit);
  case FPGA_VIEW_ABI_FEATURE: return sizeof(fpga_view_feature);
  case FPGA_VIEW_ABI_CATALOG_ENTRY: return sizeof(fpga_view_catalog_entry);
  case FPGA_VIEW_ABI_SEGBIT: return sizeof(fpga_view_segbit);
  case FPGA_VIEW_ABI_PPIP: return sizeof(fpga_view_ppip);
  }
  return 0;
}

uint32_t fpga_view_abi_version(void) { return 1; }

void fpga_view_string_free(char *error) { std::free(error); }

fpga_view_parts *fpga_view_parts_open(const char *db_root, char **error) {
  if (db_root == nullptr) {
    SetError(error, "db_root is null");
    return nullptr;
  }
  const std::filesystem::path root(db_root);
  const auto parts_yaml = fpga::MemoryMapFile(root / "mapping" / "parts.yaml");
  if (!parts_yaml.ok()) {
    SetError(error, parts_yaml.status().ToString());
    return nullptr;
  }
  const auto devices_yaml =
    fpga::MemoryMapFile(root / "mapping" / "devices.yaml");
  if (!devices_yaml.ok()) {
    SetError(error, devices_yaml.status().ToString());
    return nullptr;
  }
  const auto infos = fpga::ParsePartsInfos(
    parts_yaml.value()->AsStringView(), devices_yaml.value()->AsStringView());
  if (!infos.ok()) {
    SetError(error, infos.status().ToString());
    return nullptr;
  }
  auto parts = std::make_unique<fpga_view_parts>();
  std::vector<std::string> names;
  names.reserve(infos->size());
  for (const auto &entry : *infos) names.push_back(entry.first);
  std::ranges::sort(names);
  parts->entries.reserve(names.size());
  for (const std::string &name : names) {
    const fpga::PartInfo &info = infos->at(name);
    parts->entries.push_back(fpga_view_parts::Entry{
      .name = parts->arena.Add(name),
      .device = parts->arena.Add(info.device),
      .fabric = parts->arena.Add(info.fabric),
      .package = parts->arena.Add(info.package),
      .speedgrade = parts->arena.Add(info.speedgrade),
    });
  }
  return parts.release();
}

void fpga_view_parts_close(fpga_view_parts *parts) { delete parts; }

uint32_t fpga_view_parts_count(const fpga_view_parts *parts) {
  if (parts == nullptr) return 0;
  return static_cast<uint32_t>(parts->entries.size());
}

int fpga_view_parts_get(const fpga_view_parts *parts, uint32_t index,
                        fpga_view_str *name, fpga_view_str *device,
                        fpga_view_str *fabric, fpga_view_str *package,
                        fpga_view_str *speedgrade) {
  if (parts == nullptr || index >= parts->entries.size()) return 0;
  const fpga_view_parts::Entry &entry = parts->entries[index];
  if (name != nullptr) *name = entry.name;
  if (device != nullptr) *device = entry.device;
  if (fabric != nullptr) *fabric = entry.fabric;
  if (package != nullptr) *package = entry.package;
  if (speedgrade != nullptr) *speedgrade = entry.speedgrade;
  return 1;
}

fpga_view_db *fpga_view_db_open(const char *db_root, const char *part,
                                char **error) {
  if (db_root == nullptr || part == nullptr) {
    SetError(error, "db_root and part must not be null");
    return nullptr;
  }
  absl::StatusOr<fpga::PartDatabase> database =
    fpga::PartDatabase::Parse(db_root, part);
  if (!database.ok()) {
    SetError(error, database.status().ToString());
    return nullptr;
  }
  return new fpga_view_db(std::move(database).value());
}

void fpga_view_db_close(fpga_view_db *db) { delete db; }

fpga_view_snapshot *fpga_view_db_snapshot(fpga_view_db *db, char **error) {
  if (db == nullptr) {
    SetError(error, "db is null");
    return nullptr;
  }
  const absl::MutexLock lock(db->mutex);
  const fpga::TileGrid &grid = db->database.tiles().grid;

  // Sorting by name keeps the tile indices stable between runs, so a client
  // may cache them alongside the snapshot it downloaded.
  std::vector<std::string> names;
  names.reserve(grid.size());
  for (const auto &entry : grid) names.push_back(entry.first);
  std::ranges::sort(names);

  auto snapshot = std::make_unique<fpga_view_snapshot>();
  snapshot->tiles.reserve(names.size());
  snapshot->index_by_name.reserve(names.size());
  for (const std::string &name : names) {
    const fpga::Tile &tile = grid.at(name);
    const auto tile_index = static_cast<uint32_t>(snapshot->tiles.size());
    snapshot->index_by_name.insert({name, tile_index});

    fpga_view_tile out{};
    out.name = snapshot->arena.Add(name);
    out.type = snapshot->arena.Add(tile.type);
    out.clock_region = tile.clock_region.has_value()
                         ? snapshot->arena.Add(*tile.clock_region)
                         : fpga_view_str{nullptr, 0};
    out.grid_x = tile.coord.x;
    out.grid_y = tile.coord.y;
    out.site_first = static_cast<uint32_t>(snapshot->sites.size());
    out.site_count = 0;
    out.bits_block_count = 0;

    // Sites, in name order for the same reason as the tiles.
    std::vector<std::string> site_names;
    site_names.reserve(tile.sites.size());
    for (const auto &site : tile.sites) site_names.push_back(site.first);
    std::ranges::sort(site_names);
    for (const std::string &site_name : site_names) {
      snapshot->sites.push_back(fpga_view_site{
        .name = snapshot->arena.Add(site_name),
        .type = snapshot->arena.Add(tile.sites.at(site_name)),
        .tile_index = tile_index,
      });
      ++out.site_count;
    }

    // By bus, so a tile that has more than one block (the BRAM tiles have
    // both a CLB_IO_CLK and a BLOCK_RAM one) always reports them in the same
    // order.  The grid is a hash map, and a caller that draws "the first
    // block" must not get a different one from run to run.
    std::vector<fpga::ConfigBusType> buses;
    buses.reserve(tile.bits.size());
    for (const auto &bits_entry : tile.bits) buses.push_back(bits_entry.first);
    std::ranges::sort(buses,
                      [](fpga::ConfigBusType lhs, fpga::ConfigBusType rhs) {
                        return static_cast<int>(lhs) < static_cast<int>(rhs);
                      });
    for (const fpga::ConfigBusType bus : buses) {
      if (out.bits_block_count >= 3) break;
      const fpga::BitsBlock &block = tile.bits.at(bus);
      fpga_view_bits_block &block_out = out.bits_blocks[out.bits_block_count++];
      block_out.bus = ToBus(bus);
      block_out.base_address = block.base_address;
      block_out.frames = block.frames;
      block_out.offset = block.offset;
      block_out.words = block.words;
      if (block.alias.has_value()) {
        block_out.alias_type = snapshot->arena.Add(block.alias->type);
        block_out.alias_start_offset = block.alias->start_offset;
      } else {
        block_out.alias_type = fpga_view_str{nullptr, 0};
        block_out.alias_start_offset = 0;
      }
    }

    snapshot->width = std::max(snapshot->width, tile.coord.x + 1);
    snapshot->height = std::max(snapshot->height, tile.coord.y + 1);
    snapshot->tiles.push_back(out);
  }
  return snapshot.release();
}

void fpga_view_snapshot_close(fpga_view_snapshot *snapshot) { delete snapshot; }

const fpga_view_tile *fpga_view_snapshot_tiles(
  const fpga_view_snapshot *snapshot, uint32_t *count) {
  if (snapshot == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) *count = static_cast<uint32_t>(snapshot->tiles.size());
  return snapshot->tiles.data();
}

const fpga_view_site *fpga_view_snapshot_sites(
  const fpga_view_snapshot *snapshot, uint32_t *count) {
  if (snapshot == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) *count = static_cast<uint32_t>(snapshot->sites.size());
  return snapshot->sites.data();
}

void fpga_view_snapshot_extent(const fpga_view_snapshot *snapshot,
                               uint32_t *width, uint32_t *height) {
  if (width != nullptr) *width = snapshot != nullptr ? snapshot->width : 0;
  if (height != nullptr) *height = snapshot != nullptr ? snapshot->height : 0;
}

int32_t fpga_view_snapshot_find_tile(const fpga_view_snapshot *snapshot,
                                     const char *name) {
  if (snapshot == nullptr || name == nullptr) return -1;
  const auto it = snapshot->index_by_name.find(std::string(name));
  if (it == snapshot->index_by_name.end()) return -1;
  return static_cast<int32_t>(it->second);
}

namespace {
// One FASM assignment as the parser reported it, before it is resolved.
struct ParsedFeature {
  uint32_t line;
  std::string name;
  int start_bit;
  int width;
  uint64_t bits;
};
}  // namespace

fpga_view_eval *fpga_view_db_eval(fpga_view_db *db,
                                  const fpga_view_snapshot *snapshot,
                                  const char *text, char **error) {
  if (db == nullptr || text == nullptr) {
    SetError(error, "db and text must not be null");
    return nullptr;
  }
  std::string content(text);
  if (content.empty() || content.back() != '\n') content.push_back('\n');

  // fasm::Parse reports to a FILE*; capture it so the client sees exactly
  // the diagnostics the assembler would have printed.
  char *diagnostics_buffer = nullptr;
  size_t diagnostics_size = 0;
  FILE *diagnostics = open_memstream(&diagnostics_buffer, &diagnostics_size);
  if (diagnostics == nullptr) {
    SetError(error, "could not open the diagnostics stream");
    return nullptr;
  }

  std::vector<ParsedFeature> parsed;
  const fasm::ParseResult parse_result =
    fasm::Parse(content, diagnostics,
                [&parsed](uint32_t line, std::string_view feature,
                          int start_bit, int width, uint64_t bits) -> bool {
                  parsed.push_back(ParsedFeature{line, std::string(feature),
                                                 start_bit, width, bits});
                  return true;
                });
  std::fflush(diagnostics);

  auto eval = std::make_unique<fpga_view_eval>();
  eval->parse_result = static_cast<uint32_t>(parse_result);
  if (diagnostics_buffer != nullptr) {
    eval->diagnostics.assign(diagnostics_buffer, diagnostics_size);
  }
  std::fclose(diagnostics);
  std::free(diagnostics_buffer);

  const absl::MutexLock lock(db->mutex);
  eval->features.reserve(parsed.size());
  for (const ParsedFeature &feature : parsed) {
    fpga_view_feature out{};
    out.line = feature.line;
    out.feature = eval->arena.Add(feature.name);
    out.start_bit = feature.start_bit;
    out.width = feature.width;
    out.value_bits = feature.bits;
    out.tile_index = -1;
    out.frame_bit_first = static_cast<uint32_t>(eval->frame_bits.size());
    out.frame_bit_count = 0;
    out.error = fpga_view_str{nullptr, 0};

    // "CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT" splits into the tile name and the
    // feature within it, exactly as the assembler splits it.
    const std::vector<std::string> segments =
      absl::StrSplit(feature.name, absl::MaxSplits('.', 1));
    if (segments.size() != 2) {
      out.outcome = FPGA_VIEW_OUTCOME_ERROR;
      out.error = eval->arena.Add(
        absl::StrFormat("cannot split feature name %s", feature.name));
      eval->features.push_back(out);
      continue;
    }
    const std::string &tile_name = segments[0];
    const std::string &tile_feature = segments[1];
    out.tile = eval->arena.Add(tile_name);
    out.tile_feature = eval->arena.Add(tile_feature);

    // The tile's own bit window, used to place each bit inside the tile.
    uint64_t base_address_by_bus[3] = {0, 0, 0};
    int32_t offset_by_bus[3] = {0, 0, 0};
    const fpga::TileGrid &grid = db->database.tiles().grid;
    const auto tile_it = grid.find(tile_name);
    if (tile_it != grid.end()) {
      out.tile_type = eval->arena.Add(tile_it->second.type);
      for (const auto &bits_entry : tile_it->second.bits) {
        const auto bus = static_cast<size_t>(ToBus(bits_entry.first));
        base_address_by_bus[bus] = bits_entry.second.base_address;
        offset_by_bus[bus] = bits_entry.second.offset;
      }
    }
    if (snapshot != nullptr) {
      const auto index_it = snapshot->index_by_name.find(tile_name);
      if (index_it != snapshot->index_by_name.end()) {
        out.tile_index = static_cast<int32_t>(index_it->second);
      }
    }

    bool any_value_set = false;
    std::string failure;
    for (int addr = 0; addr < feature.width; ++addr) {
      // Only bits set to 1 address a feature, and only the low 64 bits of a
      // wider assignment are known -- the same guard the assembler applies.
      const bool value = addr < 64 && ((feature.bits >> addr) & 1) != 0;
      if (!value) continue;
      any_value_set = true;
      const auto feature_address =
        static_cast<uint32_t>(addr + feature.start_bit);
      const absl::Status status = db->database.ConfigBits(
        tile_name, tile_feature, feature_address,
        [&eval, &out, feature_address, &base_address_by_bus, &offset_by_bus](
          fpga::ConfigBusType bus, uint32_t address,
          const fpga::PartDatabase::FrameBit &bit, bool set) {
          const fpga_view_bus bus_out = ToBus(bus);
          const auto bus_index = static_cast<size_t>(bus_out);
          const auto bit_position =
            static_cast<int64_t>(bit.word) * fpga::kWordSizeBits + bit.index;
          eval->frame_bits.push_back(fpga_view_frame_bit{
            .bus = bus_out,
            .frame_address = address,
            .word = bit.word,
            .index = bit.index,
            .word_column =
              static_cast<uint32_t>(address - base_address_by_bus[bus_index]),
            .word_bit = static_cast<int32_t>(
              bit_position - static_cast<int64_t>(offset_by_bus[bus_index]) *
                               fpga::kWordSizeBits),
            .feature_address = feature_address,
            .value = static_cast<uint8_t>(set ? 1 : 0),
          });
          ++out.frame_bit_count;
        });
      if (!status.ok()) {
        failure = status.message();
        break;
      }
    }

    if (!failure.empty()) {
      out.outcome = FPGA_VIEW_OUTCOME_ERROR;
      out.error = eval->arena.Add(failure);
    } else if (out.frame_bit_count > 0) {
      out.outcome = FPGA_VIEW_OUTCOME_BITS;
    } else if (!any_value_set) {
      out.outcome = FPGA_VIEW_OUTCOME_ZERO_VALUE;
    } else {
      // The database resolved the feature but it drives nothing: that is what
      // a pseudo pip is.
      out.outcome = FPGA_VIEW_OUTCOME_PSEUDO_PIP;
    }
    eval->features.push_back(out);
  }
  return eval.release();
}

void fpga_view_eval_close(fpga_view_eval *eval) { delete eval; }

const fpga_view_feature *fpga_view_eval_features(const fpga_view_eval *eval,
                                                 uint32_t *count) {
  if (eval == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) *count = static_cast<uint32_t>(eval->features.size());
  return eval->features.data();
}

const fpga_view_frame_bit *fpga_view_eval_frame_bits(const fpga_view_eval *eval,
                                                     uint32_t *count) {
  if (eval == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) {
    *count = static_cast<uint32_t>(eval->frame_bits.size());
  }
  return eval->frame_bits.data();
}

fpga_view_str fpga_view_eval_diagnostics(const fpga_view_eval *eval) {
  if (eval == nullptr) return fpga_view_str{nullptr, 0};
  return fpga_view_str{eval->diagnostics.data(),
                       static_cast<uint32_t>(eval->diagnostics.size())};
}

uint32_t fpga_view_eval_parse_result(const fpga_view_eval *eval) {
  if (eval == nullptr) return 0;
  return eval->parse_result;
}

fpga_view_catalog *fpga_view_db_catalog(fpga_view_db *db, const char *tile_type,
                                        char **error) {
  if (db == nullptr || tile_type == nullptr) {
    SetError(error, "db and tile_type must not be null");
    return nullptr;
  }
  const absl::MutexLock lock(db->mutex);
  const std::optional<fpga::SegmentsBitsWithPseudoPIPs> database =
    db->database.tiles().bits(tile_type);
  if (!database.has_value()) {
    SetError(error,
             absl::StrFormat("no database for tile type \"%s\"", tile_type));
    return nullptr;
  }

  auto catalog = std::make_unique<fpga_view_catalog>();
  // Sorted output: the catalog is a list a human reads.
  struct CatalogKey {
    fpga::TileFeature feature;
    fpga::ConfigBusType bus;
  };
  std::vector<CatalogKey> features;
  for (const auto &bus_entry : database->segment_bits) {
    for (const auto &feature_entry : bus_entry.second) {
      features.push_back(CatalogKey{feature_entry.first, bus_entry.first});
    }
  }
  std::ranges::sort(features, [](const CatalogKey &lhs, const CatalogKey &rhs) {
    if (lhs.feature.tile_feature != rhs.feature.tile_feature) {
      return lhs.feature.tile_feature < rhs.feature.tile_feature;
    }
    return lhs.feature.address < rhs.feature.address;
  });

  catalog->entries.reserve(features.size());
  for (const CatalogKey &key : features) {
    const fpga::TileFeature &feature = key.feature;
    const std::vector<fpga::SegmentBit> &bits =
      database->segment_bits.at(key.bus).at(feature);
    fpga_view_catalog_entry entry{};
    entry.name = catalog->arena.Add(feature.tile_feature);
    entry.address = feature.address;
    entry.bus = ToBus(key.bus);
    entry.segbit_first = static_cast<uint32_t>(catalog->segbits.size());
    entry.segbit_count = static_cast<uint32_t>(bits.size());
    for (const fpga::SegmentBit &bit : bits) {
      catalog->segbits.push_back(fpga_view_segbit{
        .word_column = bit.word_column,
        .word_bit = bit.word_bit,
        .is_set = static_cast<uint8_t>(bit.is_set ? 1 : 0),
      });
    }
    catalog->entries.push_back(entry);
  }

  std::vector<std::string> ppip_names;
  ppip_names.reserve(database->pips.size());
  for (const auto &ppip : database->pips) ppip_names.push_back(ppip.first);
  std::ranges::sort(ppip_names);
  catalog->ppips.reserve(ppip_names.size());
  for (const std::string &name : ppip_names) {
    catalog->ppips.push_back(fpga_view_ppip{
      .name = catalog->arena.Add(name),
      .type = ToPseudoPIPType(database->pips.at(name)),
    });
  }
  return catalog.release();
}

void fpga_view_catalog_close(fpga_view_catalog *catalog) { delete catalog; }

const fpga_view_catalog_entry *fpga_view_catalog_entries(
  const fpga_view_catalog *catalog, uint32_t *count) {
  if (catalog == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) {
    *count = static_cast<uint32_t>(catalog->entries.size());
  }
  return catalog->entries.data();
}

const fpga_view_segbit *fpga_view_catalog_segbits(
  const fpga_view_catalog *catalog, uint32_t *count) {
  if (catalog == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) {
    *count = static_cast<uint32_t>(catalog->segbits.size());
  }
  return catalog->segbits.data();
}

const fpga_view_ppip *fpga_view_catalog_ppips(const fpga_view_catalog *catalog,
                                              uint32_t *count) {
  if (catalog == nullptr) {
    if (count != nullptr) *count = 0;
    return nullptr;
  }
  if (count != nullptr) *count = static_cast<uint32_t>(catalog->ppips.size());
  return catalog->ppips.data();
}
