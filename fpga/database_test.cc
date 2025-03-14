#include "fpga/database.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "fpga/database-parsers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace fpga {
namespace {
struct CorrectMappingAndTileNamesTestCase {
  Part part;
  PackagePins package_pins;
  absl::StatusOr<absl::flat_hash_map<uint32_t, std::vector<std::string>>>
    expected_banks_tiles_res;
};

// Tile names should be unique per bank. The IOBank locations should be
// prepended by "HCLK_IOI3_" to make the final tile name.
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
      .expected_banks_tiles_res = {{
          {0, {"HCLK_IOI3_X1Y78", "LIOB33_X0Y93", "HCLK_IOI3_X1Y79"}},
          {3, {"HCLK_IOI3_X2Y43"}},
          {4, {"HCLK_IOI3_X1Y78"}},
          {216, {"GTP_CHANNEL_1_X97Y121"}}}
      },
    },
  };
  // clang-format on
  for (const auto &test : kTestCases) {
    const absl::StatusOr<BanksTilesRegistry> res =
      BanksTilesRegistry::Create(test.part, test.package_pins);
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
}  // namespace
}  // namespace fpga
