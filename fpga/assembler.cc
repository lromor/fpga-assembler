#include <sys/types.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "fpga/database-parsers.h"
#include "fpga/database.h"
#include "fpga/fasm-parser.h"
#include "fpga/injected-features.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/bitstream.h"

// Same name as the reference implementation's flag, and likewise off by
// default.
ABSL_FLAG(bool, emit_pudc_b_pullup, false,
          "Emit an IBUF and PULLUP on the PUDC_B pin if the design leaves it "
          "unused.");

// Writes the assembled frames in the same text format fasm2frames uses: one
// line per frame, the frame address followed by its words in hex.  Comparing
// that output against the reference implementation's .frames file is how the
// two assemblers are diffed.
ABSL_FLAG(std::optional<std::string>, dump_frames_file, std::nullopt,
          "Also write the assembled frames to this file, in the text format\n"
          "fasm2frames uses (\"0x<address> 0x<word>,0x<word>,...\").");

ABSL_FLAG(
  std::optional<std::string>, prjxray_db_path, std::nullopt,
  "Path to root folder containing the prjxray database for the FPGA family.\n"
  "If not present, it must be provided via PRJXRAY_DB_PATH.");

ABSL_FLAG(std::string, part, "", "FPGA part name, e.g. \"xc7a35tcsg324-1\".");

static absl::Status ProcessFasmFeatures(
  const std::vector<fpga::FasmFeature> &features, fpga::PartDatabase &db,
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
    const std::string& tile_name = tile_feature_segments[0];
    const std::string& feature = tile_feature_segments[1];
    const uint64_t bits = tile_feature.bits;
    absl::flat_hash_set<fpga::ConfigBusType> used_config_buses;
    // Select only bit addresses with value bit set to 1.  The parser reports
    // at most 64 bits per callback, but guard the shift anyway: a wider width
    // would wrap the shift count and report a wrong address as set.
    for (unsigned addr = 0; std::cmp_less(addr , tile_feature.width); ++addr) {
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

static absl::Status ParseFasmFile(FILE *input_stream,
                                  std::vector<fpga::FasmFeature> &features) {
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
        features.push_back(fpga::FasmFeature{line, std::string(feature_name),
                                             start_bit, width, bits});
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
  std::vector<fpga::FasmFeature> features;
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
  InjectConfigurationFeatures(db, absl::GetFlag(FLAGS_emit_pudc_b_pullup),
                              features);
  return ProcessFasmFeatures(features, db, frames);
}

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
