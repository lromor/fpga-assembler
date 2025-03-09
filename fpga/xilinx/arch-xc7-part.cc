#include "fpga/xilinx/arch-xc7-part.h"

#include <cstdint>
#include <optional>

#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/arch-xc7-frame-address.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
bool ConfigurationColumn::IsValidFrameAddress(FrameAddress address) const {
  return address.minor() < frame_count_;
}

std::optional<FrameAddress> ConfigurationColumn::GetNextFrameAddress(
  FrameAddress address) const {
  if (!IsValidFrameAddress(address)) {
    return {};
  }

  if (static_cast<unsigned int>(address.minor() + 1) < frame_count_) {
    return FrameAddress(static_cast<uint32_t>(address) + 1);
  }

  // Next address is not in this column.
  return {};
}

bool ConfigurationBus::IsValidFrameAddress(FrameAddress address) const {
  auto addr_column = configuration_columns_.find(address.column());
  if (addr_column == configuration_columns_.end()) {
    return false;
  }

  return addr_column->second.IsValidFrameAddress(address);
}

std::optional<FrameAddress> ConfigurationBus::GetNextFrameAddress(
  FrameAddress address) const {
  // Find the column for the current address.
  auto addr_column = configuration_columns_.find(address.column());

  // If the current address isn't in a known column, no way to know the
  // next address.
  if (addr_column == configuration_columns_.end()) {
    return {};
  }

  // Ask the column for the next address.
  std::optional<FrameAddress> next_address =
    addr_column->second.GetNextFrameAddress(address);
  if (next_address) {
    return next_address;
  }

  // The current column doesn't know what the next address is.  Assume
  // that the next valid address is the beginning of the next column.
  if (++addr_column != configuration_columns_.end()) {
    auto next_address =
      FrameAddress(address.block_type(), address.is_bottom_half_rows(),
                   address.row(), addr_column->first, 0);
    if (addr_column->second.IsValidFrameAddress(next_address)) {
      return next_address;
    }
  }

  // Not in this bus.
  return {};
}

bool Row::IsValidFrameAddress(FrameAddress address) const {
  auto addr_bus = configuration_buses_.find(address.block_type());
  if (addr_bus == configuration_buses_.end()) {
    return false;
  }
  return addr_bus->second.IsValidFrameAddress(address);
}

std::optional<FrameAddress> Row::GetNextFrameAddress(
  FrameAddress address) const {
  // Find the bus for the current address.
  auto addr_bus = configuration_buses_.find(address.block_type());

  // If the current address isn't in a known bus, no way to know the next.
  if (addr_bus == configuration_buses_.end()) {
    return {};
  }

  // Ask the bus for the next address.
  std::optional<FrameAddress> next_address =
    addr_bus->second.GetNextFrameAddress(address);
  if (next_address) {
    return next_address;
  }

  // The current bus doesn't know what the next address is. Rows come next
  // in frame address numerical order so punt back to the caller to figure
  // it out.
  return {};
}

bool GlobalClockRegion::IsValidFrameAddress(FrameAddress address) const {
  auto addr_row = rows_.find(address.row());
  if (addr_row == rows_.end()) {
    return false;
  }
  return addr_row->second.IsValidFrameAddress(address);
}

std::optional<FrameAddress> GlobalClockRegion::GetNextFrameAddress(
  FrameAddress address) const {
  // Find the row for the current address.
  auto addr_row = rows_.find(address.row());

  // If the current address isn't in a known row, no way to know the next.
  if (addr_row == rows_.end()) {
    return {};
  }

  // Ask the row for the next address.
  std::optional<FrameAddress> next_address =
    addr_row->second.GetNextFrameAddress(address);
  if (next_address) {
    return next_address;
  }

  // The current row doesn't know what the next address is.  Assume that
  // the next valid address is the beginning of the next row.
  if (++addr_row != rows_.end()) {
    auto next_address =
      FrameAddress(address.block_type(), address.is_bottom_half_rows(),
                   addr_row->first, 0, 0);
    if (addr_row->second.IsValidFrameAddress(next_address)) {
      return next_address;
    }
  }

  // Must be in a different global clock region.
  return {};
}

bool Part::IsValidFrameAddress(FrameAddress address) const {
  if (address.is_bottom_half_rows()) {
    return bottom_region_.IsValidFrameAddress(address);
  }
  return top_region_.IsValidFrameAddress(address);
}

std::optional<FrameAddress> Part::GetNextFrameAddress(
  FrameAddress address) const {
  // Ask the current global clock region first.
  std::optional<FrameAddress> next_address =
    (address.is_bottom_half_rows() ? bottom_region_.GetNextFrameAddress(address)
                                   : top_region_.GetNextFrameAddress(address));
  if (next_address) {
    return next_address;
  }

  // If the current address is in the top region, the bottom region is
  // next numerically.
  if (!address.is_bottom_half_rows()) {
    next_address = FrameAddress(address.block_type(), true, 0, 0, 0);
    if (bottom_region_.IsValidFrameAddress(*next_address)) {
      return next_address;
    }
  }

  // Block types are next numerically.
  if (address.block_type() < BlockType::kBlockRam) {
    next_address = FrameAddress(BlockType::kBlockRam, false, 0, 0, 0);
    if (IsValidFrameAddress(*next_address)) {
      return next_address;
    }
  }

  if (address.block_type() < BlockType::kCFGCLB) {
    next_address = FrameAddress(BlockType::kCFGCLB, false, 0, 0, 0);
    if (IsValidFrameAddress(*next_address)) {
      return next_address;
    }
  }
  return {};
}

}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
