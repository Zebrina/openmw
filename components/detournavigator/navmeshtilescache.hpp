#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_NAVMESHTILESCACHE_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_NAVMESHTILESCACHE_H

#include "offmeshconnection.hpp"
#include "navmeshdata.hpp"
#include "recastmesh.hpp"
#include "tileposition.hpp"

#include <atomic>
#include <map>
#include <list>
#include <mutex>
#include <cassert>
#include <cstring>
#include <vector>

namespace osg
{
    class Stats;
}

namespace DetourNavigator
{
    struct NavMeshDataRef
    {
        unsigned char* mValue;
        int mSize;
    };

    class NavMeshTilesCache
    {
    public:
        struct Item
        {
            std::atomic<std::int64_t> mUseCount;
            osg::Vec3f mAgentHalfExtents;
            TilePosition mChangedTile;
            std::vector<unsigned char> mNavMeshKey;
            NavMeshData mNavMeshData;

            Item(const osg::Vec3f& agentHalfExtents, const TilePosition& changedTile, std::vector<unsigned char>&& navMeshKey)
                : mUseCount(0)
                , mAgentHalfExtents(agentHalfExtents)
                , mChangedTile(changedTile)
                , mNavMeshKey(std::move(navMeshKey))
            {}
        };

        using ItemIterator = std::list<Item>::iterator;

        class Value
        {
        public:
            Value()
                : mOwner(nullptr), mIterator() {}

            Value(NavMeshTilesCache& owner, ItemIterator iterator)
                : mOwner(&owner), mIterator(iterator)
            {
            }

            Value(const Value& other) = delete;

            Value(Value&& other)
                : mOwner(other.mOwner), mIterator(other.mIterator)
            {
                other.mOwner = nullptr;
            }

            ~Value()
            {
                if (mOwner)
                    mOwner->releaseItem(mIterator);
            }

            Value& operator =(const Value& other) = delete;

            Value& operator =(Value&& other)
            {
                if (mOwner)
                    mOwner->releaseItem(mIterator);

                mOwner = other.mOwner;
                mIterator = other.mIterator;

                other.mOwner = nullptr;

                return *this;
            }

            NavMeshDataRef get() const
            {
                return NavMeshDataRef {mIterator->mNavMeshData.mValue.get(), mIterator->mNavMeshData.mSize};
            }

            operator bool() const
            {
                return mOwner;
            }

        private:
            NavMeshTilesCache* mOwner;
            ItemIterator mIterator;
        };

        explicit NavMeshTilesCache(std::size_t maxNavMeshDataSize);

        Value get(const osg::Vec3f& agentHalfExtents, const TilePosition& changedTile,
            const RecastMesh& recastMesh, const std::vector<OffMeshConnection>& offMeshConnections);

        Value set(const osg::Vec3f& agentHalfExtents, const TilePosition& changedTile,
            const RecastMesh& recastMesh, const std::vector<OffMeshConnection>& offMeshConnections,
            NavMeshData&& value);

        void reportStats(unsigned int frameNumber, osg::Stats& stats) const;

    private:
        class KeyView
        {
        public:
            KeyView() = default;

            virtual ~KeyView() = default;

            KeyView(const std::vector<unsigned char>& value)
                : mValue(&value) {}

            const std::vector<unsigned char>& getValue() const
            {
                assert(mValue);
                return *mValue;
            }

            virtual int compare(const std::vector<unsigned char>& other) const
            {
                assert(mValue);

                const auto valueSize = mValue->size();
                const auto otherSize = other.size();

                if (const auto result = std::memcmp(mValue->data(), other.data(), std::min(valueSize, otherSize)))
                    return result;

                if (valueSize < otherSize)
                    return -1;

                if (valueSize > otherSize)
                    return 1;

                return 0;
            }

            virtual bool isLess(const KeyView& other) const
            {
                assert(mValue);
                return other.compare(*mValue) > 0;
            }

            friend bool operator <(const KeyView& lhs, const KeyView& rhs)
            {
                return lhs.isLess(rhs);
            }

        private:
            const std::vector<unsigned char>* mValue = nullptr;
        };

        class RecastMeshKeyView : public KeyView
        {
        public:
            RecastMeshKeyView(const RecastMesh& recastMesh, const std::vector<OffMeshConnection>& offMeshConnections)
                : mRecastMesh(recastMesh), mOffMeshConnections(offMeshConnections) {}

            int compare(const std::vector<unsigned char>& other) const override;

            bool isLess(const KeyView& other) const override
            {
                return compare(other.getValue()) < 0;
            }

            ~RecastMeshKeyView() override = default;

        private:
            std::reference_wrapper<const RecastMesh> mRecastMesh;
            std::reference_wrapper<const std::vector<OffMeshConnection>> mOffMeshConnections;
        };

        struct TileMap
        {
            std::map<KeyView, ItemIterator> mMap;
        };

        mutable std::mutex mMutex;
        std::size_t mMaxNavMeshDataSize;
        std::size_t mUsedNavMeshDataSize;
        std::size_t mFreeNavMeshDataSize;
        std::list<Item> mBusyItems;
        std::list<Item> mFreeItems;
        std::map<osg::Vec3f, std::map<TilePosition, TileMap>> mValues;

        void removeLeastRecentlyUsed();

        void acquireItemUnsafe(ItemIterator iterator);

        void releaseItem(ItemIterator iterator);

        static std::size_t getSize(const Item& item)
        {
            return static_cast<std::size_t>(item.mNavMeshData.mSize) + 2 * item.mNavMeshKey.size();
        }
    };
}

#endif
