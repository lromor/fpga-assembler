/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#include "fpga/xilinx/bitstream-writer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-configuration-packet.h"
#include "fpga/xilinx/bit-ops.h"
#include "fpga/xilinx/configuration-packet.h"
#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace {
constexpr fpga::xilinx::Architecture kArch = fpga::xilinx::Architecture::kXC7;
using ArchType = ArchitectureType<kArch>;
using ConfigurationPacket = ArchType::ConfigurationPacket;
using ConfigurationRegister = ArchType::ConfigurationRegister;

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

#if 0
void DumpPackets(BitstreamWriter<kArch> writer, bool nl = true) {
  int i = 0;
  for (unsigned int itr : writer) {
    if (nl) {
      printf("% 3d: 0x0%08X\n", i, itr);
    } else {
      printf("0x0%08X, ", itr);
    }
    fflush(stdout);
    ++i;
  }
  if (!nl) {
    printf("\n");
  }
}
#endif

// Special all 0's
void AddType0(std::vector<std::unique_ptr<ConfigurationPacket>> &packets) {
  static std::vector<uint32_t> words{};
  absl::Span<uint32_t> const word_span(words);
  // CRC is config value 0
  packets.emplace_back(
    new ConfigurationPacket(0, ConfigurationPacket::Opcode::kNOP,
                            xc7::ConfigurationRegister::kCRC, word_span));
}

