#include "fpga/database-parsers.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/rapidjson.h"
#undef RAPIDJSON_HAS_STDSTRING

#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace fpga {
namespace {
template <typename T>
absl::Status DoAssignOrReturn(T &lhs, absl::StatusOr<T> result) {
  if (result.ok()) {
    lhs = result.value();
  }
  return result.status();
}

// Internal helper for concatenating macro values.
#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y)       STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ASSIGN_OR_RETURN_IMPL(status, lhs, rexpr)       \
  absl::Status status = DoAssignOrReturn(lhs, (rexpr)); \
  if (ABSL_PREDICT_FALSE(!(status).ok())) return status;

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  ASSIGN_OR_RETURN_IMPL(             \
    STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, (rexpr));

#define OK_OR_RETURN(rexpr) \
  if (ABSL_PREDICT_FALSE(!(rexpr).ok())) return rexpr;

absl::Status IsObject(const rapidjson::Value &json) {
  return json.IsObject() ? absl::OkStatus()
                         : absl::InvalidArgumentError(
                             absl::StrFormat("json value not an object"));
}

absl::Status IsArray(const rapidjson::Value &json) {
  return json.IsArray() ? absl::OkStatus()
                        : absl::InvalidArgumentError(
                            absl::StrFormat("json value not an array"));
}

const char *JSONTypeString(const rapidjson::Type &type) {
  switch (type) {
  case rapidjson::kNullType: return "null";
  case rapidjson::kFalseType:
  case rapidjson::kTrueType: return "boolean";
  case rapidjson::kObjectType: return "object";
  case rapidjson::kArrayType: return "array";
  case rapidjson::kStringType: return "string";
  case rapidjson::kNumberType: return "number";
  default: return "unknown";
  }
}

static const absl::flat_hash_map<std::string, ConfigBusType>
  StringToConfigBusType = {{"BLOCK_RAM", ConfigBusType::kBlockRam},
                           {"CLB_IO_CLK", ConfigBusType::kCLBIOCLK},
                           {"CFG_CLB", ConfigBusType::kCFGCLB}};

std::string ValueAsString(const rapidjson::Value &json) {
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  json.Accept(writer);
  return sb.GetString();
}

absl::Status UnexpectedTypeError(const rapidjson::Value &json) {
  return absl::InvalidArgumentError(
    absl::StrFormat("could not unmarshal %s, unexpected type: %s",
                    ValueAsString(json), JSONTypeString(json.GetType())));
}

template <typename T, typename Enable = void>
absl::StatusOr<T> Unmarshal(const rapidjson::Value &json) {
  if (!json.Is<T>()) {
    return UnexpectedTypeError(json);
  }
  return json.Get<T>();
}

template <typename T>
inline absl::StatusOr<T> GetMember(const rapidjson::Value &json,
                                   std::string_view name) {
  OK_OR_RETURN(IsObject(json));
  auto itr = json.FindMember(name.data());
  if (itr == json.MemberEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
      "json attribute \"%s\" not found in %s", name, ValueAsString(json)));
  }
  return Unmarshal<T>(itr->value);
}

template <typename T>
inline absl::StatusOr<std::optional<T>> OptGetMember(
  const rapidjson::Value &json, std::string_view name) {
  OK_OR_RETURN(IsObject(json));
  auto itr = json.FindMember(name.data());
  if (itr == json.MemberEnd()) {
    return std::optional<T>{};
  }
  return Unmarshal<T>(itr->value);
}

template <>
inline absl::StatusOr<absl::flat_hash_map<std::string, std::string>> Unmarshal(
  const rapidjson::Value &json) {
  OK_OR_RETURN(IsObject(json));
  absl::flat_hash_map<std::string, std::string> value;
  std::string k;
  std::string v;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    ASSIGN_OR_RETURN(k, Unmarshal<std::string>(it->name));
    ASSIGN_OR_RETURN(v, Unmarshal<std::string>(it->value));
    value.insert({k, v});
  }
  return value;
}

