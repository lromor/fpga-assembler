/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_XC7_FRAME_ADDRESS_H
#define FPGA_XILINX_ARCH_XC7_FRAME_ADDRESS_H

#include <cstdint>
#include <ostream>

#include "fpga/xilinx/arch-xc7-defs.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
class FrameAddress {
 public:
  FrameAddress() : address_(0) {}
  explicit FrameAddress(uint32_t address) : address_(address){};
  FrameAddress(BlockType block_type, bool is_bottom_half_rows, uint8_t row,
               uint16_t column, uint8_t minor);

  explicit operator uint32_t() const { return address_; }

  BlockType block_type() const;
  bool is_bottom_half_rows() const;
  uint8_t row() const;
  uint16_t column() const;
  uint8_t minor() const;

 private:
  uint32_t address_;
};

inline bool operator==(const FrameAddress &lhs, const FrameAddress &rhs) {
  return static_cast<uint32_t>(lhs) == static_cast<uint32_t>(rhs);
}

std::ostream &operator<<(std::ostream &o, const FrameAddress &addr);
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_XC7_FRAME_ADDRESS_H
