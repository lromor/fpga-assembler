/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_CONFIGURATION_PACKET_H
#define FPGA_XILINX_CONFIGURATION_PACKET_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <utility>

#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace fpga {
namespace xilinx {
// As described in the configuration user guide for Series-7
// (UG470, pg. 108) there are two types of configuration packets
enum class ConfigurationPacketType : uint32_t { kNONE, kTYPE1, kTYPE2 };

// Creates a Type1 or Type2 configuration packet.
// Specification of the packets for Series-7 can be found in UG470, pg. 108
// For Spartan6 UG380, pg. 98
template <typename ConfigRegType, typename Derived>
class ConfigurationPacketBase {
 public:
  using ParseResult =
    std::pair<absl::Span<uint32_t>,
              absl::optional<ConfigurationPacketBase<ConfigRegType, Derived>>>;

  // Opcodes as specified in UG470 page 108
  enum class Opcode {
    kNOP = 0,
    kRead = 1,
    kWrite = 2,
    // reserved = 3
  };

  ConfigurationPacketBase(uint32_t header_type, Opcode opcode,
                          ConfigRegType address,
                          const absl::Span<const uint32_t> &data)
      : header_type_(header_type),
        opcode_(opcode),
        address_(address),
        data_(data) {}

  unsigned int header_type() const { return header_type_; }
  Opcode opcode() const { return opcode_; }
  ConfigRegType address() const { return address_; }
  const absl::Span<const uint32_t> &data() const { return data_; }
  static ParseResult InitWithWords(absl::Span<uint32_t> words,
                                   const Derived *previous_packet = nullptr) {
    return Derived::InitWithWordsImpl(words, previous_packet);
  }

 private:
  unsigned int header_type_;
  Opcode opcode_;
  ConfigRegType address_;
  absl::Span<const uint32_t> data_;
};

template <typename ConfigRegType, typename Derived>
inline std::ostream &operator<<(
  std::ostream &o,
  const ConfigurationPacketBase<ConfigRegType, Derived> &packet) {
  if (packet.header_type() == 0x0) {
    return o << "[Zero-pad]" << '\n';
  }
  using Opcode = ConfigurationPacketBase<ConfigRegType, Derived>::Opcode;
  switch (packet.opcode()) {
  case Opcode::NOP: o << "[NOP]" << '\n'; break;
  case Opcode::Read:
    o << "[Read Type=";
    o << packet.header_type();
    o << " Address=";
    o << std::setw(2) << std::hex;
    o << static_cast<int>(packet.address());
    o << " Length=";
    o << std::setw(10) << std::dec << packet.data().size();
    o << " Reg=\"" << packet.address() << "\"";
    o << "]" << '\n';
    break;
  case Opcode::Write:
    o << "[Write Type=";
    o << packet.header_type();
    o << " Address=";
    o << std::setw(2) << std::hex;
    o << static_cast<int>(packet.address());
    o << " Length=";
    o << std::setw(10) << std::dec << packet.data().size();
    o << " Reg=\"" << packet.address() << "\"";
    o << "]" << '\n';
    o << "Data in hex:" << '\n';

    for (size_t ii = 0; ii < packet.data().size(); ++ii) {
      o << std::setw(8) << std::hex;
      o << packet.data()[ii] << " ";

      if ((ii + 1) % 4 == 0) {
        o << '\n';
      }
    }
    if (packet.data().size() % 4 != 0) {
      o << '\n';
    }
    break;
  default: o << "[Invalid Opcode]" << '\n';
  }
  return o;
}

template <int Words, typename ConfigurationPacket>
class ConfigurationPacketWithPayload : public ConfigurationPacket {
 private:
  using ConfRegType = ConfigurationPacket::ConfRegType;

 public:
  ConfigurationPacketWithPayload(ConfigurationPacket::Opcode op,
                                 ConfRegType reg,
                                 const std::array<uint32_t, Words> &payload)
      : ConfigurationPacket(
          static_cast<uint32_t>(ConfigurationPacketType::kTYPE1), op, reg,
          absl::Span<uint32_t>(payload_)),
        payload_(std::move(payload)) {}

 private:
  std::array<uint32_t, Words> payload_;
};

template <typename ConfigurationPacket>
class NopPacket : public ConfigurationPacket {
 public:
  using ConfRegType = ConfigurationPacket::ConfRegType;
  NopPacket()
      : ConfigurationPacket(
          static_cast<uint32_t>(ConfigurationPacketType::kTYPE1),
          ConfigurationPacket::Opcode::kNOP, ConfRegType::kCRC, {}) {}
};
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_CONFIGURATION_PACKET_H