template <>
inline absl::StatusOr<std::vector<std::string>> Unmarshal(
  const rapidjson::Value &json) {
  OK_OR_RETURN(IsArray(json));
  std::vector<std::string> array;
  for (const auto &item : json.GetArray()) {
    if (!item.IsString()) {
      return UnexpectedTypeError(item);
    }
    array.emplace_back(item.GetString());
  }
  return array;
}

inline absl::StatusOr<bits_addr_t> ParseBaseAddress(
  absl::StatusOr<std::string> value) {
  if (!value.ok()) return value.status();
  errno = 0;
  char *end;
  const bits_addr_t address = std::strtol(value.value().c_str(), &end, 0);
  const bool range_error = errno == ERANGE;
  if (range_error) {
    return absl::InvalidArgumentError(
      absl::StrFormat("could not parse %s to bits address", value.value()));
  }
  return address;
}

template <>
inline absl::StatusOr<BitsBlockAlias> Unmarshal(const rapidjson::Value &json) {
  BitsBlockAlias value;
  ASSIGN_OR_RETURN(
    value.sites,
    (GetMember<absl::flat_hash_map<std::string, std::string>>(json, "sites")));
  ASSIGN_OR_RETURN(value.start_offset,
                   GetMember<uint32_t>(json, "start_offset"));
  ASSIGN_OR_RETURN(value.type, GetMember<std::string>(json, "type"));
  return value;
}

template <>
inline absl::StatusOr<BitsBlock> Unmarshal(const rapidjson::Value &json) {
  BitsBlock bits_block;
  ASSIGN_OR_RETURN(bits_block.alias,
                   OptGetMember<BitsBlockAlias>(json, "alias"));
  ASSIGN_OR_RETURN(bits_block.base_address,
                   ParseBaseAddress(GetMember<std::string>(json, "baseaddr")));

  ASSIGN_OR_RETURN(bits_block.frames, GetMember<uint32_t>(json, "frames"));
  ASSIGN_OR_RETURN(bits_block.offset, GetMember<uint32_t>(json, "offset"));
  ASSIGN_OR_RETURN(bits_block.words, GetMember<uint32_t>(json, "words"));
  return bits_block;
}

template <>
absl::StatusOr<Bits> Unmarshal(const rapidjson::Value &json) {
  Bits bits;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string block_type_string = it->name.GetString();
    if (!StringToConfigBusType.contains(block_type_string)) {
      return absl::InvalidArgumentError(
        absl::StrFormat("unknown frame block type \"%s\"", block_type_string));
    }
    const ConfigBusType type = StringToConfigBusType.at(block_type_string);
    BitsBlock bits_block;
    ASSIGN_OR_RETURN(bits_block, Unmarshal<BitsBlock>(it->value));
    bits.insert({type, bits_block});
  }
  return bits;
}

template <>
absl::StatusOr<Tile> Unmarshal(const rapidjson::Value &json) {
  struct Tile tile;
  ASSIGN_OR_RETURN(tile.type, GetMember<std::string>(json, "type"));
  ASSIGN_OR_RETURN(tile.coord.x, GetMember<uint32_t>(json, "grid_x"));
  ASSIGN_OR_RETURN(tile.coord.y, GetMember<uint32_t>(json, "grid_y"));

  ASSIGN_OR_RETURN(tile.clock_region,
                   OptGetMember<std::string>(json, "clock_region"));
  ASSIGN_OR_RETURN(tile.bits, GetMember<Bits>(json, "bits"));

  ASSIGN_OR_RETURN(tile.pin_functions,
                   (GetMember<absl::flat_hash_map<std::string, std::string>>(
                     json, "pin_functions")));
  ASSIGN_OR_RETURN(
    tile.sites,
    (GetMember<absl::flat_hash_map<std::string, std::string>>(json, "sites")));
  ASSIGN_OR_RETURN(tile.prohibited_sites, (GetMember<std::vector<std::string>>(
                                            json, "prohibited_sites")));
  return tile;
}

template <>
absl::StatusOr<absl::flat_hash_map<uint32_t, std::string>> Unmarshal(
  const rapidjson::Value &json) {
  absl::flat_hash_map<uint32_t, std::string> out;
  uint32_t key;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string key_string = it->name.GetString();
    if (!absl::SimpleAtoi(key_string, &key)) {
      return absl::InvalidArgumentError(absl::StrFormat(
        "cannot parse \"%s\" for %s", key_string, ValueAsString(json)));
    }
    out.insert({key, it->value.GetString()});
  }
  return out;
}

