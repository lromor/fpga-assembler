/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include "fpga/xilinx/arch-xc7-configuration-packet.h"

#include <cstdint>
#include <vector>

#include "absl/types/span.h"
#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/bit-ops.h"
#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
constexpr uint32_t kType1NOP = bit_field_set<uint32_t>(0, 31, 29, 0x1);

constexpr uint32_t MakeType1(const int opcode, const int address,
                             const int word_count) {
  return bit_field_set<uint32_t>(
    bit_field_set<uint32_t>(
      bit_field_set<uint32_t>(bit_field_set<uint32_t>(0x0, 31, 29, 0x1), 28, 27,
                              opcode),
      26, 13, address),
    10, 0, word_count);
}

constexpr uint32_t MakeType2(const int opcode, const int word_count) {
  return bit_field_set<uint32_t>(
    bit_field_set<uint32_t>(bit_field_set<uint32_t>(0x0, 31, 29, 0x2), 28, 27,
                            opcode),
    26, 0, word_count);
}

TEST(ConfigPacket, InitWithZeroBytes) {
  auto packet = ConfigurationPacket::InitWithWords({});

  EXPECT_EQ(packet.first, absl::Span<uint32_t>());
  EXPECT_FALSE(packet.second);
}

TEST(ConfigPacket, InitWithType1Nop) {
  std::vector<uint32_t> words{kType1NOP};
  const absl::Span<uint32_t> word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  EXPECT_EQ(packet.first, absl::Span<uint32_t>());
  ASSERT_TRUE(packet.second);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(packet.second->opcode(), ConfigurationPacket::Opcode::kNOP);
  EXPECT_EQ(packet.second->address(), ConfigurationRegister::kCRC);
  EXPECT_EQ(packet.second->data(), absl::Span<uint32_t>());
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(ConfigPacket, InitWithType1Read) {
  std::vector<uint32_t> words{MakeType1(0x1, 0x2, 2), 0xAA, 0xBB};
  const absl::Span<uint32_t> word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  EXPECT_EQ(packet.first, absl::Span<uint32_t>());
  ASSERT_TRUE(packet.second);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(packet.second->opcode(), ConfigurationPacket::Opcode::kRead);
  EXPECT_EQ(packet.second->address(), ConfigurationRegister::kFDRI);
  EXPECT_EQ(packet.second->data(), word_span.subspan(1));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(ConfigPacket, InitWithType1Write) {
  std::vector<uint32_t> words{MakeType1(0x2, 0x3, 2), 0xAA, 0xBB};
  const absl::Span<uint32_t> word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  EXPECT_EQ(packet.first, absl::Span<uint32_t>());
  ASSERT_TRUE(packet.second);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(packet.second->opcode(), ConfigurationPacket::Opcode::kWrite);
  EXPECT_EQ(packet.second->address(), ConfigurationRegister::kFDRO);
  EXPECT_EQ(packet.second->data(), word_span.subspan(1));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(ConfigPacket, InitWithType2WithoutPreviousPacketFails) {
  std::vector<uint32_t> words{MakeType2(0x01, 12)};
  const absl::Span<uint32_t> word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  EXPECT_EQ(packet.first, words);
  EXPECT_FALSE(packet.second);
}

TEST(ConfigPacket, InitWithType2WithPreviousPacket) {
  const ConfigurationPacket previous_packet(
    static_cast<unsigned int>(0x1), ConfigurationPacket::Opcode::kRead,
    ConfigurationRegister::kMFWR, absl::Span<uint32_t>());
  std::vector<uint32_t> words{
    MakeType2(0x01, 12), 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const absl::Span<uint32_t> word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span, &previous_packet);
  EXPECT_EQ(packet.first, absl::Span<uint32_t>());
  ASSERT_TRUE(packet.second);
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(packet.second->opcode(), ConfigurationPacket::Opcode::kRead);
  EXPECT_EQ(packet.second->address(), ConfigurationRegister::kMFWR);
  EXPECT_EQ(packet.second->data(), word_span.subspan(1));
  // NOLINTEND(bugprone-unchecked-optional-access)
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
