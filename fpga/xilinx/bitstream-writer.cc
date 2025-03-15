#include "fpga/xilinx/bitstream-writer.h"

#include <cstdint>

#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-configuration-packet.h"
#include "fpga/xilinx/bit-ops.h"
#include "fpga/xilinx/configuration-packet.h"

namespace fpga {
namespace xilinx {
// Per UG470 pg 80: Bus Width Auto Detection
template <>
typename BitstreamWriter<Architecture::kXC7>::header_t
  BitstreamWriter<Architecture::kXC7>::header_{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x000000BB, 0x11220044,
    0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566};

template <>
typename BitstreamWriter<Architecture::kXC7UltraScale>::header_t
  BitstreamWriter<Architecture::kXC7UltraScale>::header_{
    0xFFFFFFFF, 0x000000BB, 0x11220044, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566};

template <>
typename BitstreamWriter<Architecture::kXC7UltraScalePlus>::header_t
  BitstreamWriter<Architecture::kXC7UltraScalePlus>::header_{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x000000BB, 0x11220044,
    0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566};

uint32_t PacketHeader(const xc7::ConfigurationPacket &packet) {
  uint32_t ret = 0;
  ret = bit_field_set(ret, 31, 29, packet.header_type());
  switch (static_cast<ConfigurationPacketType>(packet.header_type())) {
  case ConfigurationPacketType::kNONE:
    // Bitstreams are 0 padded sometimes, essentially making
    // a type 0 frame Ignore the other fields for now
    break;
  case ConfigurationPacketType::kTYPE1: {
    // Table 5-20: Type 1 Packet Header Format
    ret = bit_field_set(ret, 28, 27, packet.opcode());
    ret = bit_field_set(ret, 26, 13, packet.address());
    ret = bit_field_set(ret, 10, 0, packet.data().length());
    break;
  }
  case ConfigurationPacketType::kTYPE2: {
    // Table 5-22: Type 2 Packet Header
    // Note address is from previous type 1 header
    ret = bit_field_set(ret, 28, 27, packet.opcode());
    ret = bit_field_set(ret, 26, 0, packet.data().length());
    break;
  }
  default: break;
  }
  return ret;
}
}  // namespace xilinx
}  // namespace fpga
