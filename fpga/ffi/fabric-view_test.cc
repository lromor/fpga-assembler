#include "fpga/ffi/fabric-view.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
using ::testing::ElementsAre;

// A miniature prjxray database, laid out like the real one but small enough
// to state every expected bit by hand.
constexpr const char kTestDatabaseRoot[] = "fpga/ffi/testdata/artix7";
constexpr const char kTestPart[] = "xc7tiny-test-1";

std::string ToString(fpga_view_str text) {
  return std::string(text.data == nullptr ? "" : text.data, text.size);
}

// Opens the test database, failing the test rather than crashing if the
// fixture is missing.
class FabricView : public ::testing::Test {
 protected:
  void SetUp() override {
    char *error = nullptr;
    db_ = fpga_view_db_open(kTestDatabaseRoot, kTestPart, &error);
    if (db_ == nullptr) {
      const std::string message = error != nullptr ? error : "unknown error";
      fpga_view_string_free(error);
      FAIL() << "could not open the test database: " << message;
    }
    snapshot_ = fpga_view_db_snapshot(db_, &error);
    if (snapshot_ == nullptr) {
      const std::string message = error != nullptr ? error : "unknown error";
      fpga_view_string_free(error);
      FAIL() << "could not snapshot the test database: " << message;
    }
  }

  void TearDown() override {
    fpga_view_snapshot_close(snapshot_);
    fpga_view_db_close(db_);
  }

  fpga_view_db *db_ = nullptr;
  fpga_view_snapshot *snapshot_ = nullptr;
};

TEST(FabricViewParts, ListsThePartsTheMappingDeclares) {
  char *error = nullptr;
  fpga_view_parts *parts = fpga_view_parts_open(kTestDatabaseRoot, &error);
  ASSERT_NE(parts, nullptr) << (error != nullptr ? error : "");
  ASSERT_EQ(fpga_view_parts_count(parts), 1u);
  fpga_view_str name;
  fpga_view_str device;
  fpga_view_str fabric;
  fpga_view_str package;
  fpga_view_str speedgrade;
  ASSERT_EQ(fpga_view_parts_get(parts, 0, &name, &device, &fabric, &package,
                                &speedgrade),
            1);
  EXPECT_EQ(ToString(name), kTestPart);
  EXPECT_EQ(ToString(device), "xc7tiny");
  EXPECT_EQ(ToString(fabric), "xc7tiny");
  EXPECT_EQ(ToString(package), "test");
  EXPECT_EQ(ToString(speedgrade), "1");
  EXPECT_EQ(
    fpga_view_parts_get(parts, 1, &name, nullptr, nullptr, nullptr, nullptr),
    0);
  fpga_view_parts_close(parts);
}

TEST(FabricViewParts, ReportsAMissingDatabase) {
  char *error = nullptr;
  EXPECT_EQ(fpga_view_parts_open("/nonexistent/database", &error), nullptr);
  EXPECT_NE(error, nullptr);
  fpga_view_string_free(error);
}

TEST_F(FabricView, SnapshotOrdersTilesByNameAndReportsTheExtent) {
  uint32_t count = 0;
  const fpga_view_tile *tiles = fpga_view_snapshot_tiles(snapshot_, &count);
  ASSERT_EQ(count, 4u);
  EXPECT_EQ(ToString(tiles[0].name), "BRAM_L_X0Y1");
  EXPECT_EQ(ToString(tiles[1].name), "CLBLM_R_X1Y0");
  EXPECT_EQ(ToString(tiles[2].name), "INT_L_X0Y0");
  EXPECT_EQ(ToString(tiles[3].name), "NULL_X0Y0");

  uint32_t width = 0;
  uint32_t height = 0;
  fpga_view_snapshot_extent(snapshot_, &width, &height);
  EXPECT_EQ(width, 3u);
  EXPECT_EQ(height, 2u);

  EXPECT_EQ(fpga_view_snapshot_find_tile(snapshot_, "INT_L_X0Y0"), 2);
  EXPECT_EQ(fpga_view_snapshot_find_tile(snapshot_, "NOPE_X0Y0"), -1);
}