// Wrapped value so to ease specialization.
struct ConfigColumnsFramesCountInternal {
  std::vector<uint32_t> value;
};

template <>
absl::StatusOr<ConfigColumnsFramesCountInternal> Unmarshal(
  const rapidjson::Value &json) {
  OK_OR_RETURN(IsObject(json));
  ConfigColumnsFramesCountInternal out;
  int32_t key = -1;
  uint32_t frame_count;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string key_string = it->name.GetString();
    const int32_t prev = key;
    if (!absl::SimpleAtoi(key_string, &key)) {
      return absl::InvalidArgumentError(absl::StrFormat(
        "cannot parse \"%s\" for %s", key_string, ValueAsString(json)));
    }
    // We are assuming a set of keys "0", "1", "2" to be exactly in this order.
    CHECK(prev == (key - 1)) << "unexpected index";
    ASSIGN_OR_RETURN(frame_count,
                     (GetMember<uint32_t>(it->value, "frame_count")));
    out.value.push_back(frame_count);
  }
  return out;
}

template <>
absl::StatusOr<ClockRegionRow> Unmarshal(const rapidjson::Value &json) {
  OK_OR_RETURN(IsObject(json));
  ClockRegionRow row;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string cfg_type_string = it->name.GetString();
    if (!StringToConfigBusType.contains(cfg_type_string)) {
      return absl::InvalidArgumentError(
        absl::StrFormat("unknown config bus type \"%s\"", cfg_type_string));
    }
    const ConfigBusType type = StringToConfigBusType.at(cfg_type_string);
    ConfigColumnsFramesCountInternal counts;
    ASSIGN_OR_RETURN(counts, GetMember<ConfigColumnsFramesCountInternal>(
                               it->value, "configuration_columns"));
    row.insert({type, counts.value});
  }
  return row;
}

template <>
absl::StatusOr<GlobalClockRegionHalf> Unmarshal(const rapidjson::Value &json) {
  OK_OR_RETURN(IsObject(json));
  std::vector<ClockRegionRow> half;
  ClockRegionRow row;
  int32_t key = -1;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string key_string = it->name.GetString();
    const int32_t prev = key;
    if (!absl::SimpleAtoi(key_string, &key)) {
      return absl::InvalidArgumentError(absl::StrFormat(
        "cannot parse \"%s\" for %s", key_string, ValueAsString(json)));
    }
    // We are assuming a set of keys "0", "1", "2" to be exactly in this order.
    CHECK(prev == (key - 1)) << "json key not in sequence";
    ASSIGN_OR_RETURN(
      row, (GetMember<ClockRegionRow>(it->value, "configuration_buses")));
    half.push_back(row);
  }
  return half;
}

template <>
absl::StatusOr<GlobalClockRegions> Unmarshal(const rapidjson::Value &json) {
  GlobalClockRegions regions;
  static const std::string kBottomRowsAttribute = "/bottom/rows";
  const rapidjson::Value *bottom_rows =
    rapidjson::Pointer(kBottomRowsAttribute.c_str()).Get(json);
  if (bottom_rows == nullptr) {
    return absl::InvalidArgumentError(
      "could not find global_clock_region bottom rows");
  }
  ASSIGN_OR_RETURN(regions.bottom_rows,
                   Unmarshal<GlobalClockRegionHalf>(*bottom_rows));
  static const std::string kTopRowsAttribute = "/top/rows";
  const rapidjson::Value *top_rows =
    rapidjson::Pointer(kTopRowsAttribute.c_str()).Get(json);
  if (top_rows == nullptr) {
    return absl::InvalidArgumentError(
      "could not find global_clock_region top rows");
  }
  ASSIGN_OR_RETURN(regions.top_rows,
                   Unmarshal<GlobalClockRegionHalf>(*top_rows));
  return regions;
}

