#include "fpga/database.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fpga/database-parsers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace fpga {
namespace {
struct CorrectMappingAndTileNamesTestCase {
  Part part;
  PackagePins package_pins;
  TileGrid grid;
  absl::StatusOr<absl::flat_hash_map<uint32_t, std::vector<std::string>>>
    expected_banks_tiles_res;
};

// Tile names should be unique per bank. The IOBank locations should be
// prepended by "HCLK_IOI3_" (or "HCLK_IOI_" on the parts whose banks use HP
// IOLOGIC) to make the final tile name.
TEST(BanksTilesRegistry, CorrectMappingAndTileNames) {
  // clang-format off
  const struct CorrectMappingAndTileNamesTestCase kTestCases[] = {{
      .part = {
        {}, {}, IOBanksIDsToLocation{{0, "X1Y78"}, {3, "X2Y43"}, {4, "X1Y78"}}
      },
      .package_pins = {
        {{}, 0, {}, "LIOB33_X0Y93", {}},
        {{}, 216, {}, "GTP_CHANNEL_1_X97Y121", {}},
        {{}, 0, {}, "HCLK_IOI3_X1Y79", {}}
      },
      .grid = {{"HCLK_IOI3_X1Y78", {}}, {"HCLK_IOI3_X2Y43", {}}},
      .expected_banks_tiles_res = {{
          {0, {"HCLK_IOI3_X1Y78", "LIOB33_X0Y93", "HCLK_IOI3_X1Y79"}},
          {3, {"HCLK_IOI3_X2Y43"}},
          {4, {"HCLK_IOI3_X1Y78"}},
          {216, {"GTP_CHANNEL_1_X97Y121"}}}
      },
    },
    // HP-only parts name the bank anchor HCLK_IOI, and the HCLK_IOI3 tile the
    // HR naming implies does not exist in their grid.
    {
      .part = {{}, {}, IOBanksIDsToLocation{{0, "X82Y130"}}},
      .package_pins = {
        {{}, 0, {}, "LIOB18_X81Y128", {}},
      },
      .grid = {{"HCLK_IOI_X82Y130", {}}},
      .expected_banks_tiles_res = {{
          {0, {"HCLK_IOI_X82Y130", "LIOB18_X81Y128"}}}
      },
    },
  };
  // clang-format on
  for (const auto &test : kTestCases) {
    const absl::StatusOr<BanksTilesRegistry> res =
      BanksTilesRegistry::Create(test.part, test.package_pins, test.grid);
    if (test.expected_banks_tiles_res.ok()) {
      ASSERT_TRUE(res.ok()) << res.status().message();
    }
    const absl::flat_hash_map<uint32_t, std::vector<std::string>> &expected =
      test.expected_banks_tiles_res.value();
    const BanksTilesRegistry &registry = res.value();
    for (const auto &pair : expected) {
      const auto maybe_tiles = registry.Tiles(pair.first);
      ASSERT_TRUE(maybe_tiles.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const std::vector<std::string> &tiles_vector = maybe_tiles.value();
      const absl::flat_hash_set<std::string> actual_tiles(tiles_vector.begin(),
                                                          tiles_vector.end());
      // Vector must have unique elements (tile names).
      EXPECT_EQ(tiles_vector.size(), actual_tiles.size());

      const absl::flat_hash_set<std::string> expected_tiles(pair.second.begin(),
                                                            pair.second.end());
      EXPECT_EQ(actual_tiles, expected_tiles);

      // Check all the tiles can be mapped back to the bank.
      for (const auto &tile : tiles_vector) {
        const std::vector<uint32_t> banks = registry.TileBanks(tile);
        ASSERT_FALSE(banks.empty());
        EXPECT_THAT(banks, ::testing::Contains(pair.first));
      }
    }
  }
}
// Tiles whose bits alias another tile type (e.g. LIOI3_SING -> LIOI3, see
// prjxray's tilegrid "alias" entries) keep a pseudo-PIP database of their own
// type: LIOI3_SING.IOI_LOGIC_OUTS18_0.IOI_ILOGIC0_O is an "always" pseudo PIP
// in ppips_lioi3_sing.db and does not exist in the LIOI3 databases. Such
// features must resolve before the alias is applied, and real features must
// still map through the aliased type's segbits with the alias offset.
TEST(PartDatabase, AliasedTileResolvesOwnPseudoPIPs) {
  TileGrid grid;
  grid.insert(
    {"LIOI3_SING_X0Y50",
     Tile{.type = "LIOI3_SING",
          .coord = {0, 50},
          .clock_region = {},
          .bits = {{ConfigBusType::kCLBIOCLK,
                    BitsBlock{.alias = BitsBlockAlias{.sites = {},
                                                      .start_offset = 2,
                                                      .type = "LIOI3"},
                              .base_address = 0x400,
                              .frames = 42,
                              .offset = 0,
                              .words = 2}}},
          .pin_functions = {},
          .sites = {},
          .prohibited_sites = {}}});
  int alias_loads = 0;
  const TileTypesSegmentsBitsGetter getter =
    [&alias_loads](const std::string &tile_type)
    -> std::optional<SegmentsBitsWithPseudoPIPs> {
    if (tile_type == "LIOI3_SING") {
      return SegmentsBitsWithPseudoPIPs{
        .pips = {{"LIOI3_SING.IOI_LOGIC_OUTS18_0.IOI_ILOGIC0_O",
                  PseudoPIPType::kAlways}},
        .segment_bits = {}};
    }
    if (tile_type == "LIOI3") {
      ++alias_loads;
      SegmentsBits segbits;
      segbits.insert(
        {TileFeature{.tile_feature = "LIOI3.IOB_Y0.PULLTYPE.PULLUP",
                     .address = 0},
         {SegmentBit{.word_column = 3, .word_bit = 70, .is_set = true}}});
      return SegmentsBitsWithPseudoPIPs{
        .pips = {{"LIOI3.WIRE_A.WIRE_B", PseudoPIPType::kAlways}},
        .segment_bits = {{ConfigBusType::kCLBIOCLK, segbits}}};
    }
    return std::nullopt;
  };
  const absl::StatusOr<BanksTilesRegistry> banks =
    BanksTilesRegistry::Create(Part{}, PackagePins{}, grid);
  ASSERT_TRUE(banks.ok()) << banks.status().message();
  PartDatabase db(
    std::make_shared<PartDatabase::Tiles>(grid, getter, banks.value(), Part{}));

  // An own-type pseudo-PIP sets no bits and does not load the alias target.
  int calls = 0;
  EXPECT_TRUE(
    db.ConfigBits("LIOI3_SING_X0Y50", "IOI_LOGIC_OUTS18_0.IOI_ILOGIC0_O", 0,
                  [&](ConfigBusType, uint32_t, const PartDatabase::FrameBit &,
                      bool) { ++calls; })
      .ok());
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(alias_loads, 0);

  // A pseudo-PIP found only in the alias target also sets no bits.
  EXPECT_TRUE(
    db.ConfigBits("LIOI3_SING_X0Y50", "WIRE_A.WIRE_B", 0,
                  [&](ConfigBusType, uint32_t, const PartDatabase::FrameBit &,
                      bool) { ++calls; })
      .ok());
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(alias_loads, 1);

  // A real feature resolves through the aliased type's segbits, shifted by
  // the alias start offset: word bit 70 - 2 * 32 = bit 6 of word 0.
  std::vector<std::tuple<ConfigBusType, uint32_t, uint32_t, uint32_t, bool>>
    bits;
  EXPECT_TRUE(db.ConfigBits("LIOI3_SING_X0Y50", "IOB_Y0.PULLTYPE.PULLUP", 0,
                            [&](ConfigBusType bus, uint32_t address,
                                const PartDatabase::FrameBit &bit, bool value) {
                              bits.emplace_back(bus, address, bit.word,
                                                bit.index, value);
                            })
                .ok());
  ASSERT_EQ(bits.size(), 1U);
  EXPECT_EQ(bits[0],
            std::make_tuple(ConfigBusType::kCLBIOCLK, 0x403U, 0U, 6U, true));

  // Preserve main's error-status API and useful unknown-feature diagnostic.
  const absl::Status status = db.ConfigBits(
    "LIOI3_SING_X0Y50", "NOT.A.FEATURE", 0,
    [](ConfigBusType, uint32_t, const PartDatabase::FrameBit &, bool) {});
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("LIOI3_SING_X0Y50"));
  EXPECT_THAT(status.message(), ::testing::HasSubstr("NOT.A.FEATURE"));
}
}  // namespace
}  // namespace fpga
