#ifndef FPGA_DATABASE_H
#define FPGA_DATABASE_H

namespace fpga {
class FasmAssembler {
 public:
  FasmAssembler() {}

 private:
  std::filesystem::path prjxray_db_root_;
};
}  // namespace fpga

#endif  // FPGA_FASM_ASSEMBLER_H
