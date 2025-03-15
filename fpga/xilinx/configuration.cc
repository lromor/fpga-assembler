/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */

#include "fpga/xilinx/configuration.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/types/optional.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-configuration-packet.h"
#include "fpga/xilinx/configuration-packet.h"

namespace fpga {
namespace xilinx {
template <>
void Configuration<Architecture::kXC7>::CreateConfigurationPackage(
  ConfigurationPackage &out_packets, const PacketData &packet_data,
  absl::optional<Part> &part) {
  // Initialization sequence
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kTIMER,
      {0x0}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kWBSTAR,
      {0x0}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kNOP)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kRCRC)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kUNKNOWN,
      {0x0}));

  // Configuration Options 0
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCOR0,
      {static_cast<uint32_t>(
        xc7::ConfigurationOptions0Value()
          .SetAddPipelineStageForDoneIn(true)
          .SetReleaseDonePinAtStartupCycle(
            xc7::ConfigurationOptions0Value::SignalReleaseCycle::Phase4)
          .SetStallAtStartupCycleUntilDciMatch(
            xc7::ConfigurationOptions0Value::StallCycle::NoWait)
          .SetStallAtStartupCycleUntilMmcmLock(
            xc7::ConfigurationOptions0Value::StallCycle::NoWait)
          .SetReleaseGtsSignalAtStartupCycle(
            xc7::ConfigurationOptions0Value::SignalReleaseCycle::Phase5)
          .SetReleaseGweSignalAtStartupCycle(
            xc7::ConfigurationOptions0Value::SignalReleaseCycle::Phase6))}));

  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCOR1,
      {0x0}));
  CHECK(part.has_value());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kIDCODE,
      {part->idcode()}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kSWITCH)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kMASK,
      {0x401}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCTL0,
      {0x501}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kMASK,
      {0x0}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCTL1,
      {0x0}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kFAR, {0x0}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kWCFG)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());

  // Frame data write
  out_packets.emplace_back(new ConfigurationPacket(
    static_cast<uint32_t>(ConfigurationPacketType::kTYPE1),
    ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kFDRI, {}));
  out_packets.emplace_back(new ConfigurationPacket(
    static_cast<uint32_t>(ConfigurationPacketType::kTYPE2),
    ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kFDRI,
    packet_data));

  // Finalization sequence
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kRCRC)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kGRESTORE)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kLFRM)}));
  for (int ii = 0; ii < 100; ++ii) {
    out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  }
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kSTART)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kFAR,
      {0x3be0000}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kMASK,
      {0x501}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCTL0,
      {0x501}));
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kRCRC)}));
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  out_packets.emplace_back(
    new ConfigurationPacketWithPayload<1, ConfigurationPacket>(
      ConfigurationPacket::Opcode::kWrite, ConfigurationRegister::kCMD,
      {static_cast<uint32_t>(xc7::Command::kDESYNC)}));
  for (int ii = 0; ii < 400; ++ii) {
    out_packets.emplace_back(new NopPacket<ConfigurationPacket>());
  }
}
}  // namespace xilinx
}  // namespace fpga