template <>
absl::StatusOr<Part> Unmarshal(const rapidjson::Value &json) {
  struct Part part;
  OK_OR_RETURN(IsObject(json));
  ASSIGN_OR_RETURN(part.idcode, GetMember<uint32_t>(json, "idcode"));
  ASSIGN_OR_RETURN(
    part.iobanks,
    (GetMember<absl::flat_hash_map<uint32_t, std::string>>(json, "iobanks")));
  ASSIGN_OR_RETURN(part.global_clock_regions, (GetMember<GlobalClockRegions>(
                                                json, "global_clock_regions")));
  return part;
}
#undef OK_OR_RETURN
#undef ASSIGN_OR_RETURN
#undef ASSIGN_OR_RETURN_IMPL
#undef STATUS_MACROS_CONCAT_NAME
#undef STATUS_MACROS_CONCAT_NAME_INNER
}  // namespace

absl::StatusOr<TileGrid> ParseTileGridJSON(const std::string_view content) {
  rapidjson::Document json;
  const rapidjson::ParseResult ok = json.Parse(content.data(), content.size());
  if (!ok) {
    return absl::InvalidArgumentError(
      absl::StrFormat("json parsing error, %s (%u)",
                      rapidjson::GetParseError_En(ok.Code()), ok.Offset()));
  }
  if (!json.IsObject()) {
    return absl::InvalidArgumentError(
      absl::StrFormat("json value not an object"));
  }
  TileGrid tilegrid;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    const std::string name = it->name.GetString();
    const absl::StatusOr<Tile> tile = Unmarshal<Tile>(it->value);
    if (!tile.ok()) {
      return absl::InvalidArgumentError(absl::StrFormat(
        "could not unmarshal tile %s: %s", name, tile.status().message()));
    }
    tilegrid.insert({name, tile.value()});
  }
  return tilegrid;
}

