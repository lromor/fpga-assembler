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
// GPIO-only adaptation of hzeller/rpi-rgb-led-matrix, retaining the register
// access method and dropping display timing, PWM, and model detection.
// Modified for fpga-assembler on 2026-11-01.
// https://github.com/hzeller/rpi-rgb-led-matrix/blob/3da7a3bdfca4637bb79ff038bc2bb26965197db5/lib/gpio.cc
// /dev/gpiomem maps the GPIO block directly on Raspberry Pi 1-4.
// See LICENSES/GPL-2.0-only.txt at the repository root for the original
// license.

#ifndef FPGA_ASSEMBLER_TESTS_HARDWARE_GPIO_H
#define FPGA_ASSEMBLER_TESTS_HARDWARE_GPIO_H

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>

namespace hardware {
class GPIO {
 public:
  GPIO() = default;
  GPIO(const GPIO &) = delete;
  GPIO &operator=(const GPIO &) = delete;
  ~GPIO() {
    if (registers_ != nullptr) {
      munmap(const_cast<uint32_t *>(registers_), 4096);
    }
  }

  bool Init() {
    const int fd = open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
      std::perror("Opening /dev/gpiomem");
      return false;
    }
    void *mapping =
      mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
      std::perror("Mapping GPIO registers");
      close(fd);
      return false;
    }
    close(fd);
    registers_ = static_cast<volatile uint32_t *>(mapping);
    return true;
  }

  // Call only after Init(). The mask uses BCM GPIO numbers 0-31.
  void SetDirection(uint32_t mask, bool output) {
    for (unsigned int pin = 0; pin < 32; ++pin) {
      if ((mask & (1u << pin)) == 0) continue;
      const unsigned int shift = (pin % 10) * 3;
      uint32_t value = registers_[pin / 10] & ~(7u << shift);
      if (output) value |= 1u << shift;
      registers_[pin / 10] = value;
    }
  }

  uint32_t Read() const { return registers_[0x34 / sizeof(uint32_t)]; }

  void WriteMaskedBits(uint32_t value, uint32_t mask) {
    registers_[0x28 / sizeof(uint32_t)] = ~value & mask;
    registers_[0x1c / sizeof(uint32_t)] = value & mask;
  }

 private:
  volatile uint32_t *registers_ = nullptr;
};
}  // namespace hardware
#endif
