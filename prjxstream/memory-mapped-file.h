#ifndef PRJXSTREAM_MEM_BLOCK_H
#define PRJXSTREAM_MEM_BLOCK_H

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace prjxstream {
// Taken from: verible/common/strings/mem-block.h
class MemoryBlock {
 public:
  virtual ~MemoryBlock() = default;
  virtual absl::string_view AsStringVew() const = 0;

 protected:
  MemoryBlock() = default;

 public:
  MemoryBlock(const MemoryBlock &) = delete;
  MemoryBlock(MemoryBlock &&) = delete;
  MemoryBlock &operator=(const MemoryBlock &) = delete;
  MemoryBlock &operator=(MemoryBlock &&) = delete;
};

absl::StatusOr<std::unique_ptr<MemoryBlock>> MemoryMapFile(absl::string_view path);
}  // namespace prjxstream
#endif  // PRJXSTREAM_MEM_BLOCK_H