TEST_F(FabricView, SnapshotCarriesTheTileBitWindowAndSites) {
  uint32_t count = 0;
  const fpga_view_tile *tiles = fpga_view_snapshot_tiles(snapshot_, &count);
  const fpga_view_tile &clb = tiles[1];
  EXPECT_EQ(ToString(clb.type), "CLBLM_R");
  EXPECT_EQ(ToString(clb.clock_region), "X0Y0");
  EXPECT_EQ(clb.grid_x, 2u);
  EXPECT_EQ(clb.grid_y, 1u);
  ASSERT_EQ(clb.bits_block_count, 1u);
  EXPECT_EQ(clb.bits_blocks[0].bus, FPGA_VIEW_BUS_CLB_IO_CLK);
  EXPECT_EQ(clb.bits_blocks[0].base_address, 0x00401080u);
  EXPECT_EQ(clb.bits_blocks[0].frames, 36u);
  EXPECT_EQ(clb.bits_blocks[0].offset, 77);
  EXPECT_EQ(clb.bits_blocks[0].words, 2u);
  EXPECT_EQ(clb.bits_blocks[0].alias_type.size, 0u);

  // A tile with no configuration of its own still appears, so the fabric
  // renders as a whole.
  EXPECT_EQ(tiles[3].bits_block_count, 0u);

  uint32_t site_count = 0;
  const fpga_view_site *sites =
    fpga_view_snapshot_sites(snapshot_, &site_count);
  // The BRAM tile's one site, then the CLB's two.
  ASSERT_EQ(site_count, 3u);
  ASSERT_EQ(clb.site_count, 2u);
  EXPECT_EQ(ToString(sites[clb.site_first].name), "SLICE_X0Y0");
  EXPECT_EQ(ToString(sites[clb.site_first].type), "SLICEM");
  EXPECT_EQ(sites[clb.site_first].tile_index, 1u);
  EXPECT_EQ(ToString(sites[clb.site_first + 1].name), "SLICE_X1Y0");
  EXPECT_EQ(ToString(sites[clb.site_first + 1].type), "SLICEL");
}

