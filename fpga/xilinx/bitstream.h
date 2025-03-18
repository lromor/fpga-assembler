#ifndef FPGA_XILINX_BITSTREAM_H
#define FPGA_XILINX_BITSTREAM_H

#include <optional>
#include <ostream>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fpga/database-parsers.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/bitstream-writer.h"
#include "fpga/xilinx/configuration.h"
#include "fpga/xilinx/frames.h"

namespace fpga {
namespace xilinx {
template <Architecture Arch>
class BitStream {
 private:
  using ArchType = ArchitectureType<Arch>;
  using FrameWords = ArchType::FrameWords;
  using FrameAddress = ArchType::FrameAddress;
  using ConfigurationPackage = ArchType::ConfigurationPackage;
  using Part = ArchType::Part;
  using FramesType = Frames<Arch>;
  using ConfigurationType = Configuration<Arch>;
  using BitstreamWriterType = BitstreamWriter<Arch>;

 public:
  template <typename FramesData>
  static absl::Status Encode(const fpga::Part &part,
                             absl::string_view part_name,
                             absl::string_view source_name,
                             const FramesData &frames_data, std::ostream &out) {
    absl::btree_map<FrameAddress, FrameWords> converted_frames;
    for (const auto &address_words_pair : frames_data) {
      converted_frames.emplace(address_words_pair.first,
                               address_words_pair.second);
    }
    constexpr absl::string_view kGeneratorName = "fpga-assembler";
    FramesType frames(converted_frames);
    absl::StatusOr<Part> xilinx_part_status = Part::FromPart(part);
    if (!xilinx_part_status.ok()) {
      return xilinx_part_status.status();
    }
    std::optional<Part> xilinx_part = xilinx_part_status.value();
    frames.AddMissingFrames(xilinx_part);
    // Create data for the type 2 configuration packet with
    // information about all frames
    typename ConfigurationType::PacketData configuration_packet_data(
      ConfigurationType::CreateType2ConfigurationPacketData(frames.GetFrames(),
                                                            xilinx_part));

    // Put together a configuration package
    ConfigurationPackage configuration_package;
    ConfigurationType::CreateConfigurationPackage(
      configuration_package, configuration_packet_data, xilinx_part);

    // Write bitstream
    auto bitstream_writer = BitstreamWriterType(configuration_package);
    if (bitstream_writer.writeBitstream(
          configuration_package, std::string(part_name),
          std::string(source_name), std::string(kGeneratorName), out)) {
      return absl::InternalError("failed generating bitstream");
    }
    return absl::OkStatus();
  }
};
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_BITSTREAM_H
