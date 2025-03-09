/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_XC7_DEFS_H
#define FPGA_XILINX_ARCH_XC7_DEFS_H

#include <cstdint>
#include <ostream>

namespace fpga {
namespace xilinx {
namespace xc7 {
// Series-7 configuration register addresses
// according to UG470, pg. 109
enum class ConfigurationRegister : unsigned int {
  kCRC = 0x00,
  kFAR = 0x01,
  kFDRI = 0x02,
  kFDRO = 0x03,
  kCMD = 0x04,
  kCTL0 = 0x05,
  kMASK = 0x06,
  kSTAT = 0x07,
  kLOUT = 0x08,
  kCOR0 = 0x09,
  kMFWR = 0x0a,
  kCBC = 0x0b,
  kIDCODE = 0x0c,
  kAXSS = 0x0d,
  kCOR1 = 0x0e,
  kWBSTAR = 0x10,
  kTIMER = 0x11,
  kUNKNOWN = 0x13,
  kBOOTSTS = 0x16,
  kCTL1 = 0x18,
  kBSPI = 0x1F,
};

std::ostream &operator<<(std::ostream &o, const ConfigurationRegister &value);

enum class BlockType : unsigned int {
  kCLBIOCLK = 0x0,
  kBlockRam = 0x1,
  kCFGCLB = 0x2,
  /* reserved = 0x3, */
};

std::ostream &operator<<(std::ostream &o, BlockType value);

enum class Architecture {
  kBase,
  kUltrascale,
  kUltrascalePlus,
};

using word_t = uint32_t;

template <Architecture arch>
struct frame_words_count;

template <>
struct frame_words_count<Architecture::kBase> {
  static constexpr int value = 101;
};

template <>
struct frame_words_count<Architecture::kUltrascale> {
  static constexpr int value = 123;
};

template <>
struct frame_words_count<Architecture::kUltrascalePlus> {
  static constexpr int value = 93;
};

template <Architecture arch>
using frame_words_count_v = frame_words_count<arch>::value;
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_XC7_DEFS_H
