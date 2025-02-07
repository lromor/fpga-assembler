#include <cstdio>
#include <iostream>
#include <filesystem>

#include "absl/strings/str_cat.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"

#include "prjxstream/fasm-parser.h"
#include "prjxstream/banks-tiles-registry.h"
#include "prjxstream/memory-mapped-file.h"

absl::StatusOr<prjxstream::BanksTilesRegistry> CreateBanksRegistry(
    absl::string_view part_json_path,
    absl::string_view package_pins_path) {
  // Parse part.json.
  const absl::StatusOr<std::unique_ptr<prjxstream::MemoryBlock>> part_json_result =
      prjxstream::MemoryMapFile(part_json_path);
  if (!part_json_result.ok()) return part_json_result.status();
  const absl::StatusOr<prjxstream::Part> part_result =
      prjxstream::ParsePartJSON(part_json_result.value()->AsStringVew());
  if (!part_result.ok()) {
    return part_result.status();
  }
  const prjxstream::Part &part = part_result.value();

  // Parse package pins.
  const absl::StatusOr<std::unique_ptr<prjxstream::MemoryBlock>> package_pins_csv_result =
      prjxstream::MemoryMapFile(package_pins_path);
  if (!package_pins_csv_result.ok()) return package_pins_csv_result.status();
  const absl::StatusOr<prjxstream::PackagePins> package_pins_result =
      prjxstream::ParsePackagePins(package_pins_csv_result.value()->AsStringVew());
  if (!package_pins_result.ok()) {
    return package_pins_result.status();
  }
  const prjxstream::PackagePins &package_pins = package_pins_result.value();
  return prjxstream::BanksTilesRegistry::Create(part, package_pins);
}

struct PUDCBTileSite {
  
};

ABSL_FLAG(std::optional<std::string>, prjxray_db_path, std::nullopt,
          R"(Path to root folder containing the prjxray database for the FPGA family.
If not present, it must be provided via PRJXRAY_DB_PATH.)");

ABSL_FLAG(
    std::optional<std::string>, part, std::nullopt,
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

static std::string GetOptFlagOrFromEnv(
    const std::optional<std::string> &flag, absl::string_view env_var) {
  if (!flag.has_value()) {
    return std::getenv(std::string(env_var).c_str());
  }
  return flag.value();
}

int main(int argc, char *argv[]) {
  const std::string usage = Usage(argv[0]);
  absl::SetProgramUsageMessage(usage);
  const std::vector<char *> args = absl::ParseCommandLine(argc, argv);
  const auto args_count = args.size();
  if (args_count > 2) {
    std::cerr << absl::ProgramUsageMessage() << std::endl;
  }
  const std::string prjxray_db_path = GetOptFlagOrFromEnv(
      absl::GetFlag(FLAGS_prjxray_db_path), "PRJXRAY_DB_PATH");
  if (prjxray_db_path.empty() || !std::filesystem::exists(prjxray_db_path)) {
    std::cerr << absl::StrFormat("invalid prjxray-db path: \"%s\"", prjxray_db_path) << std::endl;
  }

#if 0
  FILE *input_stream = stdin;
  if (args_count == 1) {
    const std::string_view arg(args[0]);
    if (arg != "-") {
      input_stream = std::fopen(args[0], "r");
    }
  }

  size_t buf_size = 8192;
  char *buffer = (char *)malloc(buf_size);
  while (getline(&buffer, &buf_size, input_stream)) {
    const std::string_view content(buffer, buf_size);
    fasm::Parse(
      content, stderr,
      [](uint32_t line, std::string_view feature_name, int start_bit, int
      width,
         uint64_t bits) -> bool { return true; },
      [](uint32_t, std::string_view, std::string_view name,
         std::string_view value) {});
  }
#endif
  return 0;
}
