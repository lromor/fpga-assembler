#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <filesystem>

#include "absl/strings/str_cat.h"
#include "absl/strings/numbers.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"

#include "xstream/database.h"
#include "xstream/fasm-parser.h"
#include "xstream/banks-tiles-registry.h"
#include "xstream/memory-mapped-file.h"

absl::StatusOr<xstream::BanksTilesRegistry> CreateBanksRegistry(
    absl::string_view part_json_path,
    absl::string_view package_pins_path) {
  // Parse part.json.
  const absl::StatusOr<std::unique_ptr<xstream::MemoryBlock>> part_json_result =
      xstream::MemoryMapFile(part_json_path);
  if (!part_json_result.ok()) return part_json_result.status();
  const absl::StatusOr<xstream::Part> part_result =
      xstream::ParsePartJSON(part_json_result.value()->AsStringVew());
  if (!part_result.ok()) {
    return part_result.status();
  }
  const xstream::Part &part = part_result.value();

  // Parse package pins.
  const absl::StatusOr<std::unique_ptr<xstream::MemoryBlock>> package_pins_csv_result =
      xstream::MemoryMapFile(package_pins_path);
  if (!package_pins_csv_result.ok()) return package_pins_csv_result.status();
  const absl::StatusOr<xstream::PackagePins> package_pins_result =
      xstream::ParsePackagePins(package_pins_csv_result.value()->AsStringVew());
  if (!package_pins_result.ok()) {
    return package_pins_result.status();
  }
  const xstream::PackagePins &package_pins = package_pins_result.value();
  return xstream::BanksTilesRegistry::Create(part, package_pins);
}

struct TileSiteInfo {
  std::string tile;
  std::string site;
};

absl::StatusOr<std::optional<TileSiteInfo>> FindPUDCBTileSite(
  const xstream::TileGrid &tilegrid) {
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
          return absl::InternalError("could not parse site last character as int");
        }
        return TileSiteInfo{
          .tile = tile,
          .site = absl::StrFormat("IOB_Y%d", y_coord % 2),
        };
      }
    }
  }
  return std::optional<TileSiteInfo>{};
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

static absl::StatusOr<xstream::PartInfo> ParsePartInfo(
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
  const absl::StatusOr<std::map<std::string, xstream::PartInfo>> parts_infos_result =
    xstream::ParsePartsInfos(
      parts_yaml_content_result.value()->AsStringVew(),
      devices_yaml_content_result.value()->AsStringVew());
  if (!parts_infos_result.ok()) {
    return parts_infos_result.status();
  }
  const std::map<std::string, xstream::PartInfo> &parts_infos = parts_infos_result.value();
  if (!parts_infos.count(part)) {
    return absl::InvalidArgumentError(absl::StrFormat("invalid or unknown part \"%s\"", part));
  }
  return parts_infos.at(part);
}

static absl::StatusOr<xstream::TileGrid> ParseTileGrid(
  const std::filesystem::path &prjxray_db_path, const xstream::PartInfo &part_info) {
  const auto tilegrid_json_content_result =
      xstream::MemoryMapFile(prjxray_db_path / part_info.fabric / "tilegrid.json");
  if (!tilegrid_json_content_result.ok()) {
    return tilegrid_json_content_result.status();
  }
  return xstream::ParseTileGridJSON(
      tilegrid_json_content_result.value()->AsStringVew());
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

struct FasmFeature {
  std::string name;
  int start_bit;
  int width;
  uint64_t bits;
};

static absl::Status AddFasmLine(const uint32_t line, FasmFeature feature) {
  // Get first segment of feature name. That's the tile name.
  // The rest is the feature of that specific tile. For instance:
  //  [tile        ] [feature          ][e, s] [value                             ]
  //  CLBLM_R_X33Y38.SLICEM_X0.ALUT.INIT[31:0]=32'b11111111111111110000000000000000
  std::vector<std::string> tile_feature = absl::StrSplit(feature.name, absl::MaxSplits('.', 1));
  if (tile_feature.size() != 2) {
    return absl::InvalidArgumentError(absl::StrFormat("cannot split feature name %s", feature.name));
  }
  for (int addr = 0; addr < feature.width; ++addr) {
    
  }
  return absl::OkStatus();
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
  const absl::StatusOr<xstream::PartInfo> part_info_result = ParsePartInfo(
      prjxray_db_path, part);
  if (!part_info_result.ok()) {
    std::cerr << StatusToErrorMessage("part mapping parsing", part_info_result.status())
              << std::endl;
    return EXIT_FAILURE;
  }
  const xstream::PartInfo &part_info = part_info_result.value();

  // Parse tilegrid.
  const absl::StatusOr<xstream::TileGrid> tilegrid_result =
      ParseTileGrid(prjxray_db_path, part_info);
  if (!tilegrid_result.ok()) {
    std::cerr << StatusToErrorMessage("tilegrid parsing", part_info_result.status())
              << std::endl;
  }

  // TODO: add required features.
  // TODO: add roi.

  FILE *input_stream = stdin;
  if (args_count == 2) {
    const std::string_view arg(args[1]);
    if (arg != "-") {
      input_stream = std::fopen(args[1], "r");
    }
  }

  size_t buf_size = 8192;
  char *buffer = (char *)malloc(buf_size);
  ssize_t read_count;
  while ((read_count = getline(&buffer, &buf_size, input_stream)) > 0) {
    const std::string_view content(buffer, read_count);
    fasm::Parse(
      content, stderr,
      [](uint32_t line, std::string_view feature_name, int start_bit, int
      width,
         uint64_t bits) -> bool {
        AddFasmLine(line, FasmFeature{std::string(feature_name), start_bit, width, bits});
        return false;
      },
      [](uint32_t, std::string_view, std::string_view name,
         std::string_view value) {});
  }
  free(buffer);
  return EXIT_SUCCESS;
}
