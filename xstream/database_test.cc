#include "xstream/database.h"
#include <unordered_set>

#include "gtest/gtest.h"

namespace xstream {
namespace {
struct CorrectMappingAndTileNamesTestCase {
  Part part;
  PackagePins package_pins;
  absl::StatusOr<
    std::map<uint32_t, std::vector<std::string>>> expected_banks_tiles_res;
};

// Tile names should be unique per bank. The IOBank locations should be
// prepended by "HCLK_IOI3_" to make the final tile name.
TEST(BanksTilesRegistry, CorrectMappingAndTileNames) {
  const struct CorrectMappingAndTileNamesTestCase kTestCases[] = {
    {
      {{}, {}, IOBanksIDsToLocation{{0, "X1Y78"}, {3, "X2Y43"}}},
      {
        {{}, 0, {}, "LIOB33_X0Y93", {}},
        {{}, 216, {}, "GTP_CHANNEL_1_X97Y121", {}},
        {{}, 0, {}, "HCLK_IOI3_X1Y79", {}}
      },
      {{{0, {"HCLK_IOI3_X1Y78", "LIOB33_X0Y93", "HCLK_IOI3_X1Y79"}},
        {3, {"HCLK_IOI3_X2Y43"}},
        {216, {"GTP_CHANNEL_1_X97Y121"}}}}
    },
  };

  for (const auto &test : kTestCases) {
    const absl::StatusOr<BanksTilesRegistry> res = BanksTilesRegistry::Create(test.part, test.package_pins);
    if (test.expected_banks_tiles_res.ok()) {
      ASSERT_TRUE(res.ok()) << res.status().message();
    }
    const std::map<uint32_t, std::vector<std::string>> &expected = test.expected_banks_tiles_res.value();
    const BanksTilesRegistry &registry = res.value();
    for (const auto &pair : expected) {
      const auto maybe_tiles = registry.Tiles(pair.first);
      EXPECT_TRUE(maybe_tiles.has_value());
      const std::vector<std::string> &tiles_vector = maybe_tiles.value();
      const std::unordered_set<std::string> actual_tiles(tiles_vector.begin(), tiles_vector.end());
      // Vector must have unique elements (tile names).
      EXPECT_EQ(tiles_vector.size(), actual_tiles.size());

      const std::unordered_set<std::string> expected_tiles(pair.second.begin(), pair.second.end());
      EXPECT_EQ(actual_tiles, expected_tiles);

      // Check all the tiles can be mapped back to the bank.
      for (const auto &tile : tiles_vector) {
        const std::optional<uint32_t> maybe_bank = registry.TileBank(tile);
        ASSERT_TRUE(maybe_bank.has_value());
        EXPECT_EQ(maybe_bank.value(), pair.first);
      }
    }
  }
}
}  // namespace
}  // namespace xstream
