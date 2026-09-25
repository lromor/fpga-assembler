#include "fpga/injected-features.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "fpga/database-parsers.h"
#include "fpga/database.h"

namespace fpga {

namespace {

// A tile and one of its sites.
struct TileSiteInfo {
  std::string tile;
  std::string site;
};

bool FindPUDCBTileSite(const TileGrid &tilegrid, TileSiteInfo &info) {
  for (const auto &p : tilegrid) {
    const std::string &tile = p.first;
    const Tile &tileinfo = p.second;
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

std::vector<std::string> GetIOBSites(const TileGrid &grid,
                                     const std::string &tile_name) {
  std::vector<std::string> out;
  const Tile &tile = grid.at(tile_name);
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
void AddFeature(std::vector<FasmFeature> &features, const std::string &name) {
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
void AddPUDCBFeatures(const TileGrid &tilegrid,
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

void AddStepDownFeatures(const BanksTilesRegistry &banks, const TileGrid &grid,
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
void AddHpBankGlueFeatures(const std::vector<FasmFeature> &features,
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
void AddGfanTieRootFeatures(const std::vector<FasmFeature> &features,
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
void AddBufrClkActiveFeatures(const std::vector<FasmFeature> &features,
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

}  // namespace

void InjectConfigurationFeatures(PartDatabase &db, bool emit_pudc_b_pullup,
                                 std::vector<FasmFeature> &features) {
  const TileGrid &grid = db.tiles().grid;
  TileSiteInfo pudc_b_info;
  const bool pudc_b_is_known = FindPUDCBTileSite(grid, pudc_b_info);
  const std::string pudc_b_tile =
    pudc_b_is_known ? pudc_b_info.tile : std::string();
  const FeatureProbe has_feature = [&db](const std::string &tile,
                                         const std::string &feature) {
    return db.HasFeature(tile, feature);
  };

  // The features the assembler parses out of the input stay first; everything
  // below is appended behind them.
  std::vector<FasmFeature> injected;
  if (emit_pudc_b_pullup) {
    AddPUDCBFeatures(grid, features, injected);
  }
  AddStepDownFeatures(db.tiles().banks, grid, features);
  AddHpBankGlueFeatures(features, pudc_b_tile, has_feature, injected);
  AddGfanTieRootFeatures(features, has_feature, injected);
  AddBufrClkActiveFeatures(features, has_feature, injected);
  features.insert(features.end(), injected.begin(), injected.end());
}

}  // namespace fpga