TEST_F(FabricView, ResolvesALutInitBitToItsFrameAddress) {
  char *error = nullptr;
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=1", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");

  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  const fpga_view_feature &feature = features[0];
  EXPECT_EQ(feature.outcome, FPGA_VIEW_OUTCOME_BITS);
  EXPECT_EQ(ToString(feature.tile), "CLBLM_R_X1Y0");
  EXPECT_EQ(ToString(feature.tile_type), "CLBLM_R");
  EXPECT_EQ(ToString(feature.tile_feature), "SLICEM_X0.ALUT.INIT");
  EXPECT_EQ(feature.tile_index, 1);
  EXPECT_EQ(feature.start_bit, 34);
  EXPECT_EQ(feature.width, 1);
  EXPECT_EQ(feature.value_bits, 1u);
  ASSERT_EQ(feature.frame_bit_count, 1u);

  uint32_t bit_count = 0;
  const fpga_view_frame_bit *bits = fpga_view_eval_frame_bits(eval, &bit_count);
  ASSERT_EQ(bit_count, 1u);
  const fpga_view_frame_bit &bit = bits[feature.frame_bit_first];
  // segbit "34_06" on a block based at 0x00401080 with word offset 77:
  // frame 0x401080 + 34, bit 77 * 32 + 6 counted from the frame's first word.
  EXPECT_EQ(bit.bus, FPGA_VIEW_BUS_CLB_IO_CLK);
  EXPECT_EQ(bit.frame_address, 0x00401080u + 34u);
  EXPECT_EQ(bit.word, 77u);
  EXPECT_EQ(bit.index, 6u);
  EXPECT_EQ(bit.word_column, 34u);
  EXPECT_EQ(bit.word_bit, 6);
  EXPECT_EQ(bit.feature_address, 34u);
  EXPECT_EQ(bit.value, 1u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, AWidthAssignmentAddressesOneFeatureBitPerSetValueBit) {
  char *error = nullptr;
  // Bits 34 and 35 of INIT, of which only 35 is set.
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[35:34]=2'b10", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  ASSERT_EQ(features[0].frame_bit_count, 1u);
  uint32_t bit_count = 0;
  const fpga_view_frame_bit *bits = fpga_view_eval_frame_bits(eval, &bit_count);
  EXPECT_EQ(bits[0].feature_address, 35u);
  EXPECT_EQ(bits[0].frame_address, 0x00401080u + 35u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, ReportsBothTheSetAndTheClearedBitsOfAFeature) {
  char *error = nullptr;
  fpga_view_eval *eval =
    fpga_view_db_eval(db_, snapshot_, "CLBLM_R_X1Y0.SLICEM_X0.WA7USED", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_BITS);
  ASSERT_EQ(features[0].frame_bit_count, 2u);
  uint32_t bit_count = 0;
  const fpga_view_frame_bit *bits = fpga_view_eval_frame_bits(eval, &bit_count);
  ASSERT_EQ(bit_count, 2u);
  // "32_07 !33_07": the second bit is cleared, and the viewer has to show
  // that difference rather than two identical marks.
  EXPECT_THAT((std::vector<uint32_t>{bits[0].word_column, bits[1].word_column}),
              ElementsAre(32u, 33u));
  EXPECT_EQ(bits[0].value, 1u);
  EXPECT_EQ(bits[1].value, 0u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, APseudoPipResolvesToNoBitsWithoutBeingAnError) {
  char *error = nullptr;
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "CLBLM_R_X1Y0.SLICEM_X0.CLKINV.CLK", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_PSEUDO_PIP);
  EXPECT_EQ(features[0].frame_bit_count, 0u);
  EXPECT_EQ(features[0].error.size, 0u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, ReportsAnUnknownFeatureWithoutFailingTheWholeInput) {
  char *error = nullptr;
  fpga_view_eval *eval =
    fpga_view_db_eval(db_, snapshot_,
                      "CLBLM_R_X1Y0.SLICEM_X0.NOT_A_FEATURE\n"
                      "CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=1\n",
                      &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 2u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_ERROR);
  EXPECT_NE(features[0].error.size, 0u);
  EXPECT_EQ(features[0].line, 1u);
  // The line after a bad one still resolves.
  EXPECT_EQ(features[1].outcome, FPGA_VIEW_OUTCOME_BITS);
  EXPECT_EQ(features[1].line, 2u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, ReportsAnUnknownTile) {
  char *error = nullptr;
  fpga_view_eval *eval =
    fpga_view_db_eval(db_, snapshot_, "NOPE_X9Y9.SOME.FEATURE", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_ERROR);
  EXPECT_EQ(features[0].tile_index, -1);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, AnAssignmentOfZeroAddressesNothing) {
  char *error = nullptr;
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=0", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_ZERO_VALUE);
  EXPECT_EQ(features[0].frame_bit_count, 0u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, EvaluatesAMultiLineInputAndKeepsTheBitRangesDisjoint) {
  char *error = nullptr;
  fpga_view_eval *eval =
    fpga_view_db_eval(db_, snapshot_,
                      "CLBLM_R_X1Y0.SLICEM_X0.ALUT.INIT[34]=1\n"
                      "CLBLM_R_X1Y0.SLICEM_X0.WA7USED\n",
                      &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 2u);
  EXPECT_EQ(features[0].frame_bit_first, 0u);
  EXPECT_EQ(features[0].frame_bit_count, 1u);
  EXPECT_EQ(features[1].frame_bit_first, 1u);
  EXPECT_EQ(features[1].frame_bit_count, 2u);
  uint32_t bit_count = 0;
  fpga_view_eval_frame_bits(eval, &bit_count);
  EXPECT_EQ(bit_count, 3u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, ATilesBitBlocksAreReportedInBusOrder) {
  uint32_t count = 0;
  const fpga_view_tile *tiles = fpga_view_snapshot_tiles(snapshot_, &count);
  const fpga_view_tile &bram = tiles[0];
  ASSERT_EQ(ToString(bram.name), "BRAM_L_X0Y1");
  // The grid is a hash map, so without an explicit order a caller that
  // draws "the first block" would get a different one from run to run.
  ASSERT_EQ(bram.bits_block_count, 2u);
  EXPECT_EQ(bram.bits_blocks[0].bus, FPGA_VIEW_BUS_CLB_IO_CLK);
  EXPECT_EQ(bram.bits_blocks[0].base_address, 0x00400300u);
  EXPECT_EQ(bram.bits_blocks[1].bus, FPGA_VIEW_BUS_BLOCK_RAM);
  EXPECT_EQ(bram.bits_blocks[1].base_address, 0x00c00000u);
}

TEST_F(FabricView, AContentBitLandsOnTheBlockRamBusNotTheRoutingOne) {
  char *error = nullptr;
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "BRAM_L_X0Y1.RAMB18_Y0.INIT_00[2:0]=3'b100", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  EXPECT_EQ(features[0].outcome, FPGA_VIEW_OUTCOME_BITS);
  ASSERT_EQ(features[0].frame_bit_count, 1u);
  uint32_t bit_count = 0;
  const fpga_view_frame_bit *bits = fpga_view_eval_frame_bits(eval, &bit_count);
  // "0_05" on the block ram block, which is based at 0x00c00000 with a word
  // offset of 0 -- not the routing block the tile also has.
  EXPECT_EQ(bits[0].bus, FPGA_VIEW_BUS_BLOCK_RAM);
  EXPECT_EQ(bits[0].frame_address, 0x00c00000u);
  EXPECT_EQ(bits[0].word, 0u);
  EXPECT_EQ(bits[0].index, 5u);
  EXPECT_EQ(bits[0].word_column, 0u);
  EXPECT_EQ(bits[0].word_bit, 5);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, ARoutingBitOnTheSameTileLandsOnTheOtherBus) {
  char *error = nullptr;
  fpga_view_eval *eval = fpga_view_db_eval(
    db_, snapshot_, "BRAM_L_X0Y1.RAMB18_Y0.READ_WIDTH_A_1", &error);
  ASSERT_NE(eval, nullptr) << (error != nullptr ? error : "");
  uint32_t feature_count = 0;
  const fpga_view_feature *features =
    fpga_view_eval_features(eval, &feature_count);
  ASSERT_EQ(feature_count, 1u);
  ASSERT_EQ(features[0].frame_bit_count, 1u);
  uint32_t bit_count = 0;
  const fpga_view_frame_bit *bits = fpga_view_eval_frame_bits(eval, &bit_count);
  EXPECT_EQ(bits[0].bus, FPGA_VIEW_BUS_CLB_IO_CLK);
  EXPECT_EQ(bits[0].frame_address, 0x00400300u + 21u);
  EXPECT_EQ(bits[0].index, 7u);
  fpga_view_eval_close(eval);
}

TEST_F(FabricView, CatalogListsEveryDocumentedFeatureAndPseudoPip) {
  char *error = nullptr;
  fpga_view_catalog *catalog = fpga_view_db_catalog(db_, "CLBLM_R", &error);
  ASSERT_NE(catalog, nullptr) << (error != nullptr ? error : "");
  uint32_t entry_count = 0;
  const fpga_view_catalog_entry *entries =
    fpga_view_catalog_entries(catalog, &entry_count);
  ASSERT_EQ(entry_count, 4u);
  EXPECT_EQ(ToString(entries[0].name), "CLBLM_R.SLICEL_X1.AFF.ZINI");
  EXPECT_EQ(entries[0].address, 0u);
  EXPECT_EQ(ToString(entries[1].name), "CLBLM_R.SLICEM_X0.ALUT.INIT");
  EXPECT_EQ(entries[1].address, 34u);
  EXPECT_EQ(ToString(entries[2].name), "CLBLM_R.SLICEM_X0.ALUT.INIT");
  EXPECT_EQ(entries[2].address, 35u);
  EXPECT_EQ(ToString(entries[3].name), "CLBLM_R.SLICEM_X0.WA7USED");

  uint32_t segbit_count = 0;
  const fpga_view_segbit *segbits =
    fpga_view_catalog_segbits(catalog, &segbit_count);
  ASSERT_EQ(entries[3].segbit_count, 2u);
  ASSERT_LE(entries[3].segbit_first + 2u, segbit_count);
  EXPECT_EQ(segbits[entries[3].segbit_first].word_column, 32u);
  EXPECT_EQ(segbits[entries[3].segbit_first].is_set, 1u);
  EXPECT_EQ(segbits[entries[3].segbit_first + 1].word_column, 33u);
  EXPECT_EQ(segbits[entries[3].segbit_first + 1].is_set, 0u);

  uint32_t ppip_count = 0;
  const fpga_view_ppip *ppips = fpga_view_catalog_ppips(catalog, &ppip_count);
  ASSERT_EQ(ppip_count, 1u);
  EXPECT_EQ(ToString(ppips[0].name), "CLBLM_R.SLICEM_X0.CLKINV.CLK");
  EXPECT_EQ(ppips[0].type, FPGA_VIEW_PPIP_DEFAULT);
  fpga_view_catalog_close(catalog);
}

TEST_F(FabricView, CatalogReportsAnUnknownTileType) {
  char *error = nullptr;
  EXPECT_EQ(fpga_view_db_catalog(db_, "NOT_A_TILE_TYPE", &error), nullptr);
  EXPECT_NE(error, nullptr);
  fpga_view_string_free(error);
}
}  // namespace
