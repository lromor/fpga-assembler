#include "fpga/xilinx/arch-xc7-defs.h"

#include <ostream>

namespace fpga {
namespace xilinx {
namespace xc7 {
std::ostream &operator<<(std::ostream &o, const ConfigurationRegister &value) {
  switch (value) {
  case ConfigurationRegister::kCRC: return o << "CRC";
  case ConfigurationRegister::kFAR: return o << "Frame Address";
  case ConfigurationRegister::kFDRI: return o << "Frame Data Input";
  case ConfigurationRegister::kFDRO: return o << "Frame Data Output";
  case ConfigurationRegister::kCMD: return o << "Command";
  case ConfigurationRegister::kCTL0: return o << "Control 0";
  case ConfigurationRegister::kMASK: return o << "Mask for CTL0 and CTL1";
  case ConfigurationRegister::kSTAT: return o << "Status";
  case ConfigurationRegister::kLOUT: return o << "Legacy Output";
  case ConfigurationRegister::kCOR0: return o << "Configuration Option 0";
  case ConfigurationRegister::kMFWR: return o << "Multiple Frame Write";
  case ConfigurationRegister::kCBC: return o << "Initial CBC Value";
  case ConfigurationRegister::kIDCODE: return o << "Device ID";
  case ConfigurationRegister::kAXSS: return o << "User Access";
  case ConfigurationRegister::kCOR1: return o << "Configuration Option 1";
  case ConfigurationRegister::kWBSTAR: return o << "Warm Boot Start Address";
  case ConfigurationRegister::kTIMER: return o << "Watchdog Timer";
  case ConfigurationRegister::kBOOTSTS: return o << "Boot History Status";
  case ConfigurationRegister::kCTL1: return o << "Control 1";
  case ConfigurationRegister::kBSPI:
    return o << "BPI/SPI Configuration Options";
  default: return o << "Unknown";
  }
}

std::ostream &operator<<(std::ostream &o, BlockType value) {
  switch (value) {
  case BlockType::kCLBIOCLK: o << "CLB/IO/CLK"; break;
  case BlockType::kBlockRam: o << "Block RAM"; break;
  case BlockType::kCFGCLB: o << "Config CLB"; break;
  }
  return o;
}
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
