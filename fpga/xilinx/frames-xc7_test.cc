/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include <vector>

#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-frame.h"
#include "fpga/xilinx/frames.h"
#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
namespace {

template <typename T>
T Fill(typename T::value_type value) {
  T out;
  out.fill(value);
  return out;
}

TEST(XC7FramesTest, FillInMissingFrames) {
  constexpr fpga::xilinx::Architecture kArch = fpga::xilinx::Architecture::kXC7;
  using ArchType = ArchitectureType<kArch>;
  using FrameAddress = ArchType::FrameAddress;
  using Part = ArchType::Part;
  using FrameWords = ArchType::FrameWords;

  std::vector<FrameAddress> test_part_addresses = {
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 0),
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 1),
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 2),
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 3),
    FrameAddress(xc7::BlockType::kCLBIOCLK, false, 0, 0, 4)};

  Part test_part(0x1234, test_part_addresses);
  Frames<kArch> frames;
  frames.GetFrames().emplace(FrameAddress(2), Fill<FrameWords>(0xCC));
  frames.GetFrames().emplace(xc7::FrameAddress(3), Fill<FrameWords>(0xDD));
  frames.GetFrames().emplace(xc7::FrameAddress(4), Fill<FrameWords>(0xEE));

  ASSERT_EQ(frames.GetFrames().size(), 3);
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[2]),
            Fill<FrameWords>(0xCC));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[3]),
            Fill<FrameWords>(0xDD));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[4]),
            Fill<FrameWords>(0xEE));

  frames.AddMissingFrames(test_part);

  ASSERT_EQ(frames.GetFrames().size(), 5);
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[0]), Fill<FrameWords>(0));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[1]), Fill<FrameWords>(0));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[2]),
            Fill<FrameWords>(0xCC));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[3]),
            Fill<FrameWords>(0xDD));
  EXPECT_EQ(frames.GetFrames().at(test_part_addresses[4]),
            Fill<FrameWords>(0xEE));
}
}  // namespace
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
