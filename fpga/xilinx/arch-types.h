/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_H
#define FPGA_XILINX_ARCH_H

#include <memory>
#include <vector>

#include "fpga/xilinx/arch-xc7-configuration-packet.h"
#include "fpga/xilinx/arch-xc7-frame.h"
#include "fpga/xilinx/arch-xc7-part.h"

namespace fpga {
namespace xilinx {
enum class Architecture {
  kXC7,
  kXC7UltraScale,
  kXC7UltraScalePlus,
};

template <Architecture Arch>
struct ArchitectureTraits;

template <xc7::Architecture Arch>
struct ArchitectureXC7Base {
  using Part = xc7::Part;
  using ConfigurationPacket = xc7::ConfigurationPacket;
  using ConfigurationPackage =
    std::vector<std::unique_ptr<ConfigurationPacket>>;
  using ConfigurationRegister = ConfigurationPacket::ConfRegType;
  using FrameAddress = xc7::FrameAddress;
  using FrameWords = xc7::FrameWords<Arch>;
};

template <>
struct ArchitectureTraits<Architecture::kXC7> {
  using type = ArchitectureXC7Base<xc7::Architecture::kBase>;
};

template <>
struct ArchitectureTraits<Architecture::kXC7UltraScale> {
  using type = ArchitectureXC7Base<xc7::Architecture::kUltraScale>;
};

template <>
struct ArchitectureTraits<Architecture::kXC7UltraScalePlus> {
  using type = ArchitectureXC7Base<xc7::Architecture::kUltraScalePlus>;
};

template <Architecture Arch>
using ArchitectureType = ArchitectureTraits<Arch>::type;
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_H
