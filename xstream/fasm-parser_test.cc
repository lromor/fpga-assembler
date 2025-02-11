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

#include "xstream/fasm-parser.h"

#include <iostream>
#include <string_view>

using fasm::ParseResult;

// Make ParseResult printable so that we can use it in test outputs.
std::ostream &operator<<(std::ostream &o, fasm::ParseResult r) {
  switch (r) {
  case fasm::ParseResult::kSuccess: return o << "Success";
  case fasm::ParseResult::kInfo: return o << "Info";
  case fasm::ParseResult::kNonCritical: return o << "NonCritical";
  case fasm::ParseResult::kSkipped: return o << "Skipped";
  case fasm::ParseResult::kUserAbort: return o << "UserAbort";
  case fasm::ParseResult::kError: return o << "Error";
  }
  return o;
}

static int expect_mismatch_count = 0;
#define EXPECT_EQ(a, b)                                                      \
  if ((a) == (b)) {                                                          \
  } else                                                                     \
    (++expect_mismatch_count, std::cerr)                                     \
      << __LINE__ << ": EXPECT FAIL (" << #a << " == " << #b << ") (" << (a) \
      << " vs. " << (b) << ") "

struct ValueTestCase {
  std::string_view input;
  // Expected outputs
  fasm::ParseResult result;
  std::string_view feature_name;
  int min_bit;
  int width;
  uint64_t bits;
};

