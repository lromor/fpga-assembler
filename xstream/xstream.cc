#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <unordered_set>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/numbers.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/status/status.h"

#include "xstream/database-parsers.h"
#include "xstream/database.h"
#include "xstream/fasm-parser.h"

struct TileSiteInfo {
  std::string tile;
  std::string site;
};

bool FindPUDCBTileSite(
    const xstream::TileGrid &tilegrid, TileSiteInfo &info) {
  for (const auto &p : tilegrid) {
    const std::string &tile = p.first;
    const xstream::Tile &tileinfo = p.second;
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

std::vector<std::string> GetIOBSites(const xstream::TileGrid &grid, const std::string &tile_name) {
  std::vector<std::string> out;
  const xstream::Tile &tile = grid.at(tile_name);
  uint32_t site_y;
  for (const auto &site_pair : tile.sites) {
    const absl::string_view site_name = site_pair.first;
    const std::string site_y_value = std::to_string(site_name[site_name.size() - 1]);
    CHECK(absl::SimpleAtoi(site_y_value, &site_y));
    out.push_back(absl::StrFormat("IOB_Y%d", site_y %2));
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
  xstream::Bits bits;
};

static absl::Status ProcessFasmFeatures(
    const std::vector<FasmFeature> &features, xstream::PartDatabase& db, xstream::Frames &frames) {
  for (const auto &tile_feature : features) {
    // Get first segment of feature name. That's the tile name.
    // The rest is the feature of that specific tile. For instance:
    //  [tile name   ] [feature          ][e, s] [value                             ]
    //  CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[31:0]=32'b11111111111111110000000000000000
    std::vector<std::string> tile_feature_segments = absl::StrSplit(tile_feature.name, absl::MaxSplits('.', 1));
    if (tile_feature_segments.size() != 2) {
      return absl::InvalidArgumentError(absl::StrFormat("cannot split feature name %s", tile_feature.name));
    }
    const std::string tile_name = tile_feature_segments[0];
    const std::string feature = tile_feature_segments[1];
    const uint64_t bits = tile_feature.bits;
    std::unordered_set<xstream::ConfigBusType> used_config_buses;
    // Select only bit addresses with value bit set to 1.
    for (int addr = 0; addr < tile_feature.width; ++addr) {
      const unsigned bit_addr = (addr + tile_feature.start_bit);
      const bool value = bits & (1 << bit_addr);
      if (value) {
        const int feature_addr = addr + tile_feature.start_bit;
        db.ConfigBits(
            tile_name, feature, feature_addr,
            [&frames, &used_config_buses](
                xstream::ConfigBusType bus, uint32_t address,
                const xstream::PartDatabase::FrameBit &bit,
                bool value) {
              // Update the list of tile segbits buses used.
              // So we can use it later on to mark all the frames that have been used.
              used_config_buses.insert(bus);

              // Insert the frames at address and enable the right bit.
              if (!frames.contains(address)) {
                frames.insert({address, {}});
              }
              if (value) {
                std::array<xstream::word_t, xstream::kFrameWordCount> &frame = frames[address];
                frame[bit.word] |= (1 << bit.index);
              }
        });
      }
    }
    if (used_config_buses.empty()) {
      continue;
    }
    // Get tilegrid info.
    const xstream::Tile &tile_info = db.tiles().grid.at(tile_name);
    CHECK(tile_info.bits.has_value());
    const auto &config_bits = tile_info.bits.value();
    for (const auto &bus : used_config_buses) {
      const xstream::BitsBlock &info = config_bits.at(bus);
      for (unsigned i = 0; i < info.frames; ++i) {
        frames.insert({info.base_address + i, {}});
      }
    }
  }
  return absl::OkStatus();
}

// Template that for each line should substitute a tile type and a site.
constexpr absl::string_view kPUDCBPullUpFASMLinesTemplate[] = {
  "%s.%s.LVCMOS12_LVCMOS15_LVCMOS18_LVCMOS25_LVCMOS33_LVDS_25_LVTTL_SSTL135_SSTL15_TMDS_33.IN_ONLY",
  "%s.%s.LVCMOS25_LVCMOS33_LVTTL.IN",
  "%s.%s.PULLTYPE.PULLUP",
};

bool AddPUDCBFeatures(const xstream::TileGrid &tilegrid, std::vector<FasmFeature> &features) {
  TileSiteInfo info;
  if (!FindPUDCBTileSite(tilegrid, info)) {
    return false;
  }
  FasmFeature feature = {
    .line = -1,
    .name = "",
    .start_bit = 0,
    .width = 1,
    .bits = 1,
  };
  // Unroll loop.
  feature.name = absl::StrFormat(kPUDCBPullUpFASMLinesTemplate[0], info.tile, info.site);
  features.push_back(feature);
  feature.name = absl::StrFormat(kPUDCBPullUpFASMLinesTemplate[1], info.tile, info.site);
  features.push_back(feature);
  feature.name = absl::StrFormat(kPUDCBPullUpFASMLinesTemplate[2], info.tile, info.site);
  features.push_back(feature);
  return true;
}

static void AddStepDownFeatures(
    const xstream::BanksTilesRegistry &banks,
    const xstream::TileGrid &grid, std::vector<FasmFeature> &features) {
  // Stores a set of strings <tile-type>.<site>
  std::unordered_set<std::string> used_iob_sites;
  std::map<uint32_t, std::unordered_set<std::string>> stepdown_banks_tags;
  for (const auto &feature : features) {
    if (feature.bits == 0) {
      continue;
    }
    std::vector<std::string> tile_feature_segments =
        absl::StrSplit(feature.name, absl::MaxSplits('.', 3));
    if (tile_feature_segments.size() < 3) {
      continue;
    }
    const absl::string_view tile = tile_feature_segments[0];
    const absl::string_view site = tile_feature_segments[1];
    const absl::string_view tag = tile_feature_segments[2];
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
    const std::unordered_set<std::string> &tags =
        bank_tags_pair.second;
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

      if (absl::StrContains(tile, "HCLK_IOI3")) {
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

static absl::Status AssembleFrames(FILE *input_stream, xstream::PartDatabase& db, xstream::Frames &frames) {
  // For now store everything in here.
  std::vector<FasmFeature> features;
  // TODO: add required features.
  // TODO: add roi.

  // if FindPUDCBTileSite(db.tiles().grid);
  AddPUDCBFeatures(db.tiles().grid, features);

  // Parse fasm.
  size_t buf_size = 8192;
  char *buffer = (char *)malloc(buf_size);
  absl::Cleanup buffer_freer = [buffer] { free(buffer); };
  ssize_t read_count;
  while ((read_count = getline(&buffer, &buf_size, input_stream)) > 0) {
    const std::string_view content(buffer, read_count);
    const fasm::ParseResult result = fasm::Parse(
      content, stderr,
      [&features](uint32_t line, std::string_view feature_name, int start_bit, int
      width,
         uint64_t bits) -> bool {
        features.push_back(FasmFeature{line, std::string(feature_name), start_bit, width, bits});
        return true;
      },
      [](uint32_t, std::string_view, std::string_view name,
         std::string_view value) {});

    if (result == fasm::ParseResult::kUserAbort ||
        result == fasm::ParseResult::kError) {
      return absl::InternalError("internal error");
    }
  }
  AddStepDownFeatures(db.tiles().banks, db.tiles().grid, features);
  return ProcessFasmFeatures(features, db, frames);
}

ABSL_FLAG(std::optional<std::string>, prjxray_db_path, std::nullopt,
          R"(Path to root folder containing the prjxray database for the FPGA family.
If not present, it must be provided via PRJXRAY_DB_PATH.)");

ABSL_FLAG(
    std::string, part, "",
    R"(FPGA part name, e.g. "xc7a35tcsg324-1".)");

static inline std::string Usage(absl::string_view name) {
  return absl::StrCat("usage: ", name,
                      " [options] < input.fasm > output.frm\n"
                      R"(
xstream parses a sequence of fasm lines and assembles them
into a set of frames to be mapped into bitstream.
Output is written to stdout.
)");
}

static std::string StatusToErrorMessage(absl::string_view message, const absl::Status &status) {
  return absl::StrFormat("%s: %s", message, status.message());
}

static absl::StatusOr<std::string> GetOptFlagOrFromEnv(
    const absl::Flag<std::optional<std::string>> &flag, absl::string_view env_var) {
  const std::optional<std::string> flag_value = absl::GetFlag(flag);
  if (!flag_value.has_value()) {
    const char *value = getenv(std::string(env_var).c_str());
    if (value == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrFormat(
              "flag \"%s\" not provided either via commandline or environment variable (%s)",
              flag.Name(), env_var));
    }
    return std::string(value);
  }
  return flag_value.value();
}

static void GetPrettyFrameLine(const uint32_t address,
                               const std::array<xstream::word_t, xstream::kFrameWordCount> &bits,
                               std::ostream &out) {
  out << absl::StrFormat("Frame @ 0x%08X\n", address);
  for (size_t i = 0; i < bits.size(); ++i) {
    const uint32_t &word = bits[i];
    if (word) {
      out << absl::StrFormat("\t%d: 0x%08X\n", i, word);
    }
  }
  out << "\n";
}

static void GetFrameLine(const uint32_t address,
                         const std::array<xstream::word_t, xstream::kFrameWordCount> &bits,
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

static void PrintFrames(const xstream::Frames &frames, std::ostream &out, bool pretty) {
  for (const auto &frame : frames) {
    const uint32_t &address = frame.first;
    const std::array<xstream::word_t, xstream::kFrameWordCount> &bits = frame.second;
    if (pretty) {
      GetPrettyFrameLine(address, bits, out);
    } else {
      GetFrameLine(address, bits, out);
    }
  }
}

int main(int argc, char *argv[]) {
  const std::string usage = Usage(argv[0]);
  absl::SetProgramUsageMessage(usage);
  const std::vector<char *> args = absl::ParseCommandLine(argc, argv);
  const auto args_count = args.size();
  if (args_count > 2) {
    std::cerr << absl::ProgramUsageMessage() << std::endl;
  }
  const absl::StatusOr<std::string> prjxray_db_path_result = GetOptFlagOrFromEnv(
      FLAGS_prjxray_db_path, "PRJXRAY_DB_PATH");
  if (!prjxray_db_path_result.ok()) {
    std::cerr << StatusToErrorMessage("get prjxray db path", prjxray_db_path_result.status())
              << std::endl;
    std::cerr << absl::ProgramUsageMessage() << std::endl;
    return 1;
  }
  const std::filesystem::path &prjxray_db_path = prjxray_db_path_result.value();
  if (prjxray_db_path.empty() || !std::filesystem::exists(prjxray_db_path)) {
    std::cerr << absl::StrFormat("invalid prjxray-db path: \"%s\"", prjxray_db_path) << std::endl;
    return EXIT_FAILURE;
  }

  const std::string part = absl::GetFlag(FLAGS_part);
  if (part.empty()) {
    std::cerr << "no part provided" << std::endl;
    std::cerr << absl::ProgramUsageMessage() << std::endl;
    return EXIT_FAILURE;
  }
  auto part_database_result = xstream::PartDatabase::Parse(prjxray_db_path.string(), part);
  if (!part_database_result.ok()) {
    std::cerr << StatusToErrorMessage("part mapping parsing", part_database_result.status())
              << std::endl;
    return EXIT_FAILURE;
  }
  FILE *input_stream = stdin;
  absl::Cleanup file_closer = [input_stream] {
    if (input_stream != stdin) {
      std::fclose(input_stream);
    }
  };
  if (args_count == 2) {
    const std::string_view arg(args[1]);
    if (arg != "-") {
      input_stream = std::fopen(args[1], "r");
    }
  }
  xstream::Frames frames;
  const auto assembler_result = AssembleFrames(
      input_stream, part_database_result.value(), frames);
  if (!assembler_result.ok()) {
    std::cerr << StatusToErrorMessage("could not assemble frames", assembler_result)
              << std::endl;
    return EXIT_FAILURE;
  }

  PrintFrames(frames, std::cout, false);
  return EXIT_SUCCESS;
}
