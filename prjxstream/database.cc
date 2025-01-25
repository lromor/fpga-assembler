#include "prjxstream/database.h"
#include <optional>
#include <cstdlib>

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#undef RAPIDJSON_HAS_STDSTRING

#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace prjxstream {
namespace {
template <typename T>
absl::Status DoAssignOrReturn(T& lhs, absl::StatusOr<T> result) {
  if (result.ok()) {
    lhs = result.value();
  }
  return result.status();
}

// Internal helper for concatenating macro values.
#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y)       STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ASSIGN_OR_RETURN_IMPL(status, lhs, rexpr)               \
  absl::Status status =                                         \
      DoAssignOrReturn(lhs, (rexpr));                           \
  if (ABSL_PREDICT_FALSE(!(status).ok())) return status;

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  ASSIGN_OR_RETURN_IMPL(             \
      STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, (rexpr));

#define OK_OR_RETURN(rexpr) \
  if (ABSL_PREDICT_FALSE(!(rexpr).ok())) return rexpr;


absl::Status IsObject(const rapidjson::Value &json) {
  return json.IsObject() ?
      absl::OkStatus() :
      absl::InvalidArgumentError(
          absl::StrFormat("json value not an object"));
}

absl::Status IsArray(const rapidjson::Value &json) {
  return json.IsArray() ?
      absl::OkStatus() :
      absl::InvalidArgumentError(
          absl::StrFormat("json value not an array"));
}

const char * JSONTypeString(const rapidjson::Type& type) {
  switch (type) {
    case rapidjson::kNullType:   return "null";
    case rapidjson::kFalseType:
    case rapidjson::kTrueType:   return "boolean";
    case rapidjson::kObjectType: return "object";
    case rapidjson::kArrayType:  return "array";
    case rapidjson::kStringType: return "string";
    case rapidjson::kNumberType: return "number";
    default:                     return "unknown";
  }
}

static const std::map<std::string, FrameBlockType> StringToFrameBlock = {
  {"BLOCK_RAM", FrameBlockType::kBlockRam},
  {"CLB_IO_CLK", FrameBlockType::kCLBIOCLK},
};

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
inline absl::StatusOr<T> GetMember(const rapidjson::Value &json, absl::string_view name) {
  OK_OR_RETURN(IsObject(json));
  auto itr = json.FindMember(name.data());
  if (itr == json.MemberEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("json attribute \"%s\" not found in %s", name, ValueAsString(json)));
  }
  return Unmarshal<T>(itr->value);
}

template <typename T>
inline absl::StatusOr<std::optional<T>> OptGetMember(const rapidjson::Value &json, absl::string_view name) {
  OK_OR_RETURN(IsObject(json));
  auto itr = json.FindMember(name.data());
  if (itr == json.MemberEnd()) {
    return std::optional<T>{};
  }
  return Unmarshal<T>(itr->value);
}

template <>
inline absl::StatusOr<std::map<std::string, std::string>> Unmarshal(
    const rapidjson::Value &json) {
  OK_OR_RETURN(IsObject(json));
  std::map<std::string, std::string> value;
  std::string k;
  std::string v;
  for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
    ASSIGN_OR_RETURN(k, Unmarshal<std::string>(it->name));
    ASSIGN_OR_RETURN(k, Unmarshal<std::string>(it->value));
    value.insert({k, v});
  }
  return value;
}

template <>
inline absl::StatusOr<std::vector<std::string>> Unmarshal(
    const rapidjson::Value &json) {
  OK_OR_RETURN(IsArray(json));
  std::vector<std::string> array;
  std::string value;
  for (const auto& item : json.GetArray()) {
    if (!item.IsString()) {
      return UnexpectedTypeError(item);
    }
    array.emplace_back(item.GetString());
  }
  return array;
}

inline absl::StatusOr<bits_addr_t> ParseBaseAddress(absl::StatusOr<std::string> value) {
  if (!value.ok()) return value.status();
  errno = 0;
  char* end;
  bits_addr_t address = std::strtol(value.value().c_str(), &end, 0);
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
  ASSIGN_OR_RETURN(value.sites, (GetMember<std::map<std::string, std::string>>(json, "sites")));
  ASSIGN_OR_RETURN(value.start_offset, GetMember<uint32_t>(json, "start_offset"));
  ASSIGN_OR_RETURN(value.type, GetMember<std::string>(json, "type"));
  return value;
}

template <>
inline absl::StatusOr<BitsBlock> Unmarshal(const rapidjson::Value &json) {
  BitsBlock bits_block;
  ASSIGN_OR_RETURN(bits_block.alias, OptGetMember<BitsBlockAlias>(json, "alias"));
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
    if (!StringToFrameBlock.count(block_type_string)) {
      return absl::InvalidArgumentError(
          absl::StrFormat("unknown frame block type \"%s\"", block_type_string));
    }
    const FrameBlockType type = StringToFrameBlock.at(block_type_string);
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

  ASSIGN_OR_RETURN(tile.clock_region, OptGetMember<std::string>(json, "clock_region"));
  ASSIGN_OR_RETURN(tile.bits, OptGetMember<Bits>(json, "bits"));

  ASSIGN_OR_RETURN(tile.pin_functions, (GetMember<std::map<std::string, std::string>>(json, "pin_functions")));
  ASSIGN_OR_RETURN(tile.sites, (GetMember<std::map<std::string, std::string>>(json, "sites")));
  ASSIGN_OR_RETURN(tile.prohibited_sites, (GetMember<std::vector<std::string>>(json, "prohibited_sites")));
  return tile;
}
#undef ASSIGN_OR_RETURN
#undef ASSIGN_OR_RETURN_IMPL
#undef STATUS_MACROS_CONCAT_NAME
#undef STATUS_MACROS_CONCAT_NAME_INNER
#undef OK_OR_RETURN
}  // namespace

absl::StatusOr<TileGrid> ParseTileGrid(absl::string_view tile_grid_json) {
  rapidjson::Document json;
  rapidjson::ParseResult ok = json.Parse(
      tile_grid_json.data(), tile_grid_json.size());
  if (!ok) {
    return absl::InvalidArgumentError(
        absl::StrFormat(
            "json parsing error, %s (%u)",
            rapidjson::GetParseError_En(ok.Code()),
            ok.Offset()));
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
      return absl::InvalidArgumentError(
          absl::StrFormat("could not unmarshal tile %s: %s", name, tile.status().message()));
    }
    tilegrid.insert({name, tile.value()});
  }
  return tilegrid;
}
}  // namespace prjxstream
