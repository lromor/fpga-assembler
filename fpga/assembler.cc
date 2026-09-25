#include <sys/types.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "fpga/database-parsers.h"
#include "fpga/database.h"
#include "fpga/fasm-parser.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/bitstream.h"

struct TileSiteInfo {
  std::string tile;
  std::string site;
};

bool FindPUDCBTileSite(const fpga::TileGrid &tilegrid, TileSiteInfo &info) {
  for (const auto &p : tilegrid) {
    const std::string &tile = p.first;
    const fpga::Tile &tileinfo = p.second;
    int y_coord;
    for (const auto &functions_kv : tileinfo.pin_functions) {
      const std::string &site = functions_kv.first;
      const std::string &pin_function = functions_kv.second;
      if (absl::StrContains(pin_function, "PUDC_B")) {
        // https://github.com/chipsalliance/
        // f4pga-xc-fasm/blob/25dc605c9c0896204f0c3425b52a332034cf5e5c/xc_fasm/fasm2frames.py#L100
        const std::string value = std::to_string(site[site.size() - 1]);
        if (!absl::SimpleAtoi(value, &y_coord)) {
          return false;
        }
        info = TileSiteInfo{
          .tile = tile,
          .site = absl::StrFormat("IOB_Y%d", y_coord % 2),
        };
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> GetIOBSites(const fpga::TileGrid &grid,
                                     const std::string &tile_name) {
  std::vector<std::string> out;
  const fpga::Tile &tile = grid.at(tile_name);
  uint32_t site_y;
  for (const auto &site_pair : tile.sites) {
    const std::string_view site_name = site_pair.first;
    const std::string site_y_value =
      std::to_string(site_name[site_name.size() - 1]);
    CHECK(absl::SimpleAtoi(site_y_value, &site_y));
    out.push_back(absl::StrFormat("IOB_Y%d", site_y % 2));
  }
  return out;
}

struct FasmFeature {
  int64_t line;
  std::string name;
  int start_bit;
  int width;
  uint64_t bits;
};

struct TileGridInfoAndSegbits {
  std::string tile_type;
  fpga::Bits bits;
};

static absl::Status ProcessFasmFeatures(
  const std::vector<FasmFeature> &features, fpga::PartDatabase &db,
  fpga::Frames &frames) {
  for (const auto &tile_feature : features) {
    // Get first segment of feature name. That's the tile name.
    // The rest is the feature of that specific tile. For instance:
    //  [tile name   ] [feature          ][e, s] [value ]
    //  CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[31:0]=32'b11111111111111110000000000000000
    std::vector<std::string> tile_feature_segments =
      absl::StrSplit(tile_feature.name, absl::MaxSplits('.', 1));
    if (tile_feature_segments.size() != 2) {
      return absl::InvalidArgumentError(
        absl::StrFormat("cannot split feature name %s", tile_feature.name));
    }
    const std::string tile_name = tile_feature_segments[0];
    const std::string feature = tile_feature_segments[1];
    const uint64_t bits = tile_feature.bits;
    absl::flat_hash_set<fpga::ConfigBusType> used_config_buses;
    // Select only bit addresses with value bit set to 1.  The parser reports
    // at most 64 bits per callback, but guard the shift anyway: a wider width
    // would wrap the shift count and report a wrong address as set.
    for (unsigned addr = 0; addr < tile_feature.width; ++addr) {
      const unsigned feature_addr = (addr + tile_feature.start_bit);
      const bool value = addr < 64 && (bits & (uint64_t(1) << addr));
      if (value) {
        const absl::Status config_status = db.ConfigBits(
          tile_name, feature, feature_addr,
          [&frames, &used_config_buses](
            fpga::ConfigBusType bus, uint32_t address,
            const fpga::PartDatabase::FrameBit &bit, bool value) {
            // Update the list of tile segbits buses used.
            // So we can use it later on to mark all the frames that have been
            // used.
            used_config_buses.insert(bus);

            // Insert the frames at address and enable the right bit.
            if (!frames.contains(address)) {
              frames.insert({address, {}});
            }
            if (value) {
              std::array<fpga::word_t, fpga::kFrameWordCount> &frame =
                frames[address];
              frame[bit.word] |= (1 << bit.index);
            }
          });
        if (!config_status.ok()) {
          return config_status;
        }
      }
    }
    if (used_config_buses.empty()) {
      continue;
    }
    // Get tilegrid info.
    const fpga::Tile &tile_info = db.tiles().grid.at(tile_name);
    for (const auto &bus : used_config_buses) {
      const fpga::BitsBlock &info = tile_info.bits.at(bus);
      for (unsigned i = 0; i < info.frames; ++i) {
        frames.insert({info.base_address + i, {}});
      }
    }
  }
  return absl::OkStatus();
}

// Template that for each line should substitute a tile type and a site.
constexpr std::string_view kPUDCBPullUpFASMLinesTemplate[] = {
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVDS_25_LVTTL_SSTL135_"
  "SSTL15_TMDS_33.IN_ONLY",
  "%s.%s.LVCMOS25_LVCMOS33_LVTTL.IN",
  "%s.%s.PULLTYPE.PULLUP",
};

// The HP bank IOBs of the virtex7 parts support fewer standards, so the
// pullup is spelled with the aliases the HP segbits actually carry.  Cross
// referenced with a Vivado built reference bitstream.
constexpr std::string_view kPUDCBPullUpHpFASMLinesTemplate[] = {
  "%s.%s.LVCMOS12_LVCMOS15.IN",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18.IN",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVDS_25_LVTTL_SSTL135_"
  "SSTL15_TMDS_33.IN_ONLY",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVTTL_SSTL135_SSTL15."
  "SLEW.SLOW",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18.SLEW.SLOW",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_SSTL135_SSTL15.STEPDOWN",
  "%s.%s.PULLTYPE.PULLUP",
};

// The HP pullup also configures the partner site of the same IOB.
constexpr std::string_view kPUDCBPullUpHpPartnerFASMLinesTemplate[] = {
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18.SLEW.SLOW",
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_SSTL135_SSTL15.STEPDOWN",
  "%s.%s.PULLTYPE.PULLDOWN",
};

// Appends a single bit feature to the list of features to assemble.
static void AddFeature(std::vector<FasmFeature> &features,
                       const std::string &name) {
  features.push_back(FasmFeature{
    .line = -1,
    .name = name,
    .start_bit = 0,
    .width = 1,
    .bits = 1,
  });
}

// Adds the input buffer and pullup Vivado programs on an unused PUDC_B pin.
//
// This is opt-in, like the reference implementation's --emit_pudc_b_pullup:
// injecting it unconditionally changes the configuration of every design that
// leaves the pin unused, which is all of them in practice.
static void AddPUDCBFeatures(const fpga::TileGrid &tilegrid,
                             const std::vector<FasmFeature> &features,
                             std::vector<FasmFeature> &out) {
  TileSiteInfo info;
  if (!FindPUDCBTileSite(tilegrid, info)) {
    return;
  }

  // A design that drives the PUDC_B site keeps its own configuration.
  const std::string pudc_b_prefix =
    absl::StrFormat("%s.%s.", info.tile, info.site);
  for (const FasmFeature &feature : features) {
    if (absl::StartsWith(feature.name, pudc_b_prefix)) {
      return;
    }
  }

  const bool pudc_b_is_on_an_hp_bank = absl::StartsWith(info.tile, "LIOB18") ||
                                       absl::StartsWith(info.tile, "RIOB18");
  if (pudc_b_is_on_an_hp_bank) {
    const std::string partner = info.site == "IOB_Y0" ? "IOB_Y1" : "IOB_Y0";
    for (const std::string_view line : kPUDCBPullUpHpFASMLinesTemplate) {
      AddFeature(out, absl::StrFormat(line, info.tile, info.site));
    }
    for (const std::string_view line : kPUDCBPullUpHpPartnerFASMLinesTemplate) {
      AddFeature(out, absl::StrFormat(line, info.tile, partner));
    }
    return;
  }
  for (const std::string_view line : kPUDCBPullUpFASMLinesTemplate) {
    AddFeature(out, absl::StrFormat(line, info.tile, info.site));
  }
}

static void AddStepDownFeatures(const fpga::BanksTilesRegistry &banks,
                                const fpga::TileGrid &grid,
                                std::vector<FasmFeature> &features) {
  // Stores a set of strings <tile-type>.<site>
  absl::flat_hash_set<std::string> used_iob_sites;
  absl::flat_hash_map<uint32_t, absl::flat_hash_set<std::string>>
    stepdown_banks_tags;
  for (const auto &feature : features) {
    if (feature.bits == 0) {
      continue;
    }
    // The tag keeps its dots: a DDR pin's STEPDOWN feature is spelled
    // <io-standard>.<...>.STEPDOWN, and splitting it into four pieces would
    // leave the STEPDOWN marker in the discarded piece, so the whole bank
    // would silently miss its stepdown fill.
    std::vector<std::string> tile_feature_segments =
      absl::StrSplit(feature.name, absl::MaxSplits('.', 2));
    if (tile_feature_segments.size() < 3) {
      continue;
    }
    const std::string_view tile = tile_feature_segments[0];
    const std::string_view site = tile_feature_segments[1];
    const std::string_view tag = tile_feature_segments[2];
    if (absl::StrContains(tile, "IOB33")) {
      used_iob_sites.insert(absl::StrFormat("%s.%s", tile, site));
    }

    if (absl::StrContains(tag, "STEPDOWN")) {
      const std::vector<uint32_t> bank_values =
        banks.TileBanks(std::string(tile));
      CHECK(!bank_values.empty());
      const uint32_t bank = bank_values.front();
      if (!stepdown_banks_tags.contains(bank)) {
        stepdown_banks_tags.insert({bank, {}});
      }
      stepdown_banks_tags.at(bank).insert(std::string(tag));
    }
  }

  for (const auto &bank_tags_pair : stepdown_banks_tags) {
    const uint32_t &bank = bank_tags_pair.first;
    const absl::flat_hash_set<std::string> &tags = bank_tags_pair.second;
    const auto maybe_tiles = banks.Tiles(bank);
    CHECK(maybe_tiles.has_value());
    for (const auto &tile : maybe_tiles.value()) {
      if (absl::StrContains(tile, "IOB33")) {
        for (const auto &site : GetIOBSites(grid, tile)) {
          const std::string tile_site = absl::StrFormat("%s.%s", tile, site);
          if (used_iob_sites.contains(tile_site)) {
            continue;
          }
          for (const auto &tag : tags) {
            const FasmFeature feature = {
              .line = -1,
              .name = absl::StrFormat("%s.%s", tile_site, tag),
              .start_bit = 0,
              .width = 1,
              .bits = 1,
            };
            features.push_back(feature);
          }
        }
      }

      // The bank anchor tile is HCLK_IOI3 where the bank uses HR IOLOGIC and
      // HCLK_IOI on the HP-only parts (virtex7); both carry the STEPDOWN
      // feature.
      const bool tile_is_a_bank_anchor = absl::StrContains(tile, "HCLK_IOI3") ||
                                         absl::StartsWith(tile, "HCLK_IOI_");
      if (tile_is_a_bank_anchor) {
        const FasmFeature feature = {
          .line = -1,
          .name = absl::StrFormat("%s.STEPDOWN", tile),
          .start_bit = 0,
          .width = 1,
          .bits = 1,
        };
        features.push_back(feature);
      }
    }
  }
}

// Returns true when the database documents a feature on a tile.  The glue
// below only exists in databases that have been annotated with the matching
// features, so each rule has to be probed before it is injected.
using FeatureProbe =
  std::function<bool(const std::string &tile, const std::string &feature)>;

// HP bank IOBs need driver and input enable bits that Vivado programs for
// every used IOB and that the HR bank IOBs get from their factory defaults.
static void AddHpBankGlueFeatures(const std::vector<FasmFeature> &features,
                                  const std::string &pudc_b_tile,
                                  const FeatureProbe &has_feature,
                                  std::vector<FasmFeature> &out) {
  struct SiteUsage {
    bool in = false;
    bool out = false;
    bool diff_in = false;
  };
  // Maps "<tile>.<site>" to how the site is used.
  absl::flat_hash_map<std::string, SiteUsage> site_usage;
  bool any_lio_b18_y1_out = false;
  for (const FasmFeature &feature : features) {
    if (feature.bits == 0) {
      continue;
    }
    const std::vector<std::string> segments =
      absl::StrSplit(feature.name, absl::MaxSplits('.', 2));
    if (segments.size() < 3) {
      continue;
    }
    const std::string &tile = segments[0];
    const std::string &site = segments[1];
    const std::string &tag = segments[2];
    const bool tile_is_an_hp_iob =
      absl::StartsWith(tile, "LIOB18") || absl::StartsWith(tile, "RIOB18");
    const bool site_is_an_iob = site == "IOB_Y0" || site == "IOB_Y1";
    if (!tile_is_an_hp_iob || !site_is_an_iob) {
      continue;
    }
    // Direction heuristic: a pure ".IN"/".IN_ONLY" marks an IBUF, while only
    // ".DRIVE." marks an OBUF.  Slew and output tags are also present on IBUF
    // tiles as bank wide defaults, so they would misclassify an IBUF as an
    // inout buffer and inject spurious output enable bits.
    SiteUsage &usage = site_usage[absl::StrFormat("%s.%s", tile, site)];
    if (absl::StrContains(tag, "IN_DIFF")) {
      usage.diff_in = true;
    } else if (absl::StrContains(tag, "IN_ONLY") ||
               absl::EndsWith(tag, ".IN") ||
               absl::StrContains(tag, "IBUFDISABLE")) {
      usage.in = true;
    }
    if (absl::StrContains(tag, ".DRIVE.")) {
      usage.out = true;
    }
    if (absl::StartsWith(tile, "LIOB18_X81") && site == "IOB_Y1" &&
        absl::StrContains(tag, ".DRIVE.")) {
      any_lio_b18_y1_out = true;
    }
  }

  for (const auto &usage_pair : site_usage) {
    const std::vector<std::string> tile_site =
      absl::StrSplit(usage_pair.first, '.');
    if (tile_site.size() != 2) {
      continue;
    }
    const std::string &tile = tile_site[0];
    const std::string &site = tile_site[1];
    // The PUDC_B pin's pullup is a virtual tie rather than a placed input
    // buffer, and Vivado does not bank glue it.
    if (tile == pudc_b_tile) {
      continue;
    }
    const SiteUsage &usage = usage_pair.second;
    if (usage.in) {
      const std::string glue = absl::StrFormat("%s.IBUF_HP_BANK_GLUE", site);
      if (has_feature(tile, glue)) {
        AddFeature(out, absl::StrFormat("%s.%s", tile, glue));
      }
    }
    if (usage.out) {
      const std::string glue = absl::StrFormat("%s.OBUF_HP_BANK_GLUE", site);
      if (has_feature(tile, glue)) {
        AddFeature(out, absl::StrFormat("%s.%s", tile, glue));
      }
    }
    if (usage.diff_in) {
      // Only the master site of a differential pair carries the pattern.
      const std::string glue = "IOB_Y0.IBUFDS_BANK_GLUE";
      if (has_feature(tile, glue)) {
        AddFeature(out, absl::StrFormat("%s.%s", tile, glue));
      }
    }
  }

  // A Y1 output buffer anywhere on the X81 LIOB18 column lights that column's
  // "bank active" indicator in the bottom tile of the X32 routing spine.
  if (any_lio_b18_y1_out) {
    for (const std::string_view tag :
         {"IOB_COL_OBUF_CASCADE_Y1", "IOB_COL_BANK_ACTIVE"}) {
      if (has_feature("INT_L_X32Y49", std::string(tag))) {
        AddFeature(out, absl::StrFormat("INT_L_X32Y49.%s", tag));
      }
    }
  }
}

// An output buffer whose T input is tied to GND through general routing lights
// a "this tie route is in use" marker in a mirror tile of the same column.
static void AddGfanTieRootFeatures(const std::vector<FasmFeature> &features,
                                   const FeatureProbe &has_feature,
                                   std::vector<FasmFeature> &out) {
  std::string tie_root_tile;
  for (const FasmFeature &feature : features) {
    if (feature.bits == 0 || !absl::StartsWith(feature.name, "INT_L_X62")) {
      continue;
    }
    if (!absl::StrContains(feature.name, "GFAN0.GND_WIRE")) {
      continue;
    }
    // The marker lives in the tile ten rows below the one carrying the tie.
    const std::vector<std::string> tile_feature =
      absl::StrSplit(feature.name, absl::MaxSplits('.', 1));
    const std::vector<std::string> column_and_row =
      absl::StrSplit(tile_feature[0], 'X');
    if (column_and_row.size() != 2) {
      continue;
    }
    const std::vector<std::string> row = absl::StrSplit(column_and_row[1], 'Y');
    if (row.size() != 2) {
      continue;
    }
    uint32_t y = 0;
    if (!absl::SimpleAtoi(row[1], &y)) {
      continue;
    }
    tie_root_tile =
      absl::StrFormat("%sX%sY%u", column_and_row[0], row[0], y + 10);
    break;
  }
  if (!tie_root_tile.empty() &&
      has_feature(tie_root_tile, "GFAN_TIE_ROOT_GLUE")) {
    AddFeature(out, absl::StrFormat("%s.GFAN_TIE_ROOT_GLUE", tie_root_tile));
  }
}

// Every BUFR channel in use lights one extra "channel active" bit on the
// HCLK_L tile carrying it.
static void AddBufrClkActiveFeatures(const std::vector<FasmFeature> &features,
                                     const FeatureProbe &has_feature,
                                     std::vector<FasmFeature> &out) {
  // Maps an HCLK_L tile to the BUFR channels in use in it.
  absl::flat_hash_map<std::string, absl::flat_hash_set<int>> channels;
  for (const FasmFeature &feature : features) {
    if (feature.bits == 0) {
      continue;
    }
    const std::vector<std::string> segments =
      absl::StrSplit(feature.name, absl::MaxSplits('.', 2));
    if (segments.size() < 3) {
      continue;
    }
    const std::string &tile = segments[0];
    if (!absl::StartsWith(tile, "HCLK_L")) {
      continue;
    }
    // HCLK_L_X..Y....HCLK_LEAF_CLK_B_TOP[0-5].HCLK_CK_BUFRCLK[0-3]
    constexpr std::string_view kBufrClkTag = "HCLK_CK_BUFRCLK";
    const std::string &tag = segments[2];
    const size_t tag_pos = tag.rfind(kBufrClkTag);
    if (tag_pos == std::string::npos) {
      continue;
    }
    int channel = 0;
    if (!absl::SimpleAtoi(tag.substr(tag_pos + kBufrClkTag.size()), &channel)) {
      continue;
    }
    channels[tile].insert(channel);
  }
  for (const auto &channels_pair : channels) {
    for (const int channel : channels_pair.second) {
      const std::string marker =
        absl::StrFormat("HCLK_LEAF_BUFRCLK%d_ACTIVE", channel);
      if (has_feature(channels_pair.first, marker)) {
        AddFeature(out, absl::StrFormat("%s.%s", channels_pair.first, marker));
      }
    }
  }
}

// Same name as the reference implementation's flag, and likewise off by
// default.
ABSL_FLAG(
  bool, emit_pudc_b_pullup, false,
  R"(Emit an IBUF and PULLUP on the PUDC_B pin if the design leaves it unused.)");

// Parses a FASM stream into the feature list.
static absl::Status ParseFasmFile(FILE *input_stream,
                                  std::vector<FasmFeature> &features) {
  size_t buf_size = 8192;
  char *buffer = (char *)malloc(buf_size);
  const absl::Cleanup buffer_freer = [&buffer] { free(buffer); };
  ssize_t read_count;
  // NOLINTNEXTLINE(misc-include-cleaner)
  while ((read_count = getline(&buffer, &buf_size, input_stream)) > 0) {
    const std::string_view content(buffer, read_count);
    const fasm::ParseResult result = fasm::Parse(
      content, stderr,
      [&features](uint32_t line, std::string_view feature_name, int start_bit,
                  int width, uint64_t bits) -> bool {
        features.push_back(
          FasmFeature{line, std::string(feature_name), start_bit, width, bits});
        return true;
      },
      [](uint32_t, std::string_view, std::string_view name,
         std::string_view value) {});

    if (result == fasm::ParseResult::kUserAbort ||
        result == fasm::ParseResult::kError) {
      return absl::InternalError("internal error");
    }
  }
  return absl::OkStatus();
}

static absl::Status AssembleFrames(FILE *input_stream,
                                   const std::string &db_path,
                                   const std::string &part_name,
                                   fpga::PartDatabase &db,
                                   fpga::Frames &frames) {
  // For now store everything in here.
  std::vector<FasmFeature> features;
  // TODO: add roi.

  // Parse fasm.
  const absl::Status parse_status = ParseFasmFile(input_stream, features);
  if (!parse_status.ok()) {
    return parse_status;
  }
  // The features a part requires regardless of the design, e.g. the
  // configuration of the zynq7 processing system.  Appended after the design's
  // own features, like the reference implementation does.
  const std::string required_features_path =
    absl::StrFormat("%s/%s/required_features.fasm", db_path, part_name);
  if (std::filesystem::exists(required_features_path)) {
    FILE *required_features = fopen(required_features_path.c_str(), "r");
    if (required_features == nullptr) {
      return absl::InvalidArgumentError(
        absl::StrFormat("cannot open %s", required_features_path));
    }
    const absl::Cleanup required_features_closer = [required_features] {
      std::fclose(required_features);
    };
    const absl::Status required_status =
      ParseFasmFile(required_features, features);
    if (!required_status.ok()) {
      return required_status;
    }
  }
  // Auto-injected configuration, in the order the reference implementation
  // applies it.  The features the tool parses out of the input stay first.
  TileSiteInfo pudc_b_info;
  const bool pudc_b_is_known = FindPUDCBTileSite(db.tiles().grid, pudc_b_info);
  const std::string pudc_b_tile =
    pudc_b_is_known ? pudc_b_info.tile : std::string();
  const FeatureProbe has_feature = [&db](const std::string &tile,
                                         const std::string &feature) {
    return db.HasFeature(tile, feature);
  };

  std::vector<FasmFeature> injected;
  if (absl::GetFlag(FLAGS_emit_pudc_b_pullup)) {
    AddPUDCBFeatures(db.tiles().grid, features, injected);
  }
  AddStepDownFeatures(db.tiles().banks, db.tiles().grid, features);
  AddHpBankGlueFeatures(features, pudc_b_tile, has_feature, injected);
  AddGfanTieRootFeatures(features, has_feature, injected);
  AddBufrClkActiveFeatures(features, has_feature, injected);
  features.insert(features.end(), injected.begin(), injected.end());
  return ProcessFasmFeatures(features, db, frames);
}

// Writes the assembled frames in the same text format fasm2frames uses: one
// line per frame, the frame address followed by its words in hex.  Comparing
// that output against the reference implementation's .frames file is how the
// two assemblers are diffed.
ABSL_FLAG(std::optional<std::string>, dump_frames_file, std::nullopt,
          R"(Also write the assembled frames to this file, in the text format
fasm2frames uses ("0x<address> 0x<word>,0x<word>,...").)");

ABSL_FLAG(
  std::optional<std::string>, prjxray_db_path, std::nullopt,
  R"(Path to root folder containing the prjxray database for the FPGA family.
If not present, it must be provided via PRJXRAY_DB_PATH.)");

ABSL_FLAG(std::string, part, "", R"(FPGA part name, e.g. "xc7a35tcsg324-1".)");

static inline std::string Usage(std::string_view name) {
  return absl::StrFormat(R"(usage: %s [options] < input.fasm > output.bit

This tool parses a sequence of fasm lines and assembles them
into a set of frames then mapped into bitstream.
Output is written to stdout.)",
                         name);
}

static std::string StatusToErrorMessage(std::string_view message,
                                        const absl::Status &status) {
  return absl::StrFormat("%s: %s", message, status.message());
}

static absl::StatusOr<std::string> GetOptFlagOrFromEnv(
  const absl::Flag<std::optional<std::string>> &flag, const char *env_var) {
  const std::optional<std::string> flag_value = absl::GetFlag(flag);
  if (!flag_value.has_value()) {
    const char *value = getenv(env_var);
    if (value == nullptr) {
      return absl::InvalidArgumentError(
        absl::StrFormat("flag \"%s\" not provided either via commandline or "
                        "environment variable (%s)",
                        flag.Name(), env_var));
    }
    return std::string(value);
  }
  return flag_value.value();
}

// Text dumps of the assembled frames.  The format matches fasm2frames, so the
// output of one assembler can be diffed against the other's.
static void GetFrameLine(
  const uint32_t address,
  const std::array<fpga::word_t, fpga::kFrameWordCount> &bits,
  std::ostream &out) {
  out << absl::StrFormat("0x%08X ", address);
  std::vector<std::string> words;
  words.reserve(bits.size());
  for (const unsigned int word : bits) {
    words.push_back(absl::StrFormat("0x%08X", word));
  }
  out << absl::StrJoin(words, ",");
  out << "\n";
}

static void PrintFrames(const fpga::Frames &frames, std::ostream &out) {
  for (const auto &frame : frames) {
    GetFrameLine(frame.first, frame.second, out);
  }
}

int main(int argc, char *argv[]) {
  const std::string usage = Usage(argv[0]);
  absl::SetProgramUsageMessage(usage);
  const std::vector<char *> args = absl::ParseCommandLine(argc, argv);
  const auto args_count = args.size();
  if (args_count > 2) {
    std::cerr << absl::ProgramUsageMessage() << '\n';
    return 1;
  }
  const absl::StatusOr<std::string> prjxray_db_path_result =
    GetOptFlagOrFromEnv(FLAGS_prjxray_db_path, "PRJXRAY_DB_PATH");
  if (!prjxray_db_path_result.ok()) {
    std::cerr << StatusToErrorMessage("get prjxray db path",
                                      prjxray_db_path_result.status())
              << '\n';
    std::cerr << absl::ProgramUsageMessage() << '\n';
    return 1;
  }
  const std::filesystem::path &prjxray_db_path = prjxray_db_path_result.value();
  if (prjxray_db_path.empty() || !std::filesystem::exists(prjxray_db_path)) {
    std::cerr << absl::StrFormat("invalid prjxray-db path: \"%s\"",
                                 prjxray_db_path)
              << '\n';
    return EXIT_FAILURE;
  }

  const std::string part = absl::GetFlag(FLAGS_part);
  if (part.empty()) {
    std::cerr << "no part provided" << '\n';
    std::cerr << absl::ProgramUsageMessage() << '\n';
    return EXIT_FAILURE;
  }
  auto part_database_result =
    fpga::PartDatabase::Parse(prjxray_db_path.string(), part);
  if (!part_database_result.ok()) {
    std::cerr << StatusToErrorMessage("part mapping parsing",
                                      part_database_result.status())
              << '\n';
    return EXIT_FAILURE;
  }
  FILE *input_stream = stdin;
  const absl::Cleanup file_closer = [input_stream] {
    if (input_stream != stdin) {
      std::fclose(input_stream);
    }
  };
  if (args_count == 2) {
    const std::string_view arg(args[1]);
    if (arg != "-") {
      input_stream = std::fopen(args[1], "r");
      if (input_stream == nullptr) {
        std::cerr << absl::ErrnoToStatus(errno, "cannot open fasm file")
                  << "\n";
        return 1;
      }
    }
  }
  fpga::Frames frames;
  const auto assembler_result =
    AssembleFrames(input_stream, prjxray_db_path.string(), part,
                   part_database_result.value(), frames);
  if (!assembler_result.ok()) {
    std::cerr << StatusToErrorMessage("could not assemble frames",
                                      assembler_result)
              << '\n';
    return EXIT_FAILURE;
  }
  const fpga::Part &part_data = part_database_result->tiles().part;
  const std::optional<std::string> dump_frames_file =
    absl::GetFlag(FLAGS_dump_frames_file);
  if (dump_frames_file.has_value()) {
    std::ofstream frames_out(dump_frames_file.value());
    if (!frames_out) {
      std::cerr << "cannot open " << dump_frames_file.value() << '\n';
      return EXIT_FAILURE;
    }
    PrintFrames(frames, frames_out);
  }
  const auto bitstream_status =
    fpga::xilinx::BitStream<fpga::xilinx::Architecture::kXC7>::Encode<
      fpga::Frames>(part_data, part, "fpga-source", frames, std::cout);
  if (!bitstream_status.ok()) {
    std::cerr << StatusToErrorMessage("could not generate bistream",
                                      bitstream_status)
              << '\n';
    return EXIT_FAILURE;
  }
  // Write bitstream
  return EXIT_SUCCESS;
}
