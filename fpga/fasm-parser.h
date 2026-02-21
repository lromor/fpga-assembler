// Copyright 2022 Henner Zeller <h.zeller@acm.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Simple single-header parser for FPGA assembly file format.

#ifndef FPGA_FASM_PARSER_H
#define FPGA_FASM_PARSER_H

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string_view>

namespace fasm {
// Parse callback for FASM lines. The "feature" found in line number "line"
// is set the values given in "bits", starting from lowest "start_bit" (lsb)
// with given "width".
// Returns 'true' if it wants to continue get callbacks or 'false' if it
// wants the parsing to abort.
using ParseCallback =
  std::function<bool(uint32_t line, std::string_view feature, int start_bit,
                     int width, uint64_t bits)>;

// Optional callback that receives annotation name/value pairs. If there are
// multiple annotations per feature, this is called multiple times.
using AnnotationCallback =
  std::function<void(uint32_t line, std::string_view feature,  //
                     std::string_view name, std::string_view value)>;

// Result values in increasing amount of severity. Start to worry at kSkipped.
enum class ParseResult {
  kSuccess,      // Successful parse
  kInfo,         // Got info messages, mostly FYI
  kNonCritical,  // Found strange values, but mostly non-critical FYI
  kSkipped,      // There were lines that had to be skipped.
  kUserAbort,    // The callback returned 'false' to abort.
  kError         // Errornous input
};

// Parse FPGA assembly file, send parsed values to "parse_callback".
// The "content" is the buffer to parse; last line needs to end with a newline.
// Errors/Warnings are reported to "errstream".
//
// If the optional "annotation_callback" is provided, it receives annotations
// in {...} blocks. Quotes around value is removed, escaped characters are
// preserved.
//
// The "feature" string_view as well as "name" and "value" for the callbacks
// are guranteed to not be ephemeral but backed by the original content,
// so it is valid for the lifetime of "content".
//
// If there are warnings or errors, parsing will continue if possible.
// The most severe issue found is returned.
//
// Spec: https://fasm.readthedocs.io/en/latest/specification/syntax.html
inline ParseResult Parse(std::string_view content, FILE *errstream,
                         const ParseCallback &parse_callback,
                         const AnnotationCallback &annotation_callback = {});

// -- End of API interface; rest is implementation details

namespace internal {
// This look-up table maps ASCII characters to its integer value if it is a
// digit; anything outside the range of a valid digit stops number parsing.
//
// To parse numbers, we need to allow for 'underscore' being part of the
// number as readability digit separator e.g. 32'h_dead_beef (Verilog numbers).
//
// -1    : digit separator ('_') -> ignore, but continue reading number
// 0..15 : valid digit (can be used for conversions of bases 2..16)
// > 15  : not a valid digit, number parsing is finished.
//
// The separator being less than 0 allows a single comparison to decide if we
// are in valid number territory (< base)
// Since we need a number smaller than the smallest digit, need signed values.
inline constexpr int8_t kDigitSeparator = -1;  // _less_ than any digit.
inline constexpr int8_t kDigitToInt[256] = {
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  00, 1,  2,  3,  4,  5,  6,  7,  8,  9,  99, 99, 99, 99, 99, 99,
  99, 10, 11, 12, 13, 14, 15, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, kDigitSeparator,
  99, 10, 11, 12, 13, 14, 15, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

// ASCII -> is valid identifier for the feature name.
inline constexpr char kValidIdentifier[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // dot
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,  // digits
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // LETTERS
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // LETTERS, ... underscore
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // letters
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //
};

using bit_range_t = uint16_t;  // gcc slightly faster with 16 bit
}  // namespace internal

// [[unlikely]] only available since c++20, so use gcc/clang builtin here.
#define fasm_unlikely(x) __builtin_expect((x), 0)

// Skip until we hit the first non-blank char (EOL '\n' not considered blank)
#define fasm_skip_blank() \
  while (*it == ' ' || *it == '\t') ++it

// Skip forward until we sit on the '\n' end of current line.
#define fasm_skip_to_eol() \
  while (*it != '\n') ++it

// Skip forward beyond the end of current line. To be used before 'continue'.
#define fasm_skip_to_start_of_next_line() \
  fasm_skip_to_eol();                     \
  ++it

// Parse number with given base (any base between 2 and 16 is supported)
#define fasm_parse_number_with_base(v, base)                               \
  fasm_skip_blank();                                                       \
  for (int8_t d; (d = internal::kDigitToInt[(uint8_t)*it]) < (base); ++it) \
    if (d == internal::kDigitSeparator) {                                  \
    } else                                                                 \
      (v) = (v) * (base) + d

// Parse big number with given power-of-2 base
#define fasm_parse_long_number_with_base(v, l2base)                                   \
  fasm_skip_blank();                                                                  \
  {                                                                                   \
    unsigned n = 0;                                                                   \
    for (int8_t d; (d = internal::kDigitToInt[(uint8_t) * it]) < (1 << l2base); ++it) \
      if (d == internal::kDigitSeparator) {                                           \
      } else {                                                                        \
        if ((n % 64) == 0) v.emplace_back();                                          \
        (v).back() = (v).back() * (1 << l2base) + d;                                  \
        n += l2base;                                                                  \
      }                                                                               \
  }                                                                                   \
  std::reverse(v.begin(), v.end());


inline ParseResult Parse(std::string_view content, FILE *errstream,
                         const ParseCallback &parse_callback,
                         const AnnotationCallback &annotation_callback) {
  if (content.empty()) {
    return ParseResult::kSuccess;
  }
  if (content[content.size() - 1] != '\n') {
    // We need '\n' as sentinel, so without it, we'd run past the buffer.
    fprintf(errstream, "content does not end with a newline\n");
    return ParseResult::kError;
  }

  ParseResult result = ParseResult::kSuccess;
  const char *it = content.data();
  const char *const end = content.data() + content.size();
  uint32_t line_number = 0;
  while (it < end) {
    ++line_number;
    fasm_skip_blank();
    // Read feature name; look for sequence of valid characters.
    // We are a bit lenient if it starts with a non-alphanumeric character
    // (dot, digit, or underscore) which is entirely sufficient for the parsing
    // part. The receiver of the feature name will notice semantic issues.
    const char *const start_feature = it;
    while (internal::kValidIdentifier[(uint8_t)*it]) {
      ++it;
    }
    const std::string_view feature{start_feature, size_t(it - start_feature)};
    fasm_skip_blank();

    if (!feature.empty()) {
      // Read optional feature address and determine width. feature[<max>:<min>]
      internal::bit_range_t max_bit = 0;
      internal::bit_range_t min_bit = 0;
      if (*it == '[') {
        ++it;  // skip '['
        fasm_parse_number_with_base(max_bit, 10);
        fasm_skip_blank();
        if (*it == ':') {
          ++it;  // skip ':'
          fasm_parse_number_with_base(min_bit, 10);
          fasm_skip_blank();
        } else {
          min_bit = max_bit;
        }
        if (fasm_unlikely(*it != ']')) {
          fprintf(errstream, "%u: ERR expected ']' : '%.*s'\n", line_number,
                  int(it + 1 - start_feature), start_feature);
          result = ParseResult::kError;
          fasm_skip_to_start_of_next_line();
          continue;
        }
        ++it;  // skip ']'
        if (fasm_unlikely(max_bit < min_bit)) {
          fprintf(errstream, "%u: SKIP inverted range %.*s[%d:%d]\n",
                  line_number, (int)feature.size(), feature.data(), max_bit,
                  min_bit);
          result = std::max(result, ParseResult::kSkipped);
          fasm_skip_to_start_of_next_line();
          continue;
        }
      }
      fasm_skip_blank();

      uint32_t width = (max_bit - min_bit + 1);

      std::vector<uint64_t> bitset;

      // Assignment.
      if (*it == '=') {
        ++it;  // skip '='
        fasm_skip_blank();
        uint64_t value_or_width = 0;
        if (internal::kDigitToInt[(uint8_t)*it] <= 9) {
          bitset.emplace_back();
          fasm_parse_number_with_base(value_or_width, 10);  // width or decimal value
        }
        fasm_skip_blank();
        if (*it == '\'') {
          ++it;  // skip tick
          fasm_skip_blank();
          // Last number was actually precision. Simple plausibility, but
          // ignore.
          if (fasm_unlikely(value_or_width > width)) {
            fprintf(errstream,
                    "%u: WARN Attempt to assign more bits (%" PRIu64
                    "') for "
                    "%.*s[%d:%d] with supported bit width of %u\n",
                    line_number, value_or_width, (int)feature.size(), feature.data(),
                    max_bit, min_bit, width);
            result = std::max(result, ParseResult::kNonCritical);
          }
          const char format_type = *it;
          ++it;

          if (fasm_unlikely(width > 64) && (format_type == 'd' || format_type == 'o')) {
            // TODO: if this is needed in practice, then parse in multiple
            // steps and call back multiple times with parts of the number.
            fprintf(errstream,
                    "%u: ERR: Sorry, can only deal with ranges <= 64 bit currently "
                    "%.*s[%d:%d]; trimming width %u to 64\n",
                    line_number, (int)feature.size(), feature.data(), max_bit,
                    min_bit, width);
            result = ParseResult::kError;
            width = 64;  // Clamp number of bits we report.
            // Move foward, doing best effort parsing of lower 64 bits.
          }

          switch (format_type) {
          case 'h': fasm_parse_long_number_with_base(bitset, 4); break;
          case 'b': fasm_parse_long_number_with_base(bitset, 1); break;
          case 'o': bitset.emplace_back(); fasm_parse_number_with_base(bitset[0], 8); break;
          case 'd': bitset.emplace_back(); fasm_parse_number_with_base(bitset[0], 10); break;
          default:
            fprintf(errstream,
                    "%u: unknown base signifier '%c'; expected "
                    "one of b, d, h, o\n",
                    line_number, format_type);
            result = ParseResult::kError;
            fasm_skip_to_eol();
            bitset.push_back(0x01);  // In error state now, but report this feature as set
            break;
          }
          fasm_skip_blank();
        } else {
          bitset.push_back(value_or_width);
        }
      } else {
        bitset.push_back(0x1);  // No assignment: default assumption 1 bit set.
        if (fasm_unlikely(min_bit != max_bit)) {
          fprintf(errstream,
                  "%u: INFO Range of bits %.*s[%d:%d], but no assignment\n",
                  line_number, (int)feature.size(), feature.data(), max_bit,
                  min_bit);
          result = std::max(result, ParseResult::kInfo);
        }
      }

      // Ready to report the feature and their bits.
      for (unsigned chunk = 0; chunk < bitset.size(); chunk++) {
        auto value = bitset.at(chunk);
        unsigned value_width = 64;
        if (chunk == bitset.size()) {
          value_width = unsigned(width - 64 * (bitset.size() - 1));
        }
        value &= uint64_t(-1) >> (64 - value_width);  // Clamp bits if value too wide
        if (fasm_unlikely(
              !parse_callback(line_number, feature, min_bit + chunk * 64, value_width, value))) {
          result = std::max(result, ParseResult::kUserAbort);
          break;
        }
      }
    }  // non-empty feature

    // Annotations might follow
    if (fasm_unlikely(*it == '{')) {
      if (annotation_callback) {
        do {
          ++it;  // skip '{' or ','
          fasm_skip_blank();
          const char *const start_name = it;
          while (internal::kValidIdentifier[(uint8_t)*it]) {
            ++it;
          }
          const std::string_view aname{start_name, size_t(it - start_name)};

          fasm_skip_blank();
          if (fasm_unlikely(*it != '=')) {
            fprintf(errstream, "%d: annotation %.*s: expected '='\n",
                    line_number, (int)aname.size(), aname.data());
            result = ParseResult::kError;
            break;
          }
          ++it;  // skip '='

          fasm_skip_blank();
          if (fasm_unlikely(*it != '"')) {
            fprintf(errstream,
                    "%d: %.*s : annotation '%.*s': value not quoted\n",
                    line_number, (int)feature.size(), feature.data(),
                    (int)aname.size(), aname.data());
            result = ParseResult::kError;
            break;
          }

          const char *const start_value = it + 1;
          do {
            ++it;
            while (*it != '"' && *it != '\n') {
              ++it;
            }
          } while (*(it - 1) == '\\' && *it != '\n');  // quote was escaped
          const std::string_view avalue{start_value, size_t(it - start_value)};

          if (fasm_unlikely(*it == '\n')) {
            fprintf(errstream,
                    "%d: annotation not finished before end of line\n",
                    line_number);
            result = ParseResult::kError;
            break;
          }
          annotation_callback(line_number, feature, aname, avalue);
          ++it;  // skip '"'

          fasm_skip_blank();
        } while (*it == ',');

        if (*it != '}') {
          fprintf(errstream, "%d: annotations: expected ',' or '}'; got '%c'\n",
                  line_number, *it);
          result = ParseResult::kError;
        }
      }

      fasm_skip_to_eol();
    }

    if (*it == '#' || *it == '\r') {
      fasm_skip_to_eol();
    }

    if (fasm_unlikely(*it != '\n')) {
      fprintf(errstream, "%d: expected newline, got '%c'\n", line_number, *it);
      result = ParseResult::kError;
      fasm_skip_to_eol();
    }
    ++it;  // Consume \n and get ready for next line and position there.
  }
  return result;
}

#undef fasm_parse_number_with_base
#undef fasm_skip_to_start_of_next_line
#undef fasm_skip_to_eol
#undef fasm_skip_blank
#undef fasm_unlikely

}  // namespace fasm
#endif  // FPGA_FASM_PARSER_H
