#include "prjxstream/database.h"

#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace prjxstream {
namespace {
constexpr absl::string_view kSampleTileGrid = R"({
  "TILE_A": {
    "bits": {
      "CLB_IO_CLK": {
        "alias": {
          "sites": {},
          "start_offset": 0,
          "type": "HCLK_L"
        },
        "baseaddr": "0x00020E00",
        "frames": 26,
        "offset": 50,
        "words": 1
      }
    },
    "grid_x": 72,
    "grid_y": 26,
    "pin_functions": {},
    "prohibited_sites": [],
    "sites": {},
    "type": "HCLK_L_BOT_UTURN"
  },
  "TILE_B": {
    "bits": {
      "CLB_IO_CLK": {
        "alias": {
          "sites": {
            "IOB33_Y0": "IOB33_Y0"
          },
          "start_offset": 2,
          "type": "LIOB33"
        },
        "baseaddr": "0x00400000",
        "frames": 42,
        "offset": 0,
        "words": 2
      }
    },
    "clock_region": "X0Y0",
    "grid_x": 0,
    "grid_y": 155,
    "pin_functions": {
      "IOB_X0Y0": "IO_25_14"
    },
    "prohibited_sites": [],
    "sites": {
      "IOB_X0Y0": "IOB33"
    },
    "type": "LIOB33_SING"
  }
})";

// Test some basic expectaions for a sample tilegrid.json
TEST(TileGridParser, SampleTileGrid) {
  absl::StatusOr<TileGrid> tile_grid_result = ParseTileGridJSON(kSampleTileGrid);
  EXPECT_TRUE(tile_grid_result.ok());
  const TileGrid &tile_grid = tile_grid_result.value();
  EXPECT_EQ(tile_grid.size(), 2);
  EXPECT_EQ(tile_grid.count("TILE_A"), 1);
  EXPECT_EQ(tile_grid.count("TILE_B"), 1);

  const Tile &tile_a = tile_grid.at("TILE_A");
  EXPECT_EQ(tile_a.coord.x, 72);
  EXPECT_EQ(tile_a.coord.y, 26);

  // Check bits block.
  EXPECT_TRUE(tile_a.bits.has_value());
  EXPECT_EQ(tile_a.pin_functions.size(), 0);

  const Tile &tile_b = tile_grid.at("TILE_B");
  EXPECT_EQ(tile_b.pin_functions.size(), 1);
  EXPECT_EQ(tile_b.bits.value().count(FrameBlockType::kCLBIOCLK), 1);

  const BitsBlock &block = tile_b.bits.value().at(FrameBlockType::kCLBIOCLK);
  EXPECT_TRUE(block.alias.has_value());
  EXPECT_EQ(block.alias.value().sites.size(), 1);
  EXPECT_EQ(block.base_address, 4194304);  // 0x00400000
}

using TileGridParserTestCaseArray = std::initializer_list<const char *>;

// Inputs that should fail gracefully.
TEST(TileGridParser, EmptyTileGrid) {
  constexpr absl::string_view kExpectFailTests[] = {
    "", "[]", "  ", "\n\n", "32", "asd",
    R"({
  "TILE_A": {
    "bits": {},
    "grid_x": 72,
    "grid_y": 26,
    "pin_functions": {},
    "prohibited_sites": [],
    "type": "HCLK_L_BOT_UTURN"
  },
})"
  };
  for (const auto &v : kExpectFailTests) {
    const absl::StatusOr<TileGrid> tile_grid_result = ParseTileGridJSON(v);
    EXPECT_FALSE(tile_grid_result.ok());
  }
}

struct PseudoPIPsParserTestCase {
  absl::string_view db;
  PseudoPIPs expected_ppips;
  bool success;
};

TEST(PseudoPIPsParser, SamplePseudoPip) {
  const struct PseudoPIPsParserTestCase kTestCases[] = {
    {.db= "Palways", .expected_ppips = {}, .success = false },
    {.db= "P    always", .expected_ppips = {{"P", PseudoPIPType::kAlways}}, .success = true },
    {.db= "P  always   \n", .expected_ppips = {{"P", PseudoPIPType::kAlways}}, .success = true },
    {.db= "P always", .expected_ppips = {{"P", PseudoPIPType::kAlways}}, .success = true },
    {.db= "P default", .expected_ppips = {{"P", PseudoPIPType::kDefault}}, .success = true },
    {.db= "P hint", .expected_ppips = {{"P", PseudoPIPType::kHint}}, .success = true },
    {
      .db= "P  always   \n  A   default \n",
      .expected_ppips = {
        {"P", PseudoPIPType::kAlways},
        {"A", PseudoPIPType::kDefault}},
      .success = true
    },
  };
  for (const auto &test : kTestCases) {
    const absl::StatusOr<PseudoPIPs> res = ParsePseudPIPsDatabase(test.db);
    if (!test.success) {
      EXPECT_FALSE(res.ok());
    } else {
      ASSERT_TRUE(res.ok()) << absl::StrFormat("for \"%s\"", test.db);
      EXPECT_EQ(res.value(), test.expected_ppips);
    }
  }
}
}  // namespace
}  // namespace prjxstream

