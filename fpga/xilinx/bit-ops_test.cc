/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include "fpga/xilinx/bit-ops.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace {
TEST(BitMaskTest, Bit0) {
  const uint32_t expected = bit_mask<uint32_t>(0);
  EXPECT_EQ(static_cast<uint32_t>(0x1), expected);
}

TEST(BitMaskTest, Bit3) {
  const uint32_t expected = bit_mask<uint32_t>(3);
  EXPECT_EQ(static_cast<uint32_t>(0x8), expected);
}

TEST(BitMaskRange, SingleBit) {
  const uint32_t expected = bit_mask_range<uint32_t>(23, 23);
  EXPECT_EQ(static_cast<uint32_t>(0x800000), expected);
}

TEST(BitMaskRange, DownToZero) {
  const uint32_t expected = bit_mask_range<uint32_t>(7, 0);
  EXPECT_EQ(static_cast<uint32_t>(0xFF), expected);
}

TEST(BitMaskRange, MiddleBits) {
  const uint32_t expected = bit_mask_range<uint32_t>(18, 8);
  EXPECT_EQ(static_cast<uint32_t>(0x7FF00), expected);
}

TEST(BitFieldGetTest, OneSelectedBit) {
  const uint32_t expected = bit_field_get(0xFFFFFFFF, 23, 23);
  EXPECT_EQ(static_cast<uint32_t>(1), expected);
}

TEST(BitFieldGetTest, SelectDownToZero) {
  const uint32_t expected = bit_field_get(0xFFCCBBAA, 7, 0);
  EXPECT_EQ(static_cast<uint32_t>(0xAA), expected);
}

TEST(BitFieldGetTest, SelectMidway) {
  const uint32_t expected = bit_field_get(0xFFCCBBAA, 18, 8);
  EXPECT_EQ(static_cast<uint32_t>(0x4BB), expected);
}

TEST(BitFieldSetTest, WriteOneBit) {
  const uint32_t actual = bit_field_set(static_cast<uint32_t>(0x0), 23, 23,
                                        static_cast<uint32_t>(0x1));
  EXPECT_EQ(actual, static_cast<uint32_t>(0x800000));
}

TEST(BitFieldSetTest, WriteOneBitWithOutOfRangeValue) {
  const uint32_t actual = bit_field_set(static_cast<uint32_t>(0x0), 23, 23,
                                        static_cast<uint32_t>(0x3));
  EXPECT_EQ(actual, static_cast<uint32_t>(0x800000));
}

TEST(BitFieldSetTest, WriteMultipleBits) {
  const uint32_t actual = bit_field_set(static_cast<uint32_t>(0x0), 18, 8,
                                        static_cast<uint32_t>(0x123));
  EXPECT_EQ(actual, static_cast<uint32_t>(0x12300));
}

TEST(BitFieldSetTest, WriteMultipleBitsWithOutOfRangeValue) {
  const uint32_t actual = bit_field_set(static_cast<uint32_t>(0x0), 18, 8,
                                        static_cast<uint32_t>(0x1234));
  EXPECT_EQ(actual, static_cast<uint32_t>(0x23400));
}
}  // namespace
}  // namespace xilinx
}  // namespace fpga
