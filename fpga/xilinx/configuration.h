/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_CONFIGURATION_H
#define FPGA_XILINX_CONFIGURATION_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/bit-ops.h"

namespace fpga {
namespace xilinx {
template <Architecture Arch>
class Configuration {
 private:
  using ArchType = ArchitectureType<Arch>;
  using FrameWords = ArchType::FrameWords;
  using FrameAddress = ArchType::FrameAddress;
  using ConfigurationPacket = ArchType::ConfigurationPacket;
  using ConfigurationPackage = ArchType::ConfigurationPackage;
  using ConfigurationRegister = ArchType::ConfigurationRegister;
  using Part = ArchType::Part;
  static constexpr auto kWordsPerFrame = std::tuple_size_v<FrameWords>;

 public:
  using FrameMap = absl::btree_map<FrameAddress, absl::Span<const uint32_t>>;
  using PacketData = std::vector<uint32_t>;

  // Returns a configuration, i.e. collection of frame addresses
  // and corresponding data from a collection of configuration packets.
  template <typename Collection>
  static std::optional<Configuration<Arch>> InitWithPackets(
    const Part &part, Collection &packets);

  // Creates the complete configuration package which is later on
  // used by the bitstream writer to generate the bitstream file.
  // The pacakge forms a sequence suitable for Xilinx devices.
  // The programming sequence for Series-7 is taken from
  // https://www.kc8apf.net/2018/05/unpacking-xilinx-7-series-bitstreams-part-2/
  static void CreateConfigurationPackage(ConfigurationPackage &out_packets,
                                         const PacketData &packet_data,
                                         std::optional<Part> &part);

  // Returns the payload for a type 2 packet
  // which allows for bigger payload compared to type 1.
  static PacketData CreateType2ConfigurationPacketData(
    const absl::btree_map<FrameAddress, FrameWords> &frames,
    std::optional<Part> &part) {
    PacketData packet_data;
    // Certain configuration frames blocks are separated by Zero Frames,
    // i.e. frames with words with all zeroes. For Series-7, US and US+
    // there zero frames separator consists of two frames.
    static const int kZeroFramesSeparatorWords = kWordsPerFrame * 2;
    for (auto &frame : frames) {
      std::copy(frame.second.begin(), frame.second.end(),
                std::back_inserter(packet_data));

      auto next_address = part->GetNextFrameAddress(frame.first);
      if (next_address &&
          (next_address->block_type() != frame.first.block_type() ||
           next_address->is_bottom_half_rows() !=
             frame.first.is_bottom_half_rows() ||
           next_address->row() != frame.first.row())) {
        packet_data.insert(packet_data.end(), kZeroFramesSeparatorWords, 0);
      }
    }
    packet_data.insert(packet_data.end(), kZeroFramesSeparatorWords, 0);
    return packet_data;
  }

  Configuration(const Part &part,
                absl::btree_map<FrameAddress, FrameWords> &frames)
      : part_(part) {
    for (auto &frame : frames) {
      frames_[frame.first] = absl::Span<const uint32_t>(frame.second);
    }
  }

  Configuration(const Part &part, const FrameMap &frames)
      : part_(part), frames_(std::move(frames)) {}

  const Part &part() const { return part_; }
  const FrameMap &frames() const { return frames_; }
  void PrintFrameAddresses(FILE *fp);

 private:
  Part part_;
  FrameMap frames_;
};

template <Architecture Arch>
template <typename Collection>
absl::optional<Configuration<Arch>> Configuration<Arch>::InitWithPackets(
  const Part &part, Collection &packets) {
  // Registers that can be directly written to.
  uint32_t command_register = 0;
  uint32_t frame_address_register = 0;
  uint32_t mask_register = 0;
  uint32_t ctl1_register = 0;

  // Internal state machine for writes.
  bool start_new_write = false;
  FrameAddress current_frame_address = static_cast<FrameAddress>(0);

  Configuration<Arch>::FrameMap frames;
  for (auto packet : packets) {
    if (packet.opcode() != ConfigurationPacket::Opcode::kWrite) {
      continue;
    }

    switch (packet.address()) {
    case ConfigurationRegister::kMASK:
      if (packet.data().size() < 1) {
        continue;
      }
      mask_register = packet.data()[0];
      break;
    case ConfigurationRegister::kCTL1:
      if (packet.data().size() < 1) {
        continue;
      }
      ctl1_register = packet.data()[0] & mask_register;
      break;
    case ConfigurationRegister::kCMD:
      if (packet.data().size() < 1) {
        continue;
      }
      command_register = packet.data()[0];
      // Writes to CMD trigger an immediate action. In
      // the case of WCFG, that is just setting a flag
      // for the next FDIR.
      if (command_register == 0x1) {
        start_new_write = true;
      }
      break;
    case ConfigurationRegister::kIDCODE:
      // This really should be a one-word write.
      if (packet.data().size() < 1) {
        continue;
      }

      // If the IDCODE doesn't match our expected
      // part, consider the bitstream invalid.
      if (packet.data()[0] != part.idcode()) {
        return {};
      }
      break;
    case ConfigurationRegister::kFAR:
      // This really should be a one-word write.
      if (packet.data().size() < 1) {
        continue;
      }
      frame_address_register = static_cast<uint32_t>(packet.data()[0]);

      // Per UG470, the command present in the CMD
      // register is executed each time the FAR
      // register is laoded with a new value.  As we
      // only care about WCFG commands, just check
      // that here.  CTRL1 is completely undocumented
      // but looking at generated bitstreams, bit 21
      // is used when per-frame CRC is enabled.
      // Setting this bit seems to inhibit the
      // re-execution of CMD during a FAR write.  In
      // practice, this is used so FAR writes can be
      // added in the bitstream to show progress
      // markers without impacting the actual write
      // operation.
      if (bit_field_get(ctl1_register, 21, 21) == 0 &&
          command_register == 0x1) {
        start_new_write = true;
      }
      break;
    case ConfigurationRegister::kFDRI: {
      if (start_new_write) {
        current_frame_address =
          static_cast<FrameAddress>(frame_address_register);
        start_new_write = false;
      }

      // Number of words in configuration frames
      // depend on tje architecture.  Writes to this
      // register can be multiples of that number to
      // do auto-incrementing block writes.
      for (size_t ii = 0; ii < packet.data().size(); ii += kWordsPerFrame) {
        frames[current_frame_address] =
          packet.data().subspan(ii, kWordsPerFrame);

        auto next_address = part.GetNextFrameAddress(current_frame_address);
        if (!next_address) {
          break;
        }

        // Bitstreams appear to have 2 frames of
        // padding between rows.
        if (next_address &&
            (next_address->block_type() != current_frame_address.block_type() ||
             next_address->is_bottom_half_rows() !=
               current_frame_address.is_bottom_half_rows() ||
             next_address->row() != current_frame_address.row())) {
          ii += 2 * kWordsPerFrame;
        }
        current_frame_address = *next_address;
      }
      break;
    }
    default: break;
    }
  }
  return Configuration(part, frames);
}

template <Architecture Arch>
void Configuration<Arch>::PrintFrameAddresses(FILE *fp) {
  fprintf(fp, "Frame addresses in bitstream: ");
  for (auto frame = frames_.begin(); frame != frames_.end(); ++frame) {
    fprintf(fp, "%08X", (int)frame->first);
    if (std::next(frame) != frames_.end()) {
      fprintf(fp, " ");
    } else {
      fprintf(fp, "\n");
    }
  }
}
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_CONFIGURATION_H
