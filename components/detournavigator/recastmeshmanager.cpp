#include "recastmeshmanager.hpp"
#include "recastmeshbuilder.hpp"
#include "settings.hpp"

namespace DetourNavigator
{
    RecastMeshManager::RecastMeshManager(const Settings& settings, const TileBounds& bounds, std::size_t generation)
        : mSettings(settings)
        , mGeneration(generation)
        , mTileBounds(bounds)
    {
    }

    bool RecastMeshManager::addObject(const ObjectId id, const btCollisionShape& shape, const btTransform& transform,
                                      const AreaType areaType)
    {
        const auto object = mObjects.lower_bound(id);
        if (object != mObjects.end() && object->first == id)
            return false;
        mObjects.emplace_hint(object, id,
            OscillatingRecastMeshObject(RecastMeshObject(shape, transform, areaType), mRevision + 1));
        ++mRevision;
        return true;
    }

    bool RecastMeshManager::updateObject(const ObjectId id, const btTransform& transform, const AreaType areaType)
    {
        const auto object = mObjects.find(id);
        if (object == mObjects.end())
            return false;
        const std::size_t lastChangeRevision = mLastNavMeshReportedChange.has_value()
                ? mLastNavMeshReportedChange->mRevision : mRevision;
        if (!object->second.update(transform, areaType, lastChangeRevision, mTileBounds))
            return false;
        ++mRevision;
        return true;
    }

    std::optional<RemovedRecastMeshObject> RecastMeshManager::removeObject(const ObjectId id)
    {
        const auto object = mObjects.find(id);
        if (object == mObjects.end())
            return std::nullopt;
        const RemovedRecastMeshObject result {object->second.getImpl().getShape(), object->second.getImpl().getTransform()};
        mObjects.erase(object);
        ++mRevision;
        return result;
    }

    bool RecastMeshManager::addWater(const osg::Vec2i& cellPosition, const int cellSize,
        const btTransform& transform)
    {
        if (!mWater.emplace(cellPosition, Water {cellSize, transform}).second)
            return false;
        ++mRevision;
        return true;
    }

    std::optional<RecastMeshManager::Water> RecastMeshManager::removeWater(const osg::Vec2i& cellPosition)
    {
        const auto water = mWater.find(cellPosition);
        if (water == mWater.end())
            return std::nullopt;
        ++mRevision;
        const Water result = water->second;
        mWater.erase(water);
        return result;
    }

    std::shared_ptr<RecastMesh> RecastMeshManager::getMesh()
    {
        TileBounds tileBounds = mTileBounds;
        tileBounds.mMin /= mSettings.mRecastScaleFactor;
        tileBounds.mMax /= mSettings.mRecastScaleFactor;
        RecastMeshBuilder builder(tileBounds);
        for (const auto& [k, v] : mWater)
            builder.addWater(v.mCellSize, v.mTransform);
        for (const auto& [k, object] : mObjects)
        {
            const RecastMeshObject& v = object.getImpl();
            builder.addObject(v.getShape(), v.getTransform(), v.getAreaType());
        }
        return std::move(builder).create(mGeneration, mRevision);
    }

    bool RecastMeshManager::isEmpty() const
    {
        return mObjects.empty();
    }

    void RecastMeshManager::reportNavMeshChange(const Version& recastMeshVersion, const Version& navMeshVersion)
    {
        if (recastMeshVersion.mGeneration != mGeneration)
            return;
        if (mLastNavMeshReport.has_value() && navMeshVersion < mLastNavMeshReport->mNavMeshVersion)
            return;
        mLastNavMeshReport = {recastMeshVersion.mRevision, navMeshVersion};
        if (!mLastNavMeshReportedChange.has_value()
                || mLastNavMeshReportedChange->mNavMeshVersion < mLastNavMeshReport->mNavMeshVersion)
            mLastNavMeshReportedChange = mLastNavMeshReport;
    }

    Version RecastMeshManager::getVersion() const
    {
        return Version {mGeneration, mRevision};
    }
}
