#ifndef FPGA_MEMORY_MAPPED_FILE_H
#define FPGA_MEMORY_MAPPED_FILE_H

#include <filesystem>
#include <string_view>

#include "absl/status/statusor.h"

namespace fpga {
// Taken from: verible/common/strings/mem-block.h
class MemoryBlock {
 public:
  virtual ~MemoryBlock() = default;
  virtual std::string_view AsStringView() const = 0;

 protected:
  MemoryBlock() = default;

 public:
  MemoryBlock(const MemoryBlock &) = delete;
  MemoryBlock(MemoryBlock &&) = delete;
  MemoryBlock &operator=(const MemoryBlock &) = delete;
  MemoryBlock &operator=(MemoryBlock &&) = delete;
};

absl::StatusOr<std::unique_ptr<MemoryBlock>> MemoryMapFile(
    std::string_view path);

inline absl::StatusOr<std::unique_ptr<MemoryBlock>> MemoryMapFile(
    const std::filesystem::path &path) {
  return MemoryMapFile(std::string_view(std::string(path)));
}
}  // namespace fpga
#endif  // FPGA_MEMORY_MAPPED_FILE_H
