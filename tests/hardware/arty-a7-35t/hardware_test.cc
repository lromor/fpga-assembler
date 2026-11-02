// SPDX-License-Identifier: GPL-2.0-only
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <thread>
#include <vector>

#include "tests/hardware/gpio.h"

struct ChannelTrace {
  const char *channel;
  unsigned int gpio;
  std::string_view wave;
};
using Traces = std::initializer_list<ChannelTrace>;
struct Test {
  std::string_view name;
  Traces inputs;
  Traces expect;
};
// Channel names are Arty Pmod pins; GPIO numbers are Pi chip line offsets.
// Pin mappings and sources: README.md#wiring-and-channel-names.
constexpr Test kTests[] = {
  {.name = "low", .inputs = {}, .expect = {{"JA1", 8, "_"}}},
  {.name = "high", .inputs = {}, .expect = {{"JA1", 8, "-"}}},
  {.name = "loopback",
   .inputs = {{"JA2", 10, "__--_-__"}},
   .expect = {{"JA1", 8, "__--_-__"}}},
};

static uint32_t Mask(Traces traces) {
  uint32_t mask = 0;
  for (const auto &trace : traces) mask |= 1u << trace.gpio;
  return mask;
}
static uint32_t Levels(Traces traces, size_t column) {
  uint32_t bits = 0;
  for (const auto &trace : traces) {
    if (trace.wave[column] == '-') bits |= 1u << trace.gpio;
  }
  return bits;
}

static void Print(Traces traces, const char *meaning) {
  for (const auto &trace : traces) {
    std::printf("%-4s %-8s %.*s\n", trace.channel, meaning,
                static_cast<int>(trace.wave.size()), trace.wave.data());
  }
}

static bool Run(const Test &test, const char *chip) {
  if (test.expect.size() == 0) {
    std::fputs("A test needs at least one expected channel.\n", stderr);
    return false;
  }
  const size_t steps = test.expect.begin()->wave.size();
  uint32_t used = 0;
  for (const Traces traces : {test.inputs, test.expect}) {
    for (const auto &trace : traces) {
      if (steps == 0 || trace.wave.size() != steps || trace.gpio >= 32 ||
          trace.wave.find_first_not_of("_-") != std::string_view::npos ||
          (used & (1u << trace.gpio)) != 0) {
        std::fputs(
          "Invalid channel trace: use unique GPIOs and equal-length _/- "
          "waves.\n",
          stderr);
        return false;
      }
      used |= 1u << trace.gpio;
    }
  }
  hardware::GPIO gpio;
  const uint32_t input_mask = Mask(test.inputs);
  const uint32_t expect_mask = Mask(test.expect);
  if (!gpio.Init(chip, used) ||
      !gpio.SetDirection(input_mask, true, Levels(test.inputs, 0))) {
    return false;
  }
  std::vector<uint32_t> captured;
  bool passed = true;
  for (size_t column = 0; column < steps; ++column) {
    if (!gpio.WriteMaskedBits(Levels(test.inputs, column), input_mask)) {
      return false;
    }
    const uint32_t expected = Levels(test.expect, column);
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
    uint32_t observed;
    do {
      if (!gpio.Read(observed)) return false;
      if ((observed & expect_mask) == expected) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    captured.push_back(observed);
    if ((observed & expect_mask) != expected) {
      passed = false;
      break;
    }
  }
  if (!gpio.SetDirection(input_mask, false)) return false;
  for (const auto &trace : test.expect) {
    std::printf("%-4s observed ", trace.channel);
    for (const uint32_t sample : captured) {
      std::putchar((sample & (1u << trace.gpio)) ? '-' : '_');
    }
    std::putchar('\n');
  }
  return passed;
}

int main(int argc, char **argv) {
  const bool list = argc == 2 && std::string_view(argv[1]) == "--list";
  const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
  if ((argc != 3 && !list) || help) {
    std::puts(
      "Usage: hardware_test low|high|loopback /dev/gpiochipN\n"
      "       hardware_test --list|--help\n"
      "Select the chip whose line offsets match the traces. Program the "
      "matching "
      "fixture first; each column allows 1 s for outputs to match.");
    return help ? 0 : 2;
  }
  for (const auto &test : kTests) {
    if (argv[1] != test.name && !list) continue;
    std::printf("%.*s\n", static_cast<int>(test.name.size()), test.name.data());
    Print(test.inputs, "input");
    Print(test.expect, "expect");
    if (list) continue;
    const bool passed = Run(test, argv[2]);
    std::puts(passed ? "[PASS]" : "[FAIL]");
    return passed ? 0 : 1;
  }
  if (list) return 0;
  std::fputs("Unknown test; use --list.\n", stderr);
  return 2;
}
