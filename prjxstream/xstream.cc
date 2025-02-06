#include <cstdio>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "prjxstream/fasm-parser.h"

static constexpr absl::string_view kDatabasePath =
  "/home/lromor/Projects/OpenSource/prjxstream";

static constexpr absl::string_view kPart = "xc7a35tcsg324-1";

int main(int argc, char *argv[]) {
  const auto usage = absl::StrCat("usage: ", argv[0],
                                  " [options] < input.fasm > output.frm\n"
                                  R"(
xstream parses a sequence of fasm lines and assembles them
into a set of frames to be mapped into bitstream.
Output is written to stdout.
)");
  if (argc > 2) {
    std::cerr << usage << std::endl;
  }

  // FILE *input_stream = stdin;
  // if (argc == 2) {
  //   const std::string_view arg(argv[1]);
  //   if (arg != "-") {
  //     input_stream = std::fopen(argv[1], "r");
  //   }
  // }

  // size_t buf_size = 8192;
  // char *buffer = (char *)malloc(buf_size);
  // while (getline(&buffer, &buf_size, input_stream)) {
  //   const std::string_view content(buffer, buf_size);
  //   fasm::Parse(
  //     content, stderr,
  //     [](uint32_t line, std::string_view feature_name, int start_bit, int
  //     width,
  //        uint64_t bits) -> bool { return true; },
  //     [](uint32_t, std::string_view, std::string_view name,
  //        std::string_view value) {});
  // }
  return 0;
}