void ValueParseTest() {
  std::cout << "\n-- Value parse test -- \n";
  constexpr ValueTestCase tests[] = {
    // Names
    {"DOTS.IN.FEATURE", ParseResult::kSuccess, "DOTS.IN.FEATURE", 0, 1, 1},
    {"D_1_G1TS", ParseResult::kSuccess, "D_1_G1TS", 0, 1, 1},
    {"   \tINDENTED # foo", ParseResult::kSuccess, "INDENTED", 0, 1, 1},

    // We don't validate if the start of an identifier is actually in the
    // allowed set that won't include digits.
    // The receiver will verify if the feature exists. So 'Success' it is.
    {"0valid", ParseResult::kSuccess, "0valid", 0, 1, 1},
    {"[8:0]", ParseResult::kError, "", 0, 0, 0},  // Range without feature.

    // Empty lines and comments
    {"", ParseResult::kSuccess, "", 0, 0, 0},           // Callback never called
    {" # hello ", ParseResult::kSuccess, "", 0, 0, 0},  // dito
    {"COMMENT # more stuff", ParseResult::kSuccess, "COMMENT", 0, 1, 1},
    {"COMMENT[3:0] = 12 # ok", ParseResult::kSuccess, "COMMENT", 0, 4, 12},

    // Implicit set with no assign; explicit zero assign.
    {"IMPLICIT_ONE", ParseResult::kSuccess, "IMPLICIT_ONE", 0, 1, 1},
    {"EXPLICIT_ZERO = 0", ParseResult::kSuccess, "EXPLICIT_ZERO", 0, 1, 0},

    // An equal assignment without any value followed is interpreted as
    // zero. Maybe too lenient, so maybe should be kError ?
    {"IMPLICIT_ZERO[8:0] =  # no value assigned", ParseResult::kSuccess,
     "IMPLICIT_ZERO", 0, 9, 0},

    // Parsing numbers with included underscores
    {"UNDERSCORE_BITPOS[ _8_ ]", ParseResult::kSuccess,  //
     "UNDERSCORE_BITPOS", 8, 1, 1},
    {"UNDERSCORE_DECIMAL[15:0] = 1_234", ParseResult::kSuccess,  //
     "UNDERSCORE_DECIMAL", 0, 16, 1234},
    {"UNDERSCORE_HEXVALUE[15:0] = 'hAB_CD", ParseResult::kSuccess,  //
     "UNDERSCORE_HEXVALUE", 0, 16, 0xabcd},

    // Decimal, hex, binary and octal
    {"ASSIGN_DECIMAL[3:0] = 5", ParseResult::kSuccess,  //
     "ASSIGN_DECIMAL", 0, 4, 5},
    {"ASSIGN_DECIMAL[3:0] = 4'd5", ParseResult::kSuccess,  //
     "ASSIGN_DECIMAL", 0, 4, 5},
    // Invalid digit at end.
    {"ASSIGN_BROKEN_DEC[7:0] = 4'd5a", ParseResult::kError,  //
     "ASSIGN_BROKEN_DEC", 0, 8, 5},

    {"ASSIGN_HEX1[15:0] = 16'hCa_Fe", ParseResult::kSuccess,  //
     "ASSIGN_HEX1", 0, 16, 0xcafe},
    {"ASSIGN_HEX2[31:0] = 32'h_dead_beef", ParseResult::kSuccess,  //
     "ASSIGN_HEX2", 0, 32, 0xdeadbeef},
    {"ASSIGN_HEX3[31:0] = 32 ' h _dead_beef ", ParseResult::kSuccess,  //
     "ASSIGN_HEX3", 0, 32, 0xdeadbeef},

    {"BINARY[63:48] = 16'b1111_0000_1111_0000", ParseResult::kSuccess, "BINARY",
     48, 16, 0xF0F0},
    {"ASSIGN_OCT[8:0] = 9'o644", ParseResult::kSuccess,  //
     "ASSIGN_OCT", 0, 9, 0644},
    {"UNKNOWN_BASE[7:0] = 8'y123", ParseResult::kError,  //
     "UNKNOWN_BASE", 0, 8, 1},  // fallback to default on bit.

    // Unannounced hex value.
    {"ASSIGN_INVALID[8:0] = beef # hex not expected", ParseResult::kError,
     "ASSIGN_INVALID", 0, 9, 0},
    {"ASSIGN_INVALID[8:0] = 5beef # starts valid dec", ParseResult::kError,
     "ASSIGN_INVALID", 0, 9, 5},

    // Error: inverted ranges or plain old parse errors.
    {"INVERTED_RANGE[0:8]", ParseResult::kSkipped,   //
     "", 0, 0, 0},                                   // Callback never called
    {"BRACKET_MISSING[4:0xyz", ParseResult::kError,  //
     "", 0, 0, 0},                                   // Callback never called

    // Numbers longer than 64 bit can not be dealt with, only best effort
    // parse
    {"VERY_LONG_NOT_SUPPORTED[255:0] = 256'h1", ParseResult::kError,
     "VERY_LONG_NOT_SUPPORTED", 0, 64, 1},  // Short enough to parse complete
    {"BEST_EFFORT[127:0] = 128'hdeadbeef_deadbeef_c0feface_1337f00d",
     ParseResult::kError,                        //
     "BEST_EFFORT", 0, 64, 0xc0feface1337f00d},  // Truncated

    // Examples from README.
    {"FOO[255:192] = 42", ParseResult::kSuccess, "FOO", 192, 64, 42},
    {"BAR[255:0] = 42", ParseResult::kError, "BAR", 0, 64, 42},

    // Attempt to assign too wide number; warn but comes back properly shaved
    {"ASSIGN_HEX[15:0] = 32'hcafebabe", ParseResult::kNonCritical, "ASSIGN_HEX",
     0, 16, 0xbabe},
    {"ASSIGN_DECIMAL[3:0] = 255", ParseResult::kSuccess,  //
     "ASSIGN_DECIMAL", 0, 4, 0x0F},                       // Shaved down

    // Annotations are acknowledged, but ignored.
    // Global annotation, no feature. Callback never called.
    {"{.global = \"annotation\"}", ParseResult::kSuccess, "", 0, 0, 0},

    // Even though annotations are ignored, the values are still parsed.
    {"HELLO {.foo = \"bar\"}", ParseResult::kSuccess, "HELLO", 0, 1, 1},
    {"HELLO[5:0] = 42{.foo = \"bar\"}", ParseResult::kSuccess,  //
     "HELLO", 0, 6, 42},
    {"EXPLICIT_ZERO = 0 {.foo = \"bar\"}", ParseResult::kSuccess,
     "EXPLICIT_ZERO", 0, 1, 0},
  };

  for (const ValueTestCase &expected : tests) {
    for (const char *line_ending : {"\n", "\r\n"}) {
      const std::string line = std::string(expected.input);
      const std::string input = line + line_ending;
      bool was_called = false;
      auto result = fasm::Parse(
        input, stderr,
        [&](uint32_t, std::string_view n, int min_bit, int width,
            uint64_t bits) {
          was_called = true;
          EXPECT_EQ(n, expected.feature_name) << expected.input << "\n";
          EXPECT_EQ(min_bit, expected.min_bit) << expected.input << "\n";
          EXPECT_EQ(width, expected.width) << expected.input << "\n";
          EXPECT_EQ(bits, expected.bits) << expected.input << "\n";
          return true;
        });

      EXPECT_EQ(result, expected.result) << expected.input << "\n";
      // If the expected the callback to be called, the expect data will have
      // a width != 0.
      EXPECT_EQ(was_called, (expected.width != 0)) << expected.input << "\n";
    }
  }
}

