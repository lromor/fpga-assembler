/*
 * Copyright (C) 2017-2020  The Project X-Ray Authors.
 *
 * Use of this source code is governed by a ISC-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/ISC
 *
 * SPDX-License-Identifier: ISC
 */
/*
 * Takes in a collection of ConfigurationPacket and writes them to specified
 * file This includes the following: -Bus auto detection -Sync Word -FPGA
 * configuration
 */
#ifndef FPGA_XILINX_BITSTREAM_WRITER_H
#define FPGA_XILINX_BITSTREAM_WRITER_H
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "fpga/xilinx/arch-types.h"
#include "fpga/xilinx/arch-xc7-configuration-packet.h"

namespace fpga {
namespace xilinx {
uint32_t PacketHeader(const xc7::ConfigurationPacket &packet);

// Writes out the complete Xilinx bitstream including
// header, sync word and configuration sequence.
template <Architecture Arch>
class BitstreamWriter {
 private:
  using ArchType = ArchitectureType<Arch>;
  using FrameWords = ArchType::FrameWords;
  using FrameAddress = ArchType::FrameAddress;
  using ConfigurationPacket = ArchType::ConfigurationPacket;
  using ConfigurationPackage = ArchType::ConfigurationPackage;
  using ConfigurationRegister = ArchType::ConfigurationRegister;

 public:
  using header_t = std::vector<uint32_t>;
  using packets_t = std::vector<std::unique_ptr<ConfigurationPacket>>;
  using BitstreamHeader = std::vector<uint8_t>;
  // Only defined if a packet exists
  using op_data_t = absl::optional<absl::Span<const uint32_t>>;
  using data_iterator_t = absl::Span<const uint32_t>::iterator;
  using itr_value_type = uint32_t;

  class packet_iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = BitstreamWriter::itr_value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    packet_iterator &operator++();

    bool operator==(const packet_iterator &other) const;
    bool operator!=(const packet_iterator &other) const;

    itr_value_type operator*() const;
    itr_value_type operator->() const;

    using state_t = enum {
      STATE_HEADER = 1,
      STATE_DATA = 2,
      STATE_END = 3,
    };

   protected:
    explicit packet_iterator(const ConfigurationPacket *packet, state_t state,
                             data_iterator_t itr_data);

   private:
    friend class iterator;
    friend BitstreamWriter;

    // Data iterators
    // First over the fixed header, then the configuration data
    state_t state_;
    // Over packet.data()
    data_iterator_t itr_data_;

    const ConfigurationPacket *packet_;
  };

  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = BitstreamWriter::itr_value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    iterator &operator++();

    bool operator==(const iterator &other) const;
    bool operator!=(const iterator &other) const;

    itr_value_type operator*() const;
    itr_value_type operator->() const;

    packet_iterator packet_begin();
    packet_iterator packet_end();

   protected:
    explicit iterator(header_t::iterator itr_header, const packets_t &packets,
                      typename packets_t::const_iterator itr_packets,
                      absl::optional<packet_iterator> op_itr_packet);

   private:
    friend BitstreamWriter;
    // Data iterators
    // First over the fixed header, then the configuration data
    header_t::iterator itr_header_;
    const packets_t &packets_;
    typename packets_t::const_iterator itr_packets_;
    absl::optional<packet_iterator> op_itr_packet_;
  };

  explicit BitstreamWriter(const packets_t &packets) : packets_(packets) {}

  // Writes out the complete bitstream for Xilinx FPGA based on
  // the Configuration Package which holds the complete programming
  // sequence.
  int writeBitstream(const typename ArchType::ConfigurationPackage &packets,
                     const std::string &part_name,
                     const std::string &frames_file,
                     const std::string &generator_name,
                     const std::string &output_file);
  iterator begin();
  iterator end();

 private:
  static header_t header_;
  const packets_t &packets_;

  // Creates a Xilinx bit header which is mostly a
  // Tag-Length-Value(TLV) format documented here:
  // http://www.fpga-faq.com/FAQ_Pages/0026_Tell_me_about_bit_files.htm
  BitstreamHeader create_header(const std::string &part_name,
                                const std::string &frames_file_name,
                                const std::string &generator_name);
};

