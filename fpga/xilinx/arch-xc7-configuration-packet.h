/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_XC7_CONFIGURATION_PACKET_H
#define FPGA_XILINX_ARCH_XC7_CONFIGURATION_PACKET_H

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/types/span.h"
#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/configuration-packet.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
class ConfigurationPacket
    : public ConfigurationPacketBase<ConfigurationRegister,
                                     ConfigurationPacket> {
 private:
  using BaseType =
    ConfigurationPacketBase<ConfigurationRegister, ConfigurationPacket>;

 public:
  using BaseType::BaseType;

 private:
  friend BaseType;
  static ParseResult InitWithWordsImpl(
    absl::Span<uint32_t> words,
    const ConfigurationPacket *previous_packet = nullptr);
};

using ConfigurationPackage = std::vector<std::unique_ptr<ConfigurationPacket>>;
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_XC7_CONFIGURATION_PACKET_H