struct AnnotationTestCase {
  std::string_view input;
  fasm::ParseResult result;
  // Expected outputs
  std::vector<std::pair<std::string_view, std::string_view>> annotations;
};
void AnnotationParseTest() {
  std::cout << "\n-- Annotation parse test -- \n";
  const AnnotationTestCase tests[] = {
    // Simple, multi name=value pair
    {"{ foo = \"bar\", baz = \"quux\" }\n",
     ParseResult::kSuccess,
     {{"foo", "bar"}, {"baz", "quux"}}},

    {"SOME_FEATURE = 42 { foo = \"bar\", baz = \"quux\" }\n",
     ParseResult::kSuccess,
     {{"foo", "bar"}, {"baz", "quux"}}},

    // Value with backslash-escaped quote
    {"{ .escaped = \"Some quote with \\\"quote\\\"\" }\n",
     ParseResult::kSuccess,
     {{".escaped", "Some quote with \\\"quote\\\""}}},

    // Error: String quote missing around value
    {"{ foo = \"bar\", baz = quux\" }\n",
     ParseResult::kError,
     {{"foo", "bar"}}},

    // Error: Semicolon instead of comma.
    {"{ foo = \"bar\"; baz = \"quux\" }\n",
     ParseResult::kError,
     {{"foo", "bar"}}},

    // Error: String does not end - failed to find " at end of line
    {"{ unterminated = \"string }\nNEXT_LINE\n", ParseResult::kError, {}},

    {"{ line_continuation_is_error = \"string\\\nNEXT_LINE\"\n",
     ParseResult::kError,
     {}},
  };

  for (const AnnotationTestCase &expected : tests) {
    auto annotation_pos = expected.annotations.begin();
    auto result = fasm::Parse(
      expected.input, stderr,
      [&](uint32_t, std::string_view feature_name, int, int, uint64_t) {
        // Global annotations don't have a feature associated with it. This
        // callback should only be called if there is a feature.
        EXPECT_EQ(feature_name.empty(), false) << expected.input;
        return true;
      },
      [&](uint32_t, std::string_view,  //
          std::string_view name, std::string_view value) {
        EXPECT_EQ(annotation_pos == expected.annotations.end(), false)
          << expected.input;
        EXPECT_EQ(annotation_pos->first, name) << expected.input;
        EXPECT_EQ(annotation_pos->second, value) << expected.input;
        ++annotation_pos;
        std::cout << name << " = " << value << "\n";
      });
    EXPECT_EQ(annotation_pos == expected.annotations.end(), true)
      << expected.input;

    EXPECT_EQ(result, expected.result) << expected.input;
  }
}

int main() {
  ValueParseTest();
  AnnotationParseTest();

  if (expect_mismatch_count == 0) {
    printf("\nPASS, all expectations met.\n");
  } else {
    printf("\nFAIL, %d expectations **not** met.\n", expect_mismatch_count);
  }

  return expect_mismatch_count;
}
