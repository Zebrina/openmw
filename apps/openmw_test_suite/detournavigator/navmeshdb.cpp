#include "generate.hpp"

#include <components/detournavigator/navmeshdb.hpp>
#include <components/esm/cellid.hpp>

#include <DetourAlloc.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <numeric>
#include <random>

namespace
{
    using namespace testing;
    using namespace DetourNavigator;
    using namespace DetourNavigator::Tests;

    struct Tile
    {
        std::string mWorldspace;
        TilePosition mTilePosition;
        std::vector<std::byte> mInput;
        std::vector<std::byte> mData;
    };

    struct DetourNavigatorNavMeshDbTest : Test
    {
        NavMeshDb mDb {":memory:"};
        std::minstd_rand mRandom;

        std::vector<std::byte> generateData()
        {
            std::vector<std::byte> data(32);
            generateRange(data.begin(), data.end(), mRandom);
            return data;
        }

        Tile insertTile(TileId tileId, TileVersion version)
        {
            std::string worldspace = "sys::default";
            const TilePosition tilePosition {3, 4};
            std::vector<std::byte> input = generateData();
            std::vector<std::byte> data = generateData();
            EXPECT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
            return {std::move(worldspace), tilePosition, std::move(input), std::move(data)};
        }
    };

    TEST_F(DetourNavigatorNavMeshDbTest, get_max_tile_id_for_empty_db_should_return_zero)
    {
        EXPECT_EQ(mDb.getMaxTileId(), TileId {0});
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_tile_should_be_found_by_key)
    {
        const TileId tileId {146};
        const TileVersion version {1};
        const auto [worldspace, tilePosition, input, data] = insertTile(tileId, version);
        const auto result = mDb.findTile(worldspace, tilePosition, input);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->mTileId, tileId);
        EXPECT_EQ(result->mVersion, version);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_tile_should_change_max_tile_id)
    {
        insertTile(TileId {53}, TileVersion {1});
        EXPECT_EQ(mDb.getMaxTileId(), TileId {53});
    }

    TEST_F(DetourNavigatorNavMeshDbTest, updated_tile_should_change_data)
    {
        const TileId tileId {13};
        const TileVersion version {1};
        auto [worldspace, tilePosition, input, data] = insertTile(tileId, version);
        generateRange(data.begin(), data.end(), mRandom);
        ASSERT_EQ(mDb.updateTile(tileId, version, data), 1);
        const auto row = mDb.getTileData(worldspace, tilePosition, input);
        ASSERT_TRUE(row.has_value());
        EXPECT_EQ(row->mTileId, tileId);
        EXPECT_EQ(row->mVersion, version);
        ASSERT_FALSE(row->mData.empty());
        EXPECT_EQ(row->mData, data);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, on_inserted_duplicate_should_throw_exception)
    {
        const TileId tileId {53};
        const TileVersion version {1};
        const std::string worldspace = "sys::default";
        const TilePosition tilePosition {3, 4};
        const std::vector<std::byte> input = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
        EXPECT_THROW(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), std::runtime_error);
    }

    TEST_F(DetourNavigatorNavMeshDbTest, inserted_duplicate_leaves_db_in_correct_state)
    {
        const TileId tileId {53};
        const TileVersion version {1};
        const std::string worldspace = "sys::default";
        const TilePosition tilePosition {3, 4};
        const std::vector<std::byte> input = generateData();
        const std::vector<std::byte> data = generateData();
        ASSERT_EQ(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), 1);
        EXPECT_THROW(mDb.insertTile(tileId, worldspace, tilePosition, version, input, data), std::runtime_error);
        EXPECT_NO_THROW(insertTile(TileId {54}, version));
    }
}
