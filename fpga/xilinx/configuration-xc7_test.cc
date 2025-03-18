/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "fpga/database-parsers.h"
#include "fpga/memory-mapped-file.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-frame.h"
#include "fpga/xilinx/bitstream-reader.h"
#include "fpga/xilinx/configuration-packet.h"
#include "fpga/xilinx/configuration.h"
#include "fpga/xilinx/frames.h"
#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace {
constexpr fpga::xilinx::Architecture kArch = fpga::xilinx::Architecture::kXC7;
using ArchType = ArchitectureType<kArch>;
using ConfigurationPacket = ArchType::ConfigurationPacket;
using ConfigurationRegister = ArchType::ConfigurationRegister;
using FrameAddress = ArchType::FrameAddress;
using Part = ArchType::Part;
using FrameWords = ArchType::FrameWords;

TEST(XC7ConfigurationTest, ConstructFromPacketsWithSingleFrame) {
  std::vector<FrameAddress> test_part_addresses;
  test_part_addresses.emplace_back(static_cast<FrameAddress>(0x4567));
  test_part_addresses.emplace_back(static_cast<FrameAddress>(0x4568));

  Part const test_part(0x1234, test_part_addresses);

  std::vector<uint32_t> idcode{0x1234};
  std::vector<uint32_t> cmd{0x0001};
  std::vector<uint32_t> frame_address{0x4567};
  std::vector<uint32_t> frame(101, 0xAA);

  std::vector<ConfigurationPacket> packets{
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kIDCODE,
      absl::MakeSpan(idcode),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFAR,
      absl::MakeSpan(frame_address),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kCMD,
      absl::MakeSpan(cmd),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFDRI,
      absl::MakeSpan(frame),
    },
  };

  auto test_config =
    Configuration<Architecture::kXC7>::InitWithPackets(test_part, packets);
  ASSERT_TRUE(test_config);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(test_config->part().idcode(), static_cast<uint32_t>(0x1234));
  EXPECT_EQ(test_config->frames().size(), static_cast<size_t>(1));
  EXPECT_EQ(test_config->frames().at(FrameAddress(0x4567)), frame);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(XC7ConfigurationTest, ConstructFromPacketsWithAutoincrement) {
  std::vector<FrameAddress> test_part_addresses;
  for (int ii = 0x4560; ii < 0x4570; ++ii) {
    test_part_addresses.emplace_back(static_cast<FrameAddress>(ii));
  }
  for (int ii = 0x4580; ii < 0x4590; ++ii) {
    test_part_addresses.emplace_back(static_cast<FrameAddress>(ii));
  }

  Part const test_part(0x1234, test_part_addresses);

  std::vector<uint32_t> idcode{0x1234};
  std::vector<uint32_t> cmd{0x0001};
  std::vector<uint32_t> frame_address{0x456F};
  std::vector<uint32_t> frame(202, 0xAA);
  std::fill_n(frame.begin() + 101, 101, 0xBB);

  std::vector<ConfigurationPacket> packets{
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kIDCODE,
      absl::MakeSpan(idcode),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFAR,
      absl::MakeSpan(frame_address),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kCMD,
      absl::MakeSpan(cmd),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFDRI,
      absl::MakeSpan(frame),
    },
  };

  auto test_config =
    Configuration<Architecture::kXC7>::InitWithPackets(test_part, packets);
  ASSERT_TRUE(test_config);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  absl::Span<uint32_t> const frame_span(frame);
  EXPECT_EQ(test_config->part().idcode(), static_cast<uint32_t>(0x1234));
  EXPECT_EQ(test_config->frames().size(), static_cast<size_t>(2));
  EXPECT_EQ(test_config->frames().at(FrameAddress(0x456F)),
            std::vector<uint32_t>(101, 0xAA));
  EXPECT_EQ(test_config->frames().at(FrameAddress(0x4580)),
            std::vector<uint32_t>(101, 0xBB));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

// Load Part from JSON.
absl::StatusOr<Part> LoadPartJSON(const std::filesystem::path &path) {
  const auto block_status = MemoryMapFile(path);
  if (!block_status.ok()) {
    return block_status.status();
  }
  const auto part_status = ParsePartJSON(block_status.value()->AsStringView());
  if (!part_status.ok()) {
    return part_status.status();
  }
  return Part::FromPart(part_status.value());
}

