#include "prjxstream/memory-mapped-file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "absl/status/status.h"

namespace prjxstream {
namespace {
class MemoryMappedFile final : public MemoryBlock {
 public:
  MemoryMappedFile(char *const data, const size_t size) :
      data_(data), size_(size) {}

  absl::string_view AsStringVew() const override {
    return {data_, size_};
  }

  ~MemoryMappedFile() override { munmap(data_, size_); }
 private:
  char *const data_;
  const size_t size_;
};
}  // namespace

absl::StatusOr<std::unique_ptr<MemoryBlock>> MemoryMapFile(
    absl::string_view path) {
  const int fd = open(path.data(), O_RDONLY);
  if (fd < 0) {
    close(fd);
    return absl::Status(
        absl::ErrnoToStatus(
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
}  // namespace prjxstream
