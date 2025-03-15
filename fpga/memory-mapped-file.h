#ifndef FPGA_MEMORY_MAPPED_FILE_H
#define FPGA_MEMORY_MAPPED_FILE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace fpga {
// Taken from: verible/common/strings/mem-block.h
class MemoryBlock {
 public:
  virtual ~MemoryBlock() = default;
  virtual std::string_view AsStringView() const = 0;
  absl::Span<const uint8_t> AsBytesView() const {
    std::string_view const bytes = AsStringView();
    return {reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()};
  }

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