namespace {
// [[unlikely]] only available since c++20, so use gcc/clang builtin here.
#define unlikely(x) __builtin_expect((x), 0)

// Skip forward until we sit on the '\n' end of current line.
#define skip_to_eol() \
  while (*it != '\n' && unlikely(it < end)) ++it

using LinesSink = std::function<absl::Status(uint32_t, const std::string_view)>;

// Given a sequence of characters, calls sink for every subsequence
// delimited by new lines except for the first and the last.
absl::Status LinesGenerator(const std::string_view content,
                            const LinesSink &sink) {
  if (content.empty()) {
    return absl::OkStatus();
  }
  const char *it = content.begin();
  const char *const end = content.end();
  uint32_t line_number = 0;
  absl::Status status;
  while (it < end) {
    ++line_number;
    const char *const line_start = it;
    skip_to_eol();
    status = sink(line_number, std::string_view(line_start, it - line_start));
    if (!status.ok()) return status;
    ++it;  // Skip new line.
  }
  return absl::OkStatus();
}
#undef skip_to_eol
#undef unlikely

absl::Status MakeInvalidLineStatus(const uint32_t line_number,
                                   std::string_view message) {
  return absl::InvalidArgumentError(
    absl::StrFormat("%d: %s", line_number, message));
}

absl::Status ParsePseudoPIPTypeFromString(const std::string &value,
                                          PseudoPIPType *type) {
  static const absl::flat_hash_map<std::string, PseudoPIPType>
    pseudo_pip_type_string = {
      {"always", PseudoPIPType::kAlways},
      {"default", PseudoPIPType::kDefault},
      {"hint", PseudoPIPType::kHint},
    };
  if (pseudo_pip_type_string.contains(value)) {
    *type = pseudo_pip_type_string.at(value);
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
    absl::StrFormat("invalid pseudo pip state \"%s\"", value));
}

absl::Status ParsePseudoPIPDatabaseLine(uint32_t line_count,
                                        const std::string_view line,
                                        PseudoPIPs &out) {
  if (line.empty()) return absl::OkStatus();
  std::vector<std::string> segments =
    absl::StrSplit(line, absl::ByAsciiWhitespace(), absl::SkipEmpty());
  if (segments.size() != 2) {
    return MakeInvalidLineStatus(line_count,
                                 absl::StrFormat("invalid line \"%s\"", line));
  }
  const std::string &name = segments[0];
  PseudoPIPType type = {};
  absl::Status status = ParsePseudoPIPTypeFromString(segments[1], &type);
  if (!status.ok()) return status;
  out.insert({name, type});
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<PseudoPIPs> ParsePseudoPIPsDatabase(
  const std::string_view content) {
  PseudoPIPs pips;
  if (content.empty()) {
    return pips;
  }
  absl::Status status = LinesGenerator(
    content,
    [&pips](uint32_t line_count, const std::string_view line) -> absl::Status {
      return ParsePseudoPIPDatabaseLine(line_count, line, pips);
    });
  if (!status.ok()) return status;
  return pips;
}

namespace {
// Parse a string like "FOO.BASD.SDA#@@!@E!{]{]{}[231]".
// The returned output will be "FOO.BASD.SDA#@@!@E!{]{]{}" with address 231.
TileFeature ParseTileFeatureNameAndAddress(const std::string &value) {
  TileFeature out{
    .tile_feature = value,
    .address = 0,
  };
  const char *const start = value.data();

  // Start from the end, if there's a ']'
  // reverse iterate until you find the opening bracket.
  if (absl::EndsWith(value, "]")) {
    const size_t open_bracket_pos = value.rfind('[');
    if (open_bracket_pos == std::string::npos) {
      // Best effort, opening bracket not found, no matching, return.
      return out;
    }
    const char *const start_address = start + open_bracket_pos + 1;
    if (!absl::SimpleAtoi(
          std::string_view(start_address, value.size() - open_bracket_pos - 2),
          &out.address)) {
      // Cannot parse integer, give up on parsing an address.
      return out;
    }
    out.tile_feature = std::string(start, start + open_bracket_pos);
  }
  return out;
}

absl::Status ParseSegmentsBitsDatabaseLine(uint32_t line_count,
                                           const std::string_view line,
                                           SegmentsBits &out) {
  std::vector<std::string> segments =
    absl::StrSplit(line, absl::ByAsciiWhitespace(), absl::SkipEmpty());
  if (segments.empty()) return absl::OkStatus();
  if (segments.size() == 1) {
    return MakeInvalidLineStatus(line_count,
                                 absl::StrFormat("invalid line \"%s\"", line));
  }
  const std::string &name = segments[0];
  const TileFeature name_and_address = ParseTileFeatureNameAndAddress(name);
  std::vector<SegmentBit> segment_bits;
  for (size_t i = 1; i < segments.size(); ++i) {
    const std::string_view bit = segments[i];
    const bool set = bit[0] != '!';
    const std::vector<std::string> coordinates =
      absl::StrSplit(set ? bit : bit.substr(1), '_');
    if (coordinates.size() != 2) {
      return MakeInvalidLineStatus(
        line_count, absl::StrFormat("invalid line \"%s\"", line));
    }
    uint32_t word_column;
    uint32_t word_bit;
    if (!absl::SimpleAtoi(coordinates[0], &word_column)) {
      return MakeInvalidLineStatus(
        line_count, absl::StrFormat("could not parse coordinate \"%s\"", line));
    }
    if (!absl::SimpleAtoi(coordinates[1], &word_bit)) {
      return MakeInvalidLineStatus(
        line_count, absl::StrFormat("could not parse coordinate \"%s\"", line));
    }
    segment_bits.push_back({word_column, word_bit, set});
  }
  out.insert({name_and_address, segment_bits});
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<SegmentsBits> ParseSegmentsBitsDatabase(
  std::string_view content) {
  SegmentsBits segbits;
  if (content.empty()) {
    return segbits;
  }
  absl::Status status = LinesGenerator(
    content,
    [&segbits](uint32_t line_count,
               const std::string_view line) -> absl::Status {
      return ParseSegmentsBitsDatabaseLine(line_count, line, segbits);
    });
  if (!status.ok()) return status;
  return segbits;
}

namespace {
bool IsValidPackagePinCSVHeader(const std::vector<std::string> &segments) {
  constexpr std::string_view kHeader[] = {
    "pin", "bank", "site", "tile", "pin_function",
  };
  if (segments.size() != std::size(kHeader)) {
    return false;
  }
  for (size_t i = 0; i < std::size(kHeader); ++i) {
    if (segments[i] != kHeader[i]) return false;
  }
  return true;
}

std::vector<std::string> StripAsciiWhitespaces(
  const std::vector<std::string> &src) {
  std::vector<std::string> out;
  std::transform(src.begin(), src.end(), std::back_inserter(out),
                 [](const std::string &s) -> std::string {
                   return std::string(absl::StripAsciiWhitespace(s));
                 });
  return out;
}

absl::Status ParsePackagePinCSVLine(uint32_t line_count,
                                    const std::string_view line,
                                    PackagePins &out) {
  std::vector<std::string> segments =
    StripAsciiWhitespaces(absl::StrSplit(line, ',', absl::SkipEmpty()));
  // First line, it's header, check it and skip.
  if (line_count == 1) {
    if (!IsValidPackagePinCSVHeader(segments)) {
      return MakeInvalidLineStatus(line_count, "missing header");
    }
    return absl::OkStatus();
  }
  if (segments.empty()) return absl::OkStatus();
  if (segments.size() != 5) {
    return MakeInvalidLineStatus(line_count,
                                 absl::StrFormat("invalid line \"%s\"", line));
  }
  PackagePin pp;
  pp.pin = segments[0];
  if (!absl::SimpleAtoi(segments[1], &pp.bank)) {
    return MakeInvalidLineStatus(
      line_count,
      absl::StrFormat("could not parse bank (second column) \"%s\"", line));
  }
  pp.site = segments[2];
  pp.tile = segments[3];
  pp.pin_function = segments[4];
  out.push_back(pp);
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<PackagePins> ParsePackagePins(std::string_view content) {
  PackagePins package_pins;
  if (content.empty()) {
    return package_pins;
  }
  absl::Status status = LinesGenerator(
    content,
    [&package_pins](uint32_t line_count,
                    const std::string_view line) -> absl::Status {
      return ParsePackagePinCSVLine(line_count, line, package_pins);
    });
  if (!status.ok()) return status;
  return package_pins;
}

absl::StatusOr<Part> ParsePartJSON(const std::string_view content) {
  rapidjson::Document json;
  const rapidjson::ParseResult ok = json.Parse(content.data(), content.size());
  if (!ok) {
    return absl::InvalidArgumentError(
      absl::StrFormat("json parsing error, %s (%u)",
                      rapidjson::GetParseError_En(ok.Code()), ok.Offset()));
  }
  return Unmarshal<Part>(json);
}

namespace {
// Sink that gets called with the key as first argument and a set of properties
// for the second associated with that key.
using KeyPropsSink = std::function<absl::Status(
  std::string_view, const absl::flat_hash_map<std::string, std::string> &)>;

// Helper function that removes matching surrounding quotes (either single or
// double) from the given string view.
std::string_view RemoveSurroundingQuotes(std::string_view s) {
  if (s.size() >= 2) {
    const char first = s.front();
    const char last = s.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      s.remove_prefix(1);
      s.remove_suffix(1);
    }
  }
  return s;
}
// Parses a YAML mapping from the provided content. It will invoke the provided
// sink for each top-level key and its associated mapping (nested properties).
//
// This function supports two variants of YAML data. For example:
//
//   # Variant 1
//   "xc7a200t":
//     fabric: "xc7a200t"
//   "xc7a100t":
//     fabric: "xc7a100t"
//
//   # Variant 2
//   xc7a100tcsg324-3:
//     device: xc7a100t
//     package: csg324
//     speedgrade: '3'
//
// Nested lines must have at least one level of indentation, and top-level lines
// must end with a colon.
absl::Status ParseMappingYAML(const std::string_view content,
                              const KeyPropsSink &sink) {
  const std::vector<std::string_view> lines = absl::StrSplit(content, '\n');
  std::string current_key;
  absl::flat_hash_map<std::string, std::string> properties;

  for (const std::string_view line : lines) {
    // Remove surrounding whitespace.
    std::string_view trimmed = absl::StripAsciiWhitespace(line);

    // Skip empty lines and comment lines.
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    // Compute the indentation level (number of leading spaces).
    const int indent =
      line.size() - absl::StripLeadingAsciiWhitespace(line).size();

    if (indent == 0) {
      // Flush any previous mapping entry.
      if (!current_key.empty()) {
        absl::Status st = sink(current_key, properties);
        if (!st.ok()) {
          return st;
        }
      }

      // Expect a top-level key line ending with a colon.
      if (!absl::ConsumeSuffix(&trimmed, ":")) {
        return absl::InvalidArgumentError(
          "Top-level key missing trailing colon");
      }

      // Remove any surrounding quotes.
      trimmed = RemoveSurroundingQuotes(trimmed);
      current_key = std::string(trimmed);
      properties.clear();  // Reset properties for the new entry.
    } else {
      // Nested line: expect a key-value pair separated by a colon.
      const size_t colon_pos = trimmed.find(':');
      if (colon_pos == std::string_view::npos) {
        return absl::InvalidArgumentError(
          "Nested key-value pair missing colon");
      }

      const std::string_view key =
        absl::StripAsciiWhitespace(trimmed.substr(0, colon_pos));
      std::string_view value =
        absl::StripAsciiWhitespace(trimmed.substr(colon_pos + 1));

      // Remove surrounding quotes from the value if any.
      value = RemoveSurroundingQuotes(value);

      properties[std::string(key)] = std::string(value);
    }
  }

  // Flush the last mapping if present.
  if (!current_key.empty()) {
    absl::Status st = sink(current_key, properties);
    if (!st.ok()) {
      return st;
    }
  }

  return absl::OkStatus();
}

struct MapperParts {
  std::string device;
  std::string package;
  std::string speedgrade;
};

struct MapperDevices {
  std::string fabric;
};

template <typename K, typename V>
absl::Status ValueOrStatus(absl::flat_hash_map<K, V> map, const K &key,
                           V &value) {
  if (map.count(key) == 0) {
    return absl::InvalidArgumentError(
      absl::StrFormat("key \"%s\" not found", std::string(key)));
  }
  value = map.at(key);
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<absl::flat_hash_map<std::string, PartInfo>> ParsePartsInfos(
  std::string_view parts_mapper_yaml, std::string_view devices_mapper_yaml) {
  // First map device to fabric.
  absl::flat_hash_map<std::string, std::string> fabrics;
  absl::Status status = ParseMappingYAML(
    devices_mapper_yaml,
    [&fabrics](std::string_view part,
               const absl::flat_hash_map<std::string, std::string> &props)
      -> absl::Status {
      constexpr const char *kFabricProperty = "fabric";
      if (!props.contains(kFabricProperty)) {
        return absl::InvalidArgumentError("parts yaml doesn't contain fabric");
      }
      const std::string &fabric = props.at(kFabricProperty);
      fabrics.insert({std::string(part), fabric});
      return absl::OkStatus();
    });
  if (!status.ok()) return status;
  absl::flat_hash_map<std::string, PartInfo> parts_infos;
  // First fill-in all the parts map.
  status = ParseMappingYAML(
    parts_mapper_yaml,
    [&fabrics, &parts_infos](
      std::string_view part,
      const absl::flat_hash_map<std::string, std::string> &props)
      -> absl::Status {
      absl::Status status;
      PartInfo info;
      status =
        ValueOrStatus<std::string, std::string>(props, "device", info.device);
      if (!status.ok()) return status;
      status =
        ValueOrStatus<std::string, std::string>(props, "package", info.package);
      if (!status.ok()) return status;
      status = ValueOrStatus<std::string, std::string>(props, "speedgrade",
                                                       info.speedgrade);
      if (!status.ok()) return status;

      // Get the fabric.
      if (!fabrics.contains(info.device)) {
        return absl::InvalidArgumentError(absl::StrFormat(
          "could not find fabric for device: \"%s\"", info.device));
      }
      info.fabric = fabrics.at(info.device);
      parts_infos.insert({std::string(part), info});
      return absl::OkStatus();
    });
  if (!status.ok()) return status;
  return parts_infos;
}
}  // namespace fpga
