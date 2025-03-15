/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_FRAMES_H
#define FPGA_XILINX_FRAMES_H

#include <map>
#include <optional>

#include "fpga/xilinx/arch-types.h"

namespace fpga {
namespace xilinx {
// Contains frame information which is used for the generation
// of the configuration package that is used in bitstream generation.
template <Architecture Arch>
class Frames {
 public:
  using ArchType = ArchitectureType<Arch>;
  using FrameWords = ArchType::FrameWords;
  using FrameAddress = ArchType::FrameAddress;
  using Part = ArchType::Part;

  void AddFrame(FrameAddress address, FrameWords words) {
    UpdateECC(data_);
    data_.insert(address, words);
  }

  Frames() = default;
  explicit Frames(std::map<FrameAddress, FrameWords> data)
      : data_(std::move(data)) {}

  // Adds empty frames that are present in the tilegrid of a specific part
  // but are missing in the current frames container.
  void AddMissingFrames(const std::optional<Part> &part);

  // Returns the map with frame addresses and corresponding data
  std::map<FrameAddress, FrameWords> &GetFrames() { return data_; }

 private:
  std::map<FrameAddress, FrameWords> data_;

  // Updates the ECC information in the frame
  void UpdateECC(FrameWords &words);
};

template <Architecture Arch>
void Frames<Arch>::AddMissingFrames(const std::optional<Part> &part) {
  auto current_frame_address = std::optional<FrameAddress>(FrameAddress(0));
  do {
    auto iter = data_.find(*current_frame_address);
    if (iter == data_.end()) {
      data_.insert({*current_frame_address, {}});
    }
    current_frame_address = part->GetNextFrameAddress(*current_frame_address);
  } while (current_frame_address);
}
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_FRAMES_H