void AddType1(std::vector<std::unique_ptr<ConfigurationPacket>> &packets) {
  static std::vector<uint32_t> words{MakeType1(0x2, 0x3, 2), 0xAA, 0xBB};
  absl::Span<uint32_t> const word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  ASSERT_TRUE(packet.second.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  packets.emplace_back(new ConfigurationPacket(*(packet.second)));
}

// Empty
void AddType1E(std::vector<std::unique_ptr<ConfigurationPacket>> &packets) {
  static std::vector<uint32_t> words{MakeType1(0x2, 0x3, 0)};
  absl::Span<uint32_t> const word_span(words);
  auto packet = ConfigurationPacket::InitWithWords(word_span);
  ASSERT_TRUE(packet.second.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  packets.emplace_back(new ConfigurationPacket(*(packet.second)));
}

void AddType2(std::vector<std::unique_ptr<ConfigurationPacket>> &packets) {
  // Type 1 packet with address
  // Without this InitWithWords will fail on type 2
  ConfigurationPacket *packet1;
  {
    static std::vector<uint32_t> words{MakeType1(0x2, 0x3, 0)};
    absl::Span<uint32_t> const word_span(words);
    auto packet1_pair = ConfigurationPacket::InitWithWords(word_span);
    ASSERT_TRUE(packet1_pair.second.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    packets.emplace_back(new ConfigurationPacket(*(packet1_pair.second)));
    packet1 = packets[0].get();
  }
  // Type 2 packet with data
  {
    static std::vector<uint32_t> words{
      MakeType2(0x01, 12), 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    absl::Span<uint32_t> const word_span(words);
    auto packet = ConfigurationPacket::InitWithWords(word_span, packet1);
    ASSERT_TRUE(packet.second.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    packets.emplace_back(new ConfigurationPacket(*(packet.second)));
  }
}

// A stream buffer which collects the output but cannot seek, like a pipe.
// The default seekoff() and seekpos() fail, so tellp() returns -1 and seekp()
// sets the failbit. This is also what std::cout does on macOS.
class NonSeekableBuffer : public std::streambuf {
 public:
  const std::string &data() const { return data_; }

 protected:
  int_type overflow(int_type ch) override {
    if (!traits_type::eq_int_type(ch, traits_type::eof())) {
      data_.push_back(traits_type::to_char_type(ch));
    }
    return traits_type::not_eof(ch);
  }

  std::streamsize xsputn(const char *s, std::streamsize n) override {
    data_.append(s, static_cast<size_t>(n));
    return n;
  }

 private:
  std::string data_;
};

uint32_t ReadBigEndian(const std::string &data, size_t pos, size_t num_bytes) {
  uint32_t value = 0;
  for (size_t i = 0; i < num_bytes; i++) {
    value = (value << 8) | static_cast<uint8_t>(data.at(pos + i));
  }
  return value;
}

// Returns the position after the header of a .bit file and sets
// "declared_length" to the data length which the header declares.
// The header is a sequence of fields: first one with a 2-byte length, then the
// tagged fields 'a' to 'd' with a 2-byte length each, then 'e' with a 4-byte
// data length. See
// http://www.fpga-faq.com/FAQ_Pages/0026_Tell_me_about_bit_files.htm
size_t ParseBitHeader(const std::string &data, uint32_t &declared_length) {
  size_t pos = 0;
  pos += 2 + ReadBigEndian(data, pos, 2);  // Sync header.
  pos += 2;                                // Always 0x0001.
  for (const char tag : {'a', 'b', 'c', 'd'}) {
    EXPECT_EQ(data.at(pos), tag);
    pos += 1 + 2 + ReadBigEndian(data, pos + 1, 2);
  }
  EXPECT_EQ(data.at(pos), 'e');
  declared_length = ReadBigEndian(data, pos + 1, 4);
  return pos + 1 + 4;
}

// Empty packets should produce just the header
TEST(BitstreamWriterTest, WriteHeader) {
  std::vector<std::unique_ptr<ConfigurationPacket>> const packets;

  BitstreamWriter<kArch> writer(packets);
  std::vector<uint32_t> const words(writer.begin(), writer.end());

  // Per UG470 pg 80: Bus Width Auto Detection
  std::vector<uint32_t> const ref_header{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x000000BB, 0x11220044,
    0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566};
  EXPECT_EQ(words, ref_header);
}

TEST(BitstreamWriterTest, WriteType0) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType0(packets);
  BitstreamWriter<kArch> writer(packets);
  std::vector<uint32_t> const words(writer.begin(), writer.end());
  std::vector<uint32_t> const ref{
    // Bus width + sync
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0000000BB, 0x011220044,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0AA995566,
    // Type 0
    0x00000000};
  EXPECT_EQ(words, ref);
}
TEST(BitstreamWriterTest, WriteType1) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType1(packets);
  BitstreamWriter<kArch> writer(packets);
  std::vector<uint32_t> const words(writer.begin(), writer.end());
  std::vector<uint32_t> const ref{
    // Bus width + sync
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0000000BB, 0x011220044,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0AA995566,
    // Type 1
    0x030006002, 0x0000000AA, 0x0000000BB};
  EXPECT_EQ(words, ref);
}

TEST(BitstreamWriterTest, WriteType2) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType2(packets);
  BitstreamWriter<kArch> writer(packets);
  std::vector<uint32_t> const words(writer.begin(), writer.end());
  std::vector<uint32_t> const ref{
    // Bus width + sync
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0000000BB, 0x011220044,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0AA995566,
    // Type 1 + type 2 header
    0x030006000, 0x04800000C, 0x000000001, 0x000000002, 0x000000003,
    0x000000004, 0x000000005, 0x000000006, 0x000000007, 0x000000008,
    0x000000009, 0x00000000A, 0x00000000B, 0x00000000C};
  EXPECT_EQ(words, ref);
}

TEST(BitstreamWriterTest, WriteMulti) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType1(packets);
  AddType1E(packets);
  AddType2(packets);
  AddType1E(packets);
  BitstreamWriter<kArch> writer(packets);
  std::vector<uint32_t> const words(writer.begin(), writer.end());
  std::vector<uint32_t> const ref{
    // Bus width + sync
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0FFFFFFFF, 0x0000000BB, 0x011220044,
    0x0FFFFFFFF, 0x0FFFFFFFF, 0x0AA995566,
    // Type1
    0x030006002, 0x0000000AA, 0x0000000BB,
    // Type1
    0x030006000,
    // Type 1 + type 2 header
    0x030006000, 0x04800000C, 0x000000001, 0x000000002, 0x000000003,
    0x000000004, 0x000000005, 0x000000006, 0x000000007, 0x000000008,
    0x000000009, 0x00000000A, 0x00000000B, 0x00000000C,
    // Type 1
    0x030006000};
  EXPECT_EQ(words, ref);
}
// The header must declare the length of the data which follows it, also when
// the output stream cannot seek back to patch the length in afterwards.
TEST(BitstreamWriterTest, HeaderLengthOnNonSeekableStream) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType1(packets);
  AddType1E(packets);
  AddType2(packets);
  BitstreamWriter<kArch> writer(packets);
  const std::vector<uint32_t> words(writer.begin(), writer.end());

  NonSeekableBuffer buffer;
  std::ostream out(&buffer);
  ASSERT_EQ(out.tellp(), std::ostream::pos_type(-1));
  EXPECT_EQ(
    writer.writeBitstream(packets, "xc7a35tcsg324-1", "test", "test", out), 0);
  EXPECT_TRUE(out.good());

  const std::string &data = buffer.data();
  uint32_t declared_length = 0;
  const size_t data_pos = ParseBitHeader(data, declared_length);
  EXPECT_EQ(declared_length, words.size() * sizeof(uint32_t));
  EXPECT_EQ(declared_length, data.size() - data_pos);
  // The configuration data is the big-endian words from the iterator.
  ASSERT_EQ(data.size() - data_pos, words.size() * sizeof(uint32_t));
  for (size_t i = 0; i < words.size(); i++) {
    EXPECT_EQ(ReadBigEndian(data, data_pos + 4 * i, 4), words[i]) << i;
  }
}

// The same output must come out of a stream which can seek.
TEST(BitstreamWriterTest, HeaderLengthOnSeekableStream) {
  std::vector<std::unique_ptr<ConfigurationPacket>> packets;
  AddType1(packets);
  AddType2(packets);
  BitstreamWriter<kArch> writer(packets);

  std::ostringstream out;
  EXPECT_EQ(
    writer.writeBitstream(packets, "xc7a35tcsg324-1", "test", "test", out), 0);

  const std::string data = out.str();
  uint32_t declared_length = 0;
  const size_t data_pos = ParseBitHeader(data, declared_length);
  EXPECT_GT(declared_length, 0u);
  EXPECT_EQ(declared_length, data.size() - data_pos);
}
}  // namespace
}  // namespace xilinx
}  // namespace fpga
