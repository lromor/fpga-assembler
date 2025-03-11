#include "fpga/memory-mapped-file.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

namespace fpga {
namespace {
class MemoryMappedFile final : public MemoryBlock {
 public:
  MemoryMappedFile(char *const data, const size_t size)
      : data_(data), size_(size) {}

  std::string_view AsStringView() const override { return {data_, size_}; }

  ~MemoryMappedFile() override { munmap(data_, size_); }

 private:
  char *const data_;
  const size_t size_;
};
}  // namespace

absl::StatusOr<std::unique_ptr<MemoryBlock>> MemoryMapFile(
  std::string_view path) {
  const int fd = open(std::string(path).c_str(), O_RDONLY);
  if (fd < 0) {
    return absl::Status(absl::ErrnoToStatus(
      errno, absl::StrFormat("could not open file: %s", path)));
  }
  struct stat s;
  fstat(fd, &s);
  const size_t file_size = s.st_size;
  // Memory map everything into a convenient contiguous buffer
  void *const buffer = mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  return std::make_unique<MemoryMappedFile>((char *)buffer, file_size);
}
}  // namespace fpga
