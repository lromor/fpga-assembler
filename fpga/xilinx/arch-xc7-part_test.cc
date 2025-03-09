#include "fpga/xilinx/arch-xc7-part.h"

#include <optional>
#include <vector>

#include "fpga/xilinx/arch-xc7-defs.h"
#include "fpga/xilinx/arch-xc7-frame-address.h"
#include "gtest/gtest.h"

namespace fpga {
namespace xilinx {
namespace xc7 {
namespace {
TEST(ConfigurationColumnTest, IsValidFrameAddress) {
  const ConfigurationColumn column(10);

  // Inside this column.
  EXPECT_TRUE(column.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 3)));
  // Past this column's frame width.
  EXPECT_FALSE(column.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 10)));
}

TEST(ConfigurationColumnTest, GetNextFrameAddressYieldNextAddressInColumn) {
  const ConfigurationColumn column(10);

  std::optional<FrameAddress> next_address = column.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 3));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 4));
}

TEST(ConfigurationColumnTest, GetNextFrameAddressYieldNothingAtEndOfColumn) {
  const ConfigurationColumn column(10);

  EXPECT_FALSE(column.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 9)));
}

TEST(ConfigurationColumnTest, GetNextFrameAddressYieldNothingOutsideColumn) {
  const ConfigurationColumn column(10);

  // Just past last frame in column.
  EXPECT_FALSE(column.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 2, 10)));
}

TEST(ConfigurationBusTest, IsValidFrameAddress) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 1);

  const ConfigurationBus bus(addresses.begin(), addresses.end());

  EXPECT_TRUE(bus.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 0)));
  EXPECT_TRUE(bus.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 1, 1)));

  EXPECT_FALSE(bus.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
}

TEST(ConfigurationBusTest, GetNextFrameAddressYieldNextAddressInBus) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 1);

  const ConfigurationBus bus(addresses.begin(), addresses.end());

  auto next_address =
    bus.GetNextFrameAddress(FrameAddress(BlockType::kBlockRam, false, 0, 0, 0));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kBlockRam, false, 0, 0, 1));

  next_address =
    bus.GetNextFrameAddress(FrameAddress(BlockType::kBlockRam, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kBlockRam, false, 0, 1, 0));
}

TEST(ConfigurationBusTest, GetNextFrameAddressYieldNothingAtEndOfBus) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 1, 1);

  const ConfigurationBus bus(addresses.begin(), addresses.end());

  EXPECT_FALSE(bus.GetNextFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 1, 1)));
}

TEST(RowTest, IsValidFrameAddress) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);

  const Row row(addresses.begin(), addresses.end());

  EXPECT_TRUE(row.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0)));
  EXPECT_TRUE(row.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0)));
  EXPECT_TRUE(row.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));

  EXPECT_FALSE(row.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 2)));
  EXPECT_FALSE(row.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 2, 0)));
}

TEST(RowTest, GetNextFrameAddressYieldNextAddressInRow) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);

  const Row row(addresses.begin(), addresses.end());

  auto next_address =
    row.GetNextFrameAddress(FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));

  next_address =
    row.GetNextFrameAddress(FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0));

  // Rows have unique behavior for GetNextFrameAddress() at the end of a
  // bus. Since the addresses need to be returned in numerically
  // increasing order, all of the rows need to be returned before moving
  // to a different bus.  That means that Row::GetNextFrameAddress() needs
  // to return no object at the end of a bus and let the caller use that
  // as a signal to try the next row.
  next_address =
    row.GetNextFrameAddress(FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 1));
  EXPECT_FALSE(next_address);

  next_address =
    row.GetNextFrameAddress(FrameAddress(BlockType::kBlockRam, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kBlockRam, false, 0, 0, 2));

  next_address =
    row.GetNextFrameAddress(FrameAddress(BlockType::kBlockRam, false, 0, 0, 2));
  EXPECT_FALSE(next_address);
}

TEST(RowTest, GetNextFrameAddressYieldNothingAtEndOfRow) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);

  const Row row(addresses.begin(), addresses.end());

  EXPECT_FALSE(row.GetNextFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
}

TEST(GlobalClockRegionTest, IsValidFrameAddress) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);

  const GlobalClockRegion global_clock_region(addresses.begin(),
                                              addresses.end());

  EXPECT_TRUE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0)));
  EXPECT_TRUE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0)));
  EXPECT_TRUE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
  EXPECT_TRUE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 0)));

  EXPECT_FALSE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 2)));
  EXPECT_FALSE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 2, 0)));
  EXPECT_FALSE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 2, 0, 0)));
  EXPECT_FALSE(global_clock_region.IsValidFrameAddress(
    FrameAddress(BlockType::kCFGCLB, false, 0, 0, 2)));
}

TEST(GlobalClockRegionTest,
     GetNextFrameAddressYieldNextAddressInGlobalClockRegion) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);

  const GlobalClockRegion global_clock_region(addresses.begin(),
                                              addresses.end());

  auto next_address = global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));

  next_address = global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0));

  next_address = global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 0));

  next_address = global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kBlockRam, false, 0, 0, 2));
}

TEST(GlobalClockRegionTest,
     GetNextFrameAddressYieldNothingAtEndOfGlobalClockRegion) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);

  const GlobalClockRegion global_clock_region(addresses.begin(),
                                              addresses.end());

  EXPECT_FALSE(global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 1)));
  EXPECT_FALSE(global_clock_region.GetNextFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
}

TEST(PartTest, IsValidFrameAddress) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 1);

  const Part part(0x1234, addresses.begin(), addresses.end());

  EXPECT_TRUE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0)));
  EXPECT_TRUE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0)));
  EXPECT_TRUE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
  EXPECT_TRUE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 0)));
  EXPECT_TRUE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, true, 0, 0, 0)));

  EXPECT_FALSE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 2)));
  EXPECT_FALSE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 2, 0)));
  EXPECT_FALSE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 2, 0, 0)));
  EXPECT_FALSE(
    part.IsValidFrameAddress(FrameAddress(BlockType::kCFGCLB, false, 0, 0, 2)));
  EXPECT_FALSE(part.IsValidFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, true, 0, 1, 0)));
}

TEST(PartTest, GetNextFrameAddressYieldNextAddressInPart) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 1);

  const Part part(0x1234, addresses.begin(), addresses.end());

  auto next_address = part.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 0));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));

  next_address = part.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 0));

  next_address = part.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 0, 1, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 0));

  next_address = part.GetNextFrameAddress(
    FrameAddress(BlockType::kCLBIOCLK, false, 1, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kCLBIOCLK, true, 0, 0, 0));

  next_address =
    part.GetNextFrameAddress(FrameAddress(BlockType::kCLBIOCLK, true, 0, 0, 1));
  ASSERT_TRUE(next_address);
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*next_address, FrameAddress(BlockType::kBlockRam, false, 0, 0, 0));
}

TEST(PartTest, GetNextFrameAddressYieldNothingAtEndOfPart) {
  std::vector<FrameAddress> addresses;
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 0, 1, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 0);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 1);
  addresses.emplace_back(BlockType::kBlockRam, false, 0, 0, 2);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, false, 1, 0, 1);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 0);
  addresses.emplace_back(BlockType::kCLBIOCLK, true, 0, 0, 1);

  const Part part(0x1234, addresses.begin(), addresses.end());

  EXPECT_FALSE(part.GetNextFrameAddress(
    FrameAddress(BlockType::kBlockRam, false, 0, 0, 2)));
}
}  // namespace
}  // namespace xc7
}  // namespace xilinx
}  // namespace fpga
