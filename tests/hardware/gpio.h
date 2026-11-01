// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
// SPDX-License-Identifier: GPL-2.0-only
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://gnu.org/licenses/gpl-2.0.txt>.
//
// GPIO interface adapted from hzeller/rpi-rgb-led-matrix. The register
// backend has been replaced with Linux GPIO character-device API v2.
// Modified for fpga-assembler on 2026-11-01.
// https://github.com/hzeller/rpi-rgb-led-matrix/blob/3da7a3bdfca4637bb79ff038bc2bb26965197db5/lib/gpio.cc
// https://docs.kernel.org/userspace-api/gpio/chardev.html
// See LICENSES/GPL-2.0-only.txt at the repository root for the original
// license.

#ifndef FPGA_ASSEMBLER_TESTS_HARDWARE_GPIO_H
#define FPGA_ASSEMBLER_TESTS_HARDWARE_GPIO_H

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>

namespace hardware {
class GPIO {
 public:
  GPIO() = default;
  GPIO(const GPIO &) = delete;
  GPIO &operator=(const GPIO &) = delete;
  ~GPIO() {
    if (fd_ >= 0) {
      (void)SetDirection(claimed_, false);
      close(fd_);
    }
  }

  // Mask bit N selects line offset N (0-31) on this explicitly chosen chip.
  // Only these lines are requested, initially as inputs. Busy lines fail.
  [[nodiscard]] bool Init(const char *chip, uint32_t mask) {
    if (fd_ >= 0 || mask == 0) return Invalid("GPIO initialization");
    const int chip_fd = open(chip, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) return Check(-1, chip);
    request_ = {};
    for (unsigned int pin = 0; pin < 32; ++pin) {
      if ((mask & (1u << pin)) != 0) {
        request_.offsets[request_.num_lines++] = pin;
      }
    }
    std::snprintf(request_.consumer, sizeof(request_.consumer), "fpga-as-test");
    request_.config.flags = GPIO_V2_LINE_FLAG_INPUT;
    const bool ok = Check(ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request_),
                          "Requesting GPIO lines");
    if (!ok) {
      std::fprintf(stderr, "%s requested line offsets:", chip);
      for (unsigned int i = 0; i < request_.num_lines; ++i) {
        std::fprintf(stderr, " %u", request_.offsets[i]);
      }
      std::fputc('\n', stderr);
    }
    close(chip_fd);
    if (!ok) return false;
    fd_ = request_.fd;
    claimed_ = mask;
    return true;
  }

  // Output direction and initial levels are configured in one ioctl.
  [[nodiscard]] bool SetDirection(uint32_t mask, bool output,
                                  uint32_t initial = 0) {
    if (!Valid(mask, claimed_)) return false;
    if (mask == 0) return true;
    const uint32_t outputs = output ? outputs_ | mask : outputs_ & ~mask;
    const uint32_t levels = (levels_ & ~mask) | (initial & mask);
    gpio_v2_line_config config{};
    config.flags = GPIO_V2_LINE_FLAG_INPUT;
    if (outputs != 0) {
      config.num_attrs = 2;
      config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_FLAGS;
      config.attrs[0].attr.flags = GPIO_V2_LINE_FLAG_OUTPUT;
      config.attrs[0].mask = RequestBits(outputs);
      config.attrs[1].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
      config.attrs[1].attr.values = RequestBits(levels);
      config.attrs[1].mask = RequestBits(outputs);
    }
    if (!Check(ioctl(fd_, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config),
               "Configuring GPIO directions")) {
      return false;
    }
    outputs_ = outputs;
    levels_ = levels;
    return true;
  }

  [[nodiscard]] bool Read(uint32_t &value) const {
    if (!Valid(claimed_, claimed_)) return false;
    gpio_v2_line_values values{};
    values.mask = RequestBits(claimed_);
    if (!Check(ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values),
               "Reading GPIO lines")) {
      return false;
    }
    value = 0;
    for (unsigned int i = 0; i < request_.num_lines; ++i) {
      if ((values.bits & (1ull << i)) != 0) value |= 1u << request_.offsets[i];
    }
    return true;
  }

  [[nodiscard]] bool WriteMaskedBits(uint32_t value, uint32_t mask) {
    if (!Valid(mask, outputs_)) return false;
    if (mask == 0) return true;
    gpio_v2_line_values values{};
    values.mask = RequestBits(mask);
    values.bits = RequestBits(value & mask);
    if (!Check(ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &values),
               "Writing GPIO lines")) {
      return false;
    }
    levels_ = (levels_ & ~mask) | (value & mask);
    return true;
  }

 private:
  static bool Check(int result, const char *operation) {
    if (result >= 0) return true;
    std::perror(operation);
    return false;
  }
  static bool Invalid(const char *operation) {
    errno = EINVAL;
    return Check(-1, operation);
  }
  bool Valid(uint32_t mask, uint32_t allowed) const {
    if (fd_ >= 0 && (mask & ~allowed) == 0) return true;
    return Invalid("GPIO mask or direction");
  }
  // Kernel bitmaps index request.offsets[], not chip line offsets.
  uint64_t RequestBits(uint32_t mask) const {
    uint64_t bits = 0;
    for (unsigned int i = 0; i < request_.num_lines; ++i) {
      if ((mask & (1u << request_.offsets[i])) != 0) bits |= 1ull << i;
    }
    return bits;
  }
  gpio_v2_line_request request_{};
  int fd_ = -1;
  uint32_t claimed_ = 0;
  uint32_t outputs_ = 0;
  uint32_t levels_ = 0;
};
}  // namespace hardware
#endif
