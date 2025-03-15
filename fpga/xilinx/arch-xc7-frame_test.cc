/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include "fpga/xilinx/arch-xc7-frame.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
namespace {
TEST(IcapCrcTest, SimpleTests) {
  // CRC for Zero Data
  EXPECT_EQ(internal::ICAPCRC(0, 0, 0), 0x0L);
  // Polynomial (single bit operation)
  EXPECT_EQ(internal::ICAPCRC(1 << 4, 0, 0), 0x82F63B78);
  // All Reg/Data bits
  EXPECT_EQ(internal::ICAPCRC(~0, ~0, 0), 0xBF86D4DF);
  // All CRC bits
  EXPECT_EQ(internal::ICAPCRC(0, 0, ~0), 0xC631E365);
}

TEST(IcapEccTest, SimpleTests) {
  // ECC for Zero Data
  EXPECT_EQ(internal::ICAPECC(0, 0, 0), (uint32_t)0x0);
  // 0x1320 - 0x13FF (avoid lower)
  EXPECT_EQ(internal::ICAPECC(0, 1, 0), (uint32_t)0x1320);
  // 0x1420 - 0x17FF (avoid 0x400)
  EXPECT_EQ(internal::ICAPECC(0x7, 1, 0), (uint32_t)0x1420);
  // 0x1820 - 0x1FFF (avoid 0x800)
  EXPECT_EQ(internal::ICAPECC(0x26, 1, 0), (uint32_t)0x1820);
  // Masked ECC Value
  EXPECT_EQ(internal::ICAPECC(0x32, ~0, 0), (uint32_t)0x000019AC);
  // Final ECC Parity
  EXPECT_EQ(internal::ICAPECC(0x64, 0, 1), (uint32_t)0x00001001);
}
}  // namespace
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
