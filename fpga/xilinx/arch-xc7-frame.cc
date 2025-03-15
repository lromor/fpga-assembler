#include "fpga/xilinx/arch-xc7-frame.h"

#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>

namespace fpga {
namespace xilinx {
namespace xc7 {
std::ostream &operator<<(std::ostream &o, BlockType value) {
  switch (value) {
  case BlockType::kCLBIOCLK: o << "CLB/IO/CLK"; break;
  case BlockType::kBlockRam: o << "Block RAM"; break;
  case BlockType::kCFGCLB: o << "Config CLB"; break;
  case BlockType::kReserved: o << "Reserved"; break;
  }
  return o;
}

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
namespace internal {
uint32_t ICAPECC(uint32_t idx, uint32_t word, uint32_t ecc) {
  uint32_t val = idx * 32;  // bit offset
  if (idx > 0x25) {         // avoid 0x800
    val += 0x1360;
  } else if (idx > 0x6) {  // avoid 0x400
    val += 0x1340;
  } else {  // avoid lower
    val += 0x1320;
  }

  if (idx == 0x32) {  // mask ECC
    word &= 0xFFFFE000;
  }

  for (int i = 0; i < 32; i++) {
    if (word & 1) {
      ecc ^= val + i;
    }

    word >>= 1;
  }
  if (idx == 0x64) {  // last index
    uint32_t v = ecc & 0xFFF;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    ecc ^= (v & 1) << 12;  // parity
  }
  return ecc;
}
}  // namespace internal
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