TEST(XC7ConfigurationTest,
     DebugAndPerFrameCrcBitstreamsProduceEqualConfigurations) {
  const std::filesystem::path kTestDataBase = "fpga/xilinx/testdata";
  auto part = LoadPartJSON(kTestDataBase / "xc7-configuration-test.json");
  ASSERT_TRUE(part.ok()) << part.status();

  absl::StatusOr<std::unique_ptr<MemoryBlock>> debug_bitstream_status =
    MemoryMapFile(kTestDataBase / "xc7-configuration.debug.bit");
  ASSERT_TRUE(debug_bitstream_status.ok()) << debug_bitstream_status.status();
  auto &debug_bitstream = debug_bitstream_status.value();
  CHECK(debug_bitstream);

  auto debug_reader =
    BitstreamReader<kArch>::InitWithBytes(debug_bitstream->AsBytesView());
  CHECK(debug_reader.has_value());
  auto debug_configuration =
    Configuration<kArch>::InitWithPackets(*part, *debug_reader);
  CHECK(debug_configuration.has_value());

  absl::StatusOr<std::unique_ptr<MemoryBlock>> perframecrc_bitstream_status =
    MemoryMapFile(kTestDataBase / "xc7-configuration.perframecrc.bit");
  ASSERT_TRUE(perframecrc_bitstream_status.ok())
    << perframecrc_bitstream_status.status();
  auto &perframecrc_bitstream = perframecrc_bitstream_status.value();
  ASSERT_TRUE(perframecrc_bitstream);

  auto perframecrc_reader =
    BitstreamReader<kArch>::InitWithBytes(perframecrc_bitstream->AsBytesView());
  CHECK(perframecrc_reader.has_value());
  auto perframecrc_configuration =
    Configuration<kArch>::InitWithPackets(*part, *perframecrc_reader);
  CHECK(perframecrc_configuration.has_value());

  for (auto debug_frame : debug_configuration->frames()) {
    auto perframecrc_frame =
      perframecrc_configuration->frames().find(debug_frame.first);
    if (perframecrc_frame == perframecrc_configuration->frames().end()) {
      ADD_FAILURE() << debug_frame.first
                    << ": missing in perframecrc bitstream";
      continue;
    }

    for (int ii = 0; ii < 101; ++ii) {
      EXPECT_EQ(perframecrc_frame->second[ii], debug_frame.second[ii])
        << debug_frame.first << ": word " << ii;
    }
  }
  for (auto perframecrc_frame : perframecrc_configuration->frames()) {
    auto debug_frame =
      debug_configuration->frames().find(perframecrc_frame.first);
    if (debug_frame == debug_configuration->frames().end()) {
      ADD_FAILURE() << perframecrc_frame.first
                    << ": unexpectedly present in "
                       "perframecrc bitstream";
    }
  }
}

TEST(XC7ConfigurationTest, DebugAndNormalBitstreamsProduceEqualConfigurations) {
  const std::filesystem::path kTestDataBase = "fpga/xilinx/testdata";
  auto part = LoadPartJSON(kTestDataBase / "xc7-configuration-test.json");
  ASSERT_TRUE(part.ok()) << part.status();

  absl::StatusOr<std::unique_ptr<MemoryBlock>> debug_bitstream_status =
    MemoryMapFile(kTestDataBase / "xc7-configuration.debug.bit");
  ASSERT_TRUE(debug_bitstream_status.ok()) << debug_bitstream_status.status();
  auto &debug_bitstream = debug_bitstream_status.value();
  CHECK(debug_bitstream);

  auto debug_reader =
    BitstreamReader<kArch>::InitWithBytes(debug_bitstream->AsBytesView());
  CHECK(debug_reader.has_value());
  auto debug_configuration =
    Configuration<kArch>::InitWithPackets(*part, *debug_reader);
  CHECK(debug_configuration.has_value());

  absl::StatusOr<std::unique_ptr<MemoryBlock>> normal_bitstream_status =
    MemoryMapFile(kTestDataBase / "xc7-configuration.bit");
  ASSERT_TRUE(normal_bitstream_status.ok()) << normal_bitstream_status.status();
  auto &normal_bitstream = normal_bitstream_status.value();
  ASSERT_TRUE(normal_bitstream);

  auto normal_reader =
    BitstreamReader<kArch>::InitWithBytes(normal_bitstream->AsBytesView());
  CHECK(normal_reader.has_value());
  auto normal_configuration =
    Configuration<kArch>::InitWithPackets(*part, *normal_reader);
  CHECK(normal_configuration.has_value());
  for (const auto &debug_frame : debug_configuration->frames()) {
    const auto normal_frame =
      normal_configuration->frames().find(debug_frame.first);
    if (normal_frame == normal_configuration->frames().end()) {
      ADD_FAILURE() << debug_frame.first << ": missing in normal bitstream";
      continue;
    }

    for (int ii = 0; ii < 101; ++ii) {
      EXPECT_EQ(normal_frame->second[ii], debug_frame.second[ii])
        << debug_frame.first << ": word " << ii;
    }
  }
  for (const auto &normal_frame : normal_configuration->frames()) {
    auto debug_frame = debug_configuration->frames().find(normal_frame.first);
    if (debug_frame == debug_configuration->frames().end()) {
      ADD_FAILURE() << normal_frame.first
                    << ": unexpectedly present in normal bitstream";
    }
  }
}

