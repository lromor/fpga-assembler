/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
#ifndef FPGA_XILINX_ARCH_XC7_PART_H
#define FPGA_XILINX_ARCH_XC7_PART_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "fpga/xilinx/arch-xc7-frame.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
// ConfigurationColumn represents an endpoint on a ConfigurationBus.
class ConfigurationColumn {
 public:
  ConfigurationColumn() = default;
  explicit ConfigurationColumn(unsigned int frame_count)
      : frame_count_(frame_count) {}

  // Returns a ConfigurationColumn that describes a continguous range of
  // minor addresses that encompasses the given
  // frame addresses.  The provided addresses must only
  // differ only by their minor addresses.
  template <typename T>
  ConfigurationColumn(T first, T last);

  // Returns true if the minor field of the address is within the valid
  // range of this column.
  bool IsValidFrameAddress(FrameAddress address) const;

  // Returns the next address in numerical order.  If the next address
  // would be outside this column, return no object.
  std::optional<FrameAddress> GetNextFrameAddress(FrameAddress address) const;

 private:
  unsigned int frame_count_;
};

template <typename T>
ConfigurationColumn::ConfigurationColumn(T first, T last) {
  assert(std::all_of(first, last, [&](const typename T::value_type &addr) {
    return (addr.block_type() == first->block_type() &&
            addr.is_bottom_half_rows() == first->is_bottom_half_rows() &&
            addr.row() == first->row() && addr.column() == first->column());
  }));

  auto max_minor = std::max_element(
    first, last, [](const FrameAddress &lhs, const FrameAddress &rhs) {
      return lhs.minor() < rhs.minor();
    });

  frame_count_ = max_minor->minor() + 1;
}

// ConfigurationBus represents a bus for sending frames to a specific BlockType
// within a Row.  An instance of ConfigurationBus will contain one or more
// ConfigurationColumns.
class ConfigurationBus {
 public:
  ConfigurationBus() = default;

  // Constructs a ConfigurationBus from iterators yielding
  // frame addresses.  The frame address need not be contiguous or sorted
  // but they must all have the same block type, row half, and row
  // address components.
  template <typename T>
  ConfigurationBus(T first, T last);

  // Returns true if the provided address falls into a valid segment of
  // the address range on this bus.  Only the column and minor components
  // of the address are considered as all other components are outside
  // the scope of a bus.
  bool IsValidFrameAddress(FrameAddress address) const;

  // Returns the next valid address on the bus in numerically increasing
  // order. If the next address would fall outside this bus, no object is
  // returned.
  std::optional<FrameAddress> GetNextFrameAddress(FrameAddress address) const;

 private:
  std::map<unsigned int, ConfigurationColumn> configuration_columns_;
};

template <typename T>
ConfigurationBus::ConfigurationBus(T first, T last) {
  assert(std::all_of(first, last, [&](const typename T::value_type &addr) {
    return (addr.block_type() == first->block_type() &&
            addr.is_bottom_half_rows() == first->is_bottom_half_rows() &&
            addr.row() == first->row());
  }));

  std::sort(first, last, [](const FrameAddress &lhs, const FrameAddress &rhs) {
    return lhs.column() < rhs.column();
  });

  for (auto col_first = first; col_first != last;) {
    auto col_last =
      std::upper_bound(col_first, last, col_first->column(),
                       [](const unsigned int &lhs, const FrameAddress &rhs) {
                         return lhs < rhs.column();
                       });

    configuration_columns_.emplace(col_first->column(),
                                   ConfigurationColumn(col_first, col_last));
    col_first = col_last;
  }
}

class Row {
 public:
  Row() = default;

  // Construct a row from a range of iterators that yield frame addresses.
  // The addresses may be noncontinguous and/or unsorted but all must
  // share the same row half and row components.
  template <typename T>
  Row(T first, T last);

  // Returns true if the provided address falls within a valid range
  // attributed to this row.  Only the block type, column, and minor
  // address components are considerd as the remaining components are
  // outside the scope of a row.
  bool IsValidFrameAddress(FrameAddress address) const;

  // Returns the next numerically increasing address within the Row. If
  // the next address would fall outside the Row, no object is returned.
  // If the next address would cross from one block type to another, no
  // object is returned as other rows of the same block type come before
  // other block types numerically.
  std::optional<FrameAddress> GetNextFrameAddress(FrameAddress address) const;

