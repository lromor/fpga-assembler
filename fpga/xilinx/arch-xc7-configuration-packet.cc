#include "fpga/xilinx/arch-xc7-configuration-packet.h"

#include <cstdint>
#include <optional>

#include "absl/types/span.h"
#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/bit-ops.h"
#include "fpga/xilinx/configuration-packet.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
ConfigurationPacket::ParseResult ConfigurationPacket::InitWithWordsImpl(
  absl::Span<uint32_t> words, const ConfigurationPacket *previous_packet) {
  using ConfigurationRegister = ConfigurationRegister;
  // Need at least one 32-bit word to have a valid packet header.
  if (words.empty()) {
    return {words, {}};
  }
  const ConfigurationPacketType header_type =
    static_cast<ConfigurationPacketType>(bit_field_get(words[0], 31, 29));
  switch (header_type) {
  case ConfigurationPacketType::kNONE:
    // Type 0 is emitted at the end of a configuration row
    // when BITSTREAM.GENERAL.DEBUGBITSTREAM is set to YES.
    // These seem to be padding that are interepreted as
    // NOPs.  Since Type 0 packets don't exist according to
    // UG470 and they seem to be zero-filled, just consume
    // the bytes without generating a packet.
    return {words.subspan(1),
            {{static_cast<uint32_t>(header_type),
              Opcode::kNOP,
              ConfigurationRegister::kCRC,
              {}}}};
  case ConfigurationPacketType::kTYPE1: {
    const Opcode opcode = static_cast<Opcode>(bit_field_get(words[0], 28, 27));
    const ConfigurationRegister address =
      static_cast<ConfigurationRegister>(bit_field_get(words[0], 26, 13));
    const uint32_t data_word_count = bit_field_get(words[0], 10, 0);

    // If the full packet has not been received, return as
    // though no valid packet was found.
    if (data_word_count > words.size() - 1) {
      return {words, {}};
    }

    return {words.subspan(data_word_count + 1),
            {{static_cast<uint32_t>(header_type), opcode, address,
              words.subspan(1, data_word_count)}}};
  }
  case ConfigurationPacketType::kTYPE2: {
    std::optional<ConfigurationPacket> packet;
    const Opcode opcode = static_cast<Opcode>(bit_field_get(words[0], 28, 27));
    const uint32_t data_word_count = bit_field_get(words[0], 26, 0);

    // If the full packet has not been received, return as
    // though no valid packet was found.
    if (data_word_count > words.size() - 1) {
      return {words, {}};
    }

    if (previous_packet) {
      packet = ConfigurationPacket(static_cast<uint32_t>(header_type), opcode,
                                   previous_packet->address(),
                                   words.subspan(1, data_word_count));
    }

    return {words.subspan(data_word_count + 1), packet};
  }
  default: return {{}, {}};
  }
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
