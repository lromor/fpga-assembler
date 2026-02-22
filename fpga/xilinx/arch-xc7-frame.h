/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_XC7_FRAME_H
#define FPGA_XILINX_ARCH_XC7_FRAME_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include "fpga/xilinx/bit-ops.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
enum class Architecture {
  kBase,
  kUltraScale,
  kUltraScalePlus,
};

template <Architecture Arch>
struct frame_words_count;

template <>
struct frame_words_count<Architecture::kBase> {
  static constexpr int value = 101;
};

template <>
struct frame_words_count<Architecture::kUltraScale> {
  static constexpr int value = 123;
};

template <>
struct frame_words_count<Architecture::kUltraScalePlus> {
  static constexpr int value = 93;
};

template <Architecture Arch>
constexpr int frame_words_count_v = frame_words_count<Arch>::value;

enum class BlockType : unsigned int {
  kCLBIOCLK = 0x0,
  kBlockRam = 0x1,
  kCFGCLB = 0x2,
  kReserved = 0x3,
  kInvalid = 0xFFFFFFFF,
};

std::ostream &operator<<(std::ostream &o, BlockType value);

using FrameWord = uint32_t;

class FrameAddress {
 public:
  FrameAddress(BlockType block_type, bool is_bottom_half_rows, uint8_t row,
               uint16_t column, uint8_t minor) {
    value_ = bit_field_set(0, 25, 23, block_type);
    value_ = bit_field_set(value_, 22, 22, is_bottom_half_rows);
    value_ = bit_field_set(value_, 21, 17, row);
    value_ = bit_field_set(value_, 16, 7, column);
    value_ = bit_field_set(value_, 6, 0, minor);
  }
  explicit FrameAddress(uint32_t value) : value_(value) {};
  auto operator<=>(const FrameAddress &o) const = default;

  BlockType block_type() const {
    return static_cast<BlockType>(bit_field_get(value_, 25, 23));
  }
  bool is_bottom_half_rows() const { return bit_field_get(value_, 22, 22); }
  uint8_t row() const { return bit_field_get(value_, 21, 17); }
  uint16_t column() const { return bit_field_get(value_, 16, 7); }
  uint8_t minor() const { return bit_field_get(value_, 6, 0); }
  explicit operator uint32_t() const { return value_; }

 private:
  uint32_t value_;
};
static_assert(sizeof(FrameAddress) == sizeof(uint32_t));

std::ostream &operator<<(std::ostream &o, const FrameAddress &addr);

template <Architecture arch>
using FrameWords = std::array<FrameWord, frame_words_count_v<arch>>;

namespace internal {
// Extend the current ECC code with one data word (32 bit) at a given
// word index in the configuration frame and return the new ECC code.
uint32_t ICAPECC(uint32_t idx, uint32_t word, uint32_t ecc);

inline constexpr size_t kECCFrameNumber = 0x32;
inline constexpr uint32_t kCrc32CastagnoliPolynomial = 0x82F63B78;

// The CRC is calculated from each written data word and the current
// register address the data is written to.

// Extend the current CRC value with one register address (5bit) and
// frame data (32bit) pair and return the newly computed CRC value.
inline uint32_t ICAPCRC(uint32_t addr, uint32_t data, uint32_t prev) {
  constexpr int kAddressBitWidth = 5;
  constexpr int kDataBitWidth = 32;
  uint64_t const poly = static_cast<uint64_t>(kCrc32CastagnoliPolynomial) << 1;
  uint64_t val = (static_cast<uint64_t>(addr) << 32) | data;
  uint64_t crc = prev;

  for (int i = 0; i < kAddressBitWidth + kDataBitWidth; i++) {
    if ((val & 1) != (crc & 1)) {
      crc ^= poly;
    }

    val >>= 1;
    crc >>= 1;
  }
  return crc;
}

template <Architecture arch>
uint32_t CalculateECC(const FrameWords<arch> &data) {
  FrameWord ecc = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    ecc = ICAPECC(i, data[i], ecc);
  }
  return ecc;
}
}  // namespace internal

// Updates the ECC information in the frame.
template <Architecture arch>
void UpdateECC(FrameWords<arch> &words) {
  CHECK(words.size() >= internal::kECCFrameNumber);
  // Replace the old ECC with the new.
  words[internal::kECCFrameNumber] &= 0xFFFFE000;
  words[internal::kECCFrameNumber] |= (internal::CalculateECC(words) & 0x1FFF);
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_XC7_FRAME_H