 private:
  std::map<BlockType, ConfigurationBus> configuration_buses_;
};

template <typename T>
Row::Row(T first, T last) {
  assert(std::all_of(first, last, [&](const typename T::value_type &addr) {
    return (addr.is_bottom_half_rows() == first->is_bottom_half_rows() &&
            addr.row() == first->row());
  }));

  std::sort(first, last, [](const FrameAddress &lhs, const FrameAddress &rhs) {
    return lhs.block_type() < rhs.block_type();
  });

  for (auto bus_first = first; bus_first != last;) {
    auto bus_last =
      std::upper_bound(bus_first, last, bus_first->block_type(),
                       [](const BlockType &lhs, const FrameAddress &rhs) {
                         return lhs < rhs.block_type();
                       });

    configuration_buses_.emplace(
      bus_first->block_type(),
      std::move(ConfigurationBus(bus_first, bus_last)));
    bus_first = bus_last;
  }
}

// GlobalClockRegion represents all the resources associated with a single
// global clock buffer (BUFG) tile.  In 7-Series FPGAs, there are two BUFG
// tiles that divide the chip into top and bottom "halves". Each half may
// contains any number of rows, buses, and columns.
class GlobalClockRegion {
 public:
  GlobalClockRegion() = default;

  // Construct a GlobalClockRegion from iterators that yield
  // frame addresses which are known to be valid. The addresses may be
  // noncontinguous and/or unordered but they must share the same row
  // half address component.
  template <typename T>
  GlobalClockRegion(T first, T last);

  // Returns true if the address falls within a valid range inside the
  // global clock region. The row half address component is ignored as it
  // is outside the context of a global clock region.
  bool IsValidFrameAddress(FrameAddress address) const;

  // Returns the next numerically increasing address known within this
  // global clock region. If the next address would fall outside this
  // global clock region, no address is returned. If the next address
  // would jump to a different block type, no address is returned as the
  // same block type in other global clock regions come numerically
  // before other block types.
  std::optional<FrameAddress> GetNextFrameAddress(FrameAddress address) const;

 private:
  std::map<unsigned int, Row> rows_;
};

template <typename T>
GlobalClockRegion::GlobalClockRegion(T first, T last) {
  assert(std::all_of(first, last, [&](const typename T::value_type &addr) {
    return addr.is_bottom_half_rows() == first->is_bottom_half_rows();
  }));

  std::sort(first, last, [](const FrameAddress &lhs, const FrameAddress &rhs) {
    return lhs.row() < rhs.row();
  });

  for (auto row_first = first; row_first != last;) {
    auto row_last =
      std::upper_bound(row_first, last, row_first->row(),
                       [](const uint8_t &lhs, const FrameAddress &rhs) {
                         return lhs < rhs.row();
                       });

    rows_.emplace(row_first->row(), std::move(Row(row_first, row_last)));
    row_first = row_last;
  }
}

class Part {
 public:
  constexpr static uint32_t kInvalidIdcode = 0;

  static std::optional<Part> FromFile(const std::string &path);

  // Constructs an invalid part with a zero IDCODE. Required for YAML
  // conversion but shouldn't be used otherwise.
  Part() : idcode_(kInvalidIdcode) {}

  template <typename T>
  Part(uint32_t idcode, T collection)
      : Part(idcode, std::begin(collection), std::end(collection)) {}

  template <typename T>
  Part(uint32_t idcode, T first, T last);

  uint32_t idcode() const { return idcode_; }

  bool IsValidFrameAddress(FrameAddress address) const;

  std::optional<FrameAddress> GetNextFrameAddress(FrameAddress address) const;

 private:
  uint32_t idcode_;
  GlobalClockRegion top_region_;
  GlobalClockRegion bottom_region_;
};

template <typename T>
Part::Part(uint32_t idcode, T first, T last) : idcode_(idcode) {
  auto first_of_top = std::partition(first, last, [](const FrameAddress &addr) {
    return addr.is_bottom_half_rows();
  });
  top_region_ = GlobalClockRegion(first_of_top, last);
  bottom_region_ = GlobalClockRegion(first, first_of_top);
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_ARCH_XC7_PART_H
