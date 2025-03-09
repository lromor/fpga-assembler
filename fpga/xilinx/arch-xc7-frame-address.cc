#include "fpga/xilinx/arch-xc7-frame-address.h"

#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>

#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/bit-ops.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
FrameAddress::FrameAddress(BlockType block_type, bool is_bottom_half_rows,
                           uint8_t row, uint16_t column, uint8_t minor) {
  address_ = bit_field_set(0, 25, 23, block_type);
  address_ = bit_field_set(address_, 22, 22, is_bottom_half_rows);
  address_ = bit_field_set(address_, 21, 17, row);
  address_ = bit_field_set(address_, 16, 7, column);
  address_ = bit_field_set(address_, 6, 0, minor);
}

BlockType FrameAddress::block_type() const {
  return static_cast<BlockType>(bit_field_get(address_, 25, 23));
}

bool FrameAddress::is_bottom_half_rows() const {
  return bit_field_get(address_, 22, 22);
}

uint8_t FrameAddress::row() const { return bit_field_get(address_, 21, 17); }

uint16_t FrameAddress::column() const { return bit_field_get(address_, 16, 7); }

uint8_t FrameAddress::minor() const { return bit_field_get(address_, 6, 0); }

std::ostream &operator<<(std::ostream &o, const FrameAddress &addr) {
  o << "[" << std::hex << std::showbase << std::setw(10)
    << static_cast<uint32_t>(addr) << "] "
    << (addr.is_bottom_half_rows() ? "BOTTOM" : "TOP")
    << " Row=" << std::setw(2) << std::dec
    << static_cast<unsigned int>(addr.row()) << " Column=" << std::setw(2)
    << std::dec << addr.column() << " Minor=" << std::setw(2) << std::dec
    << static_cast<unsigned int>(addr.minor()) << " Type=" << addr.block_type();
  return o;
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