template <Architecture Arch>
int BitstreamWriter<Arch>::writeBitstream(
  const typename ArchType::ConfigurationPackage &packets,
  const std::string &part_name, const std::string &frames_file,
  const std::string &generator_name, const std::string &output_file) {
  std::ofstream out_file(output_file, std::ofstream::binary);
  if (!out_file) {
    std::cerr << "Unable to open file for writting: " << output_file << '\n';
    return 1;
  }

  BitstreamHeader bit_header(
    create_header(part_name, frames_file, generator_name));
  out_file.write(reinterpret_cast<const char *>(bit_header.data()),
                 bit_header.size());

  auto end_of_header_pos = out_file.tellp();
  auto header_data_length_pos =
    end_of_header_pos - static_cast<std::ofstream::off_type>(4);

  BitstreamWriter<Arch> out_bitstream_writer(packets);
  int bytes_per_word = sizeof(typename ArchType::WordType);
  for (uint32_t word : out_bitstream_writer) {
    for (int byte = bytes_per_word - 1; byte >= 0; byte--) {
      out_file.put((word >> (byte * 8)) & 0xFF);
    }
  }

  uint32_t const length_of_data = out_file.tellp() - end_of_header_pos;

  out_file.seekp(header_data_length_pos);
  for (int byte = 3; byte >= 0; byte--) {
    out_file.put((length_of_data >> (byte * 8)) & 0xFF);
  }
  return 0;
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::BitstreamHeader
BitstreamWriter<Arch>::create_header(const std::string &part_name,
                                     const std::string &frames_file_name,
                                     const std::string &generator_name) {
  // Sync header
  BitstreamHeader bit_header{0x0,  0x9,  0x0f, 0xf0, 0x0f, 0xf0, 0x0f,
                             0xf0, 0x0f, 0xf0, 0x00, 0x00, 0x01, 'a'};
  auto build_source =
    absl::StrCat(frames_file_name, ";Generator=" + generator_name);
  bit_header.push_back(static_cast<uint8_t>((build_source.size() + 1) >> 8));
  bit_header.push_back(static_cast<uint8_t>(build_source.size() + 1));
  bit_header.insert(bit_header.end(), build_source.begin(), build_source.end());
  bit_header.push_back(0x0);

  // Source file.
  bit_header.push_back('b');
  bit_header.push_back(static_cast<uint8_t>((part_name.size() + 1) >> 8));
  bit_header.push_back(static_cast<uint8_t>(part_name.size() + 1));
  bit_header.insert(bit_header.end(), part_name.begin(), part_name.end());
  bit_header.push_back(0x0);

  // Build timestamp.
  auto build_time = absl::Now();
  auto build_date_string =
    absl::FormatTime("%E4Y/%m/%d", build_time, absl::UTCTimeZone());
  auto build_time_string =
    absl::FormatTime("%H:%M:%S", build_time, absl::UTCTimeZone());

  bit_header.push_back('c');
  bit_header.push_back(
    static_cast<uint8_t>((build_date_string.size() + 1) >> 8));
  bit_header.push_back(static_cast<uint8_t>(build_date_string.size() + 1));
  bit_header.insert(bit_header.end(), build_date_string.begin(),
                    build_date_string.end());
  bit_header.push_back(0x0);

  bit_header.push_back('d');
  bit_header.push_back(
    static_cast<uint8_t>((build_time_string.size() + 1) >> 8));
  bit_header.push_back(static_cast<uint8_t>(build_time_string.size() + 1));
  bit_header.insert(bit_header.end(), build_time_string.begin(),
                    build_time_string.end());
  bit_header.push_back(0x0);
  bit_header.insert(bit_header.end(), {'e', 0x0, 0x0, 0x0, 0x0});
  return bit_header;
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::packet_iterator
BitstreamWriter<Arch>::iterator::packet_begin() {
  // itr_packets = packets.begin();
  const ConfigurationPacket &packet = **itr_packets_;

  return BitstreamWriter::packet_iterator(
    &packet, BitstreamWriter::packet_iterator::STATE_HEADER,
    packet.data().begin());
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::packet_iterator
BitstreamWriter<Arch>::iterator::packet_end() {
  const ConfigurationPacket &packet = **itr_packets_;

  return BitstreamWriter<Arch>::packet_iterator(
    &packet, BitstreamWriter::packet_iterator::STATE_END,
    // Essentially ignored
    packet.data().end());
}

template <Architecture Arch>
BitstreamWriter<Arch>::packet_iterator::packet_iterator(
  const ConfigurationPacket *packet, state_t state, data_iterator_t itr_data)
    : state_(state), itr_data_(itr_data), packet_(packet) {}

template <Architecture Arch>
typename BitstreamWriter<Arch>::packet_iterator &
BitstreamWriter<Arch>::packet_iterator::operator++() {
  if (state_ == STATE_HEADER) {
    itr_data_ = packet_->data().begin();
    if (itr_data_ == packet_->data().end()) {
      state_ = STATE_END;
    } else {
      state_ = STATE_DATA;
    }
  } else if (state_ == STATE_DATA) {
    /// Advance. data must be valid while not at end
    itr_data_++;
    // Reached this end of this packet?
    if (itr_data_ == packet_->data().end()) {
      state_ = STATE_END;
    }
  }
  return *this;
}

template <Architecture Arch>
bool BitstreamWriter<Arch>::packet_iterator::operator==(
  const packet_iterator &other) const {
  return state_ == other.state_ && itr_data_ == other.itr_data_;
}

template <Architecture Arch>
bool BitstreamWriter<Arch>::packet_iterator::operator!=(
  const packet_iterator &other) const {
  return !(*this == other);
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::itr_value_type
BitstreamWriter<Arch>::packet_iterator::operator*() const {
  if (state_ == STATE_HEADER) {
    return packet2header(*packet_);
  }
  if (state_ == STATE_DATA) {
    return *itr_data_;
  }
  CHECK(false) << "unreachable state";
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::itr_value_type
BitstreamWriter<Arch>::packet_iterator::operator->() const {
  return *(*this);
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::iterator BitstreamWriter<Arch>::begin() {
  typename packets_t::const_iterator itr_packets = packets_.begin();
  absl::optional<packet_iterator> op_packet_itr;

  // May have no packets
  if (itr_packets != packets_.end()) {
    // op_packet_itr = packet_begin();
    // FIXME: de-duplicate this
    const ConfigurationPacket &packet = **itr_packets;
    packet_iterator packet_itr = packet_iterator(
      &packet, packet_iterator::STATE_HEADER, packet.data().begin());
    op_packet_itr = packet_itr;
  }
  return iterator(header_.begin(), packets_, itr_packets, op_packet_itr);
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::iterator BitstreamWriter<Arch>::end() {
  return iterator(header_.end(), packets_, packets_.end(),
                  absl::optional<packet_iterator>());
}

template <Architecture Arch>
BitstreamWriter<Arch>::iterator::iterator(
  header_t::iterator itr_header,
  const typename BitstreamWriter<Arch>::packets_t &packets,
  typename BitstreamWriter<Arch>::packets_t::const_iterator itr_packets,
  absl::optional<packet_iterator> itr_packet)
    : itr_header_(itr_header),
      packets_(packets),
      itr_packets_(itr_packets),
      op_itr_packet_(itr_packet) {}

template <Architecture Arch>
typename BitstreamWriter<Arch>::iterator &
BitstreamWriter<Arch>::iterator::operator++() {
  // Still generating header?
  if (itr_header_ != header_.end()) {
    itr_header_++;
    // Finished header?
    // Will advance to initialized itr_packets value
    // XXX: maybe should just overwrite here
    if (itr_header_ == header_.end()) {
      itr_packets_ = packets_.begin();
      if (itr_packets_ != packets_.end()) {
        op_itr_packet_ = packet_begin();
      }
    }
    // Then somewhere in packets
  } else {
    // We are either at end() in which case this operation is
    // invalid Or there is a packet in progress packet in progress?
    // Advance it
    ++(*op_itr_packet_);
    // Done with this packet?
    if (*op_itr_packet_ == packet_end()) {
      itr_packets_++;
      if (itr_packets_ == packets_.end()) {
        // we are at the very end
        // invalidate data to be neat
        op_itr_packet_.reset();
      } else {
        op_itr_packet_ = packet_begin();
      }
    }
  }
  return *this;
}

template <Architecture Arch>
bool BitstreamWriter<Arch>::iterator::operator==(const iterator &other) const {
  return itr_header_ == other.itr_header_ &&
         itr_packets_ == other.itr_packets_ &&
         op_itr_packet_ == other.op_itr_packet_;
}

template <Architecture Arch>
bool BitstreamWriter<Arch>::iterator::operator!=(const iterator &other) const {
  return !(*this == other);
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::itr_value_type
BitstreamWriter<Arch>::iterator::operator*() const {
  if (itr_header_ != header_.end()) {
    return *itr_header_;
  }
  // Iterating over packets, get data from current packet position
  return *(*op_itr_packet_);
}

template <Architecture Arch>
typename BitstreamWriter<Arch>::itr_value_type
BitstreamWriter<Arch>::iterator::operator->() const {
  return *(*this);
}
}  // namespace xilinx
}  // namespace fpga
#endif  // FPGA_XILINX_XC7SERIES_BITSTREAM_WRITER_H