template <typename T>
T Fill(typename T::value_type value) {
  T out;
  out.fill(value);
  return out;
}

TEST(XC7ConfigurationTest, CheckForPaddingFrames) {
  std::vector<FrameAddress> test_part_addresses = {
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 0),
    FrameAddress(xc7::BlockType::kCLBIOCLK, true, 0, 0, 0),
    FrameAddress(xc7::BlockType::kCLBIOCLK, true, 1, 0, 0),
    FrameAddress(xc7::BlockType::kBlockRam, false, 0, 0, 0),
    FrameAddress(xc7::BlockType::kBlockRam, false, 1, 0, 0)};

  auto test_part = absl::optional<Part>(Part(0x1234, test_part_addresses));

  Frames<kArch> frames;
  frames.GetFrames().emplace(FrameAddress(test_part_addresses.at(0)),
                             Fill<FrameWords>(0xAA));
  frames.GetFrames().emplace(FrameAddress(test_part_addresses.at(1)),
                             Fill<FrameWords>(0xBB));
  frames.GetFrames().emplace(FrameAddress(test_part_addresses.at(2)),
                             Fill<FrameWords>(0xCC));
  frames.GetFrames().emplace(FrameAddress(test_part_addresses.at(3)),
                             Fill<FrameWords>(0xDD));
  frames.GetFrames().emplace(FrameAddress(test_part_addresses.at(4)),
                             Fill<FrameWords>(0xEE));
  ASSERT_EQ(frames.GetFrames().size(), 5);

  Configuration<Architecture::kXC7>::PacketData packet_data =
    Configuration<Architecture::kXC7>::CreateType2ConfigurationPacketData(
      frames.GetFrames(), test_part);
  // createType2ConfigurationPacketData should detect 4
  // row/half/block_type switches thus add 4*2 padding frames  moreover 2
  // extra padding frames are added at the end of the creation of the data
  // overall this gives us: 5(real frames) + 4*2 + 2 = 15 frames, which is
  // 15 * 101 = 1515 words
  EXPECT_EQ(packet_data.size(), 15 * 101);

  std::vector<uint32_t> idcode{0x1234};
  std::vector<uint32_t> cmd{0x0001};
  std::vector<uint32_t> frame_address{0x0};

  std::vector<ConfigurationPacket> packets{
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kIDCODE,
      absl::MakeSpan(idcode),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFAR,
      absl::MakeSpan(frame_address),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kCMD,
      absl::MakeSpan(cmd),
    },
    {
      static_cast<unsigned int>(0x1),
      ConfigurationPacket::Opcode::kWrite,
      ConfigurationRegister::kFDRI,
      absl::MakeSpan(packet_data),
    },
  };

  auto test_config =
    Configuration<Architecture::kXC7>::InitWithPackets(*test_part, packets);
  CHECK(test_config.has_value());
  ASSERT_EQ(test_config->frames().size(), 5);
  for (const auto &frame : test_config->frames()) {
    EXPECT_EQ(frame.second, frames.GetFrames().at(frame.first));
  }
}
}  // namespace
}  // namespace xilinx
}  // namespace fpga
